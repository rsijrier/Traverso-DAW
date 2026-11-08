// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TWaveform.h"

#include "TBufferedAudioStreamReader.h"
#include "TAudioBuffer.h"
#include "TLog.h"
#include "TPeakCache.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TResourcesManager.h"

#include <algorithm>
#include <cmath>

QString TWaveform::peak_path(const TBufferedAudioStream* source)
{
    // In the user's peak cache, named after the audio file's path
    return TPeakCache::path_for(source->get_filename());
}

TWaveform::TWaveform(TBufferedAudioStreamReader* source, RequestBuild build)
    : m_sourceId(source->object_id())
    , m_fileName(source->get_filename())
    , m_rate(source->get_file_rate())
    , m_channelCount(int(source->get_channel_count()))
    , m_frameCount(source->get_nframes())
    , m_sourceValid(source->get_error() >= 0)
    , m_peakPath(peak_path(source))
    , m_decodeBuffer(std::make_unique<TAudioBuffer>())
    , m_requestBuild(std::move(build))
{
    // Delivered once per build, also when it finished before it was watched
    connect(&m_build, &QFutureWatcherBase::finished, this, &TWaveform::build_finished);
    connect(&m_build, &QFutureWatcherBase::progressValueChanged, this, [this](int percent) {
        m_progress = percent;
        emit progress(percent);
    });

    m_closeReader.setSingleShot(true);
    m_closeReader.setInterval(5000);
    connect(&m_closeReader, &QTimer::timeout, this, [this] {
        delete m_source;
        m_source = nullptr;
    });
}

TWaveform::TWaveform(std::shared_ptr<TPeakStream> live)
    : m_live(std::move(live))
    , m_state(State::Ready)
    , m_opened(true)
{
}

TWaveform::~TWaveform()
{
    // A running build is not cancelled: other waveforms of the same source
    // may wait for it (split clips, a clip that recreates its waveform).
    // Builds are cancelled when the project closes.
    delete m_source;
}

int TWaveform::channel_count() const
{
    if (m_live) {
        return m_live->channel_count();
    }
    return m_channelCount;
}

TPeakFile::SourceInfo TWaveform::source_info() const
{
    return TPeakFile::SourceInfo::from_file(m_fileName, m_rate, m_channelCount, m_frameCount);
}

TBufferedAudioStreamReader* TWaveform::reader()
{
    if (!m_source && m_sourceValid) {
        m_source = resources_manager()->get_readsource(m_sourceId);
        if (m_source && m_source->get_error() < 0) {
            delete m_source;
            m_source = nullptr;
            m_sourceValid = false;
        }
    }
    m_closeReader.start();
    return m_source;
}

TTimeRef TWaveform::recorded_length() const
{
    if (!m_live) {
        return TTimeRef();
    }
    // The frames whose blocks a read has
    return TTimeRef(uint64_t(m_live->written_frames()), m_live->rate());
}

void TWaveform::prepare()
{
    if (!m_opened) {
        m_opened = true;
        open_or_build();
    }
}

TWaveform::State TWaveform::state()
{
    prepare();
    return m_live ? State::Ready : m_state;
}

void TWaveform::open_or_build()
{
    if (!m_sourceValid) {
        TLOG_W("peaks", "no readable source for {}", m_peakPath.toStdString());
        m_state = State::Failed;
        return;
    }

    const TPeakFile::Status status = m_file.open(m_peakPath, source_info());
    switch (status) {
    case TPeakFile::Status::Ok:
        TLOG_D("peaks", "opened {}", m_peakPath.toStdString());
        // Used now: the cache keeps what is used
        TPeakCache::touch(m_peakPath);
        m_state = State::Ready;
        return;
    case TPeakFile::Status::Error:
        TLOG_W("peaks", "cannot open {}", m_peakPath.toStdString());
        m_state = State::Failed;
        return;
    case TPeakFile::Status::Missing:
    case TPeakFile::Status::Invalid:
        break;
    }

    // A file that was just built and still does not open is not going to
    if (m_builds > 0) {
        TLOG_W("peaks", "{} does not match its source after building it", m_peakPath.toStdString());
        m_state = State::Failed;
        return;
    }

    start_build(status == TPeakFile::Status::Missing ? "no peak file yet" : "the peak file does not match the source");
}

void TWaveform::start_build(const char* reason)
{
    TLOG_I("peaks", "asking for {} ({})", m_peakPath.toStdString(), reason);
    ++m_builds;
    m_state = State::Building;
    m_progress = 0;
    // No build service (a clip without a sheet): a cancelled future, so the
    // watcher reports a failed build
    const TPeakBuild build = m_requestBuild ? m_requestBuild(m_fileName, m_peakPath, m_rate, m_channelCount, m_frameCount) : TPeakBuild{};
    m_building = build.stream;
    m_build.setFuture(build.future);
}

void TWaveform::build_finished()
{
    // The build's peaks go once the file is open (or the build failed): read
    // no more from them. Another build started below brings its own.
    const std::shared_ptr<TPeakStream> built = std::move(m_building);

    // A cancelled build has no result (the project closes, or its DiskIO went)
    const QFuture<TPeakBuildResult> build = m_build.future();
    const TPeakBuildResult result = !build.isCanceled() && build.resultCount() > 0 ? build.result() : TPeakBuildResult{};

    if (result.ok) {
        open_or_build();
    } else if (m_retries++ == 0) {
        // Once more: the DiskIO that ran it went (its sheet was deleted)
        // while this waveform's clip lives on, or a recording's
        // post-processing failed (its peaks are built from the audio then)
        TLOG_I("peaks", "the build of {} {}, asking again", m_peakPath.toStdString(),
               build.isCanceled() ? "was cancelled" : "failed");
        m_builds = 0;
        open_or_build();
        if (m_state == State::Building) {
            return;
        }
    } else {
        m_state = State::Failed;
    }
    emit ready();
}

TWaveformColumns::Source TWaveform::device_columns(const TTimeRef& start, const TTimeRef& columnWidth, int count,
                                                   const TTimeRef& clipStart, const TTimeRef& clipEnd, TWaveformColumn* out)
{
    auto from = [&](const TPeakLevels& levels, TWaveformSampleSource* samples) {
        return TWaveformColumns::device_columns(levels, samples, start, columnWidth, count, clipStart, clipEnd, out);
    };
    // A file being written: what is written so far, mapped for this call
    // only, the complete chunks with their levels and level 0 of the chunk
    // being written. A fresh mapping costs a page fault per page the query
    // touches: about 0.25 ms per stereo track for 10 minutes in view, more
    // for a wider view (doc/design/peak-records.md).
    auto from_stream = [&](const TPeakStream& stream, TWaveformSampleSource* samples) {
        TWaveformColumns::Source source = TWaveformColumns::Source::None;
        stream.read([&](const TPeakLevels& levels) { source = from(levels, samples); });
        return source;
    };

    const State current = state();
    if (current == State::Building && m_building) {
        // What is built so far (the samples of the source are all there)
        return from_stream(*m_building, this);
    }
    if (current != State::Ready) {
        return TWaveformColumns::Source::None;
    }
    if (m_live) {
        return from_stream(*m_live, nullptr);
    }
    return from(m_file.levels(), this);
}

float TWaveform::max_amplitude(const TTimeRef& start, const TTimeRef& end)
{
    if (state() != State::Ready || m_live) {
        return -1.0f;
    }
    return TWaveformColumns::max_amplitude(m_file.levels(), this,
                                           TTimeRef::frame_at_or_after(start, m_rate), TTimeRef::frame_at_or_after(end, m_rate));
}

TTimeRef TWaveform::nearest_zero_crossing(const TTimeRef& time, const TTimeRef& maxDistance)
{
    if (state() != State::Ready || m_live) {
        return TTimeRef(TTimeRef::INVALID);
    }
    const qint64 frame = TWaveformColumns::nearest_zero_crossing(
        m_file.levels(), this, TTimeRef::frame_at_or_before(time, m_rate),
        qint64(std::ceil(TTimeRef::to_frames(maxDistance, m_rate))));
    return frame < 0 ? TTimeRef(TTimeRef::INVALID) : TTimeRef(uint64_t(frame), m_rate);
}

qint64 TWaveform::read(qint64 firstFrame, qint64 count, float* const* channels)
{
    TBufferedAudioStreamReader* source = reader();
    if (!source) {
        return 0;
    }
    const uint channelCount = uint(m_channelCount);
    // The file's own frames (the peaks' grid), whatever rate the source
    // plays at; the reader makes room in the buffer
    // The error is dropped on purpose: what a waveform does on a read error
    // is task T10
    const qint64 read = source->file_read_frames(*m_decodeBuffer, nframes_t(firstFrame), nframes_t(count)).frames;
    if (read <= 0) {
        return 0;
    }

    const qint64 frames = std::min<qint64>(read, count);
    for (uint c = 0; c < channelCount; ++c) {
        std::copy_n(m_decodeBuffer->get_channel_buffer(c).get_data(nframes_t(frames)), frames, channels[c]);
    }
    return frames;
}
