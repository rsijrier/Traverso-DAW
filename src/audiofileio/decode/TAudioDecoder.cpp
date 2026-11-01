// SPDX-FileCopyrightText: 2007-2026 Ben Levitt, Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TAudioDecoder.h"

#include "TAudioBuffer.h"
#include "TAudioIOScope.h"
#include "TLog.h"

TAudioFileStatus TAudioDecoder::seek(nframes_t frame)
{
    TAudioIOScope scope(*this, TAudioIOStats::Activity::Seek);
    return seek_frame(frame);
}

TAudioFileResult TAudioDecoder::read(TAudioBuffer& buffer, nframes_t count)
{
    TAudioIOScope scope(*this, TAudioIOStats::Activity::Stream);
    TAudioFileResult result = read_frames(buffer, count);
    m_stats.stream.frames += result.frames;
    return result;
}

void TAudioDecoder::adopt_open_cost(const TAudioIOCost& cost)
{
    m_stats.open.calls += cost.calls;
    m_stats.open.nanoseconds += cost.nanoseconds;
    m_stats.open.reads.add(cost.reads);
    set_activity(TAudioIOStats::Activity::Stream);
}

void TAudioDecoder::set_activity(TAudioIOStats::Activity activity)
{
    m_stats.activity = activity;
    for (const std::unique_ptr<TByteSource>& source : m_sources) {
        source->count_reads_into(&m_stats.current_cost().reads);
    }
}

TAudioDecoder::TAudioDecoder(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
    : m_fileName(fileName)
    , m_opener(opener)
{
    // It goes on counting where the open is measured until adopt_open_cost(),
    // so a failed open keeps what it read
    m_stats.mediumId = source->medium_id();
    m_sources.push_back(std::move(source));
}

TByteSource* TAudioDecoder::open_extra_source(const QString& fileName)
{
    TByteSourceResult source = m_opener ? m_opener(fileName) : TByteSource::open_file(fileName);
    if (!source) {
        // No such file is the normal case, a correction file is optional
        if (!source.error().notFound) {
            TLOG_W("audiofile", "{}: cannot read it ({}), going on without", fileName.toStdString(), source.error().reason.toStdString());
        }
        return nullptr;
    }
    (*source)->count_reads_into(&m_stats.current_cost().reads);
    m_sources.push_back(std::move(*source));
    return m_sources.back().get();
}

TAudioFileStatus TAudioDecoder::require_audio() const
{
    if (m_channels > 0 && m_rate > 0 && m_frames > 0) {
        return {};
    }
    return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen,
                                           QStringLiteral("the %1 decoder cannot read it (%2 channels, %3 Hz, %4 frames)").arg(decoder_type()).arg(m_channels).arg(m_rate).arg(m_frames)});
}

QString TAudioDecoder::take_source_error()
{
    QString reason;
    for (const std::unique_ptr<TByteSource>& source : m_sources) {
        const std::optional<QString> error = source->take_error();
        if (error && reason.isEmpty()) {
            reason = *error;
        }
    }
    return reason;
}

TAudioFileResult TAudioDecoder::nothing_read(TAudioBuffer& buffer, nframes_t count, uint channels, std::optional<TAudioFileError> error)
{
    if (count > 0) {
        buffer.check_capacity(count, channels);
        buffer.silence(0, count, channels);
    }
    return {0, std::move(error)};
}

TAudioFileResult TAudioDecoder::read_from(TAudioBuffer& buffer, nframes_t first, nframes_t count)
{
    // At or past the end: no seek, no error (contract point 2)
    if (first >= m_frames) {
        return nothing_read(buffer, count, m_channels, std::nullopt);
    }
    // A seek only when the decoder does not read from first already
    if (!next_read_is_at(first)) {
        if (TAudioFileStatus sought = seek(first); !sought) {
            return nothing_read(buffer, count, m_channels, std::move(sought.error()));
        }
    }
    return read(buffer, count);
}

TAudioFileStatus TAudioDecoder::seek_landed(nframes_t frame)
{
    m_nextFrame = frame;
    m_inErrorState = false;
    take_source_error();
    return {};
}

TAudioFileStatus TAudioDecoder::seek_failed(nframes_t frame, const QString& detail)
{
    m_inErrorState = true;
    const QString sourceError = take_source_error();
    return std::unexpected(TAudioFileError{TAudioFileError::Code::SeekFailed, !sourceError.isEmpty() ? sourceError
                                                                              : detail.isEmpty()     ? QStringLiteral("cannot seek to frame %1").arg(frame)
                                                                                                     : detail});
}

TAudioFileResult TAudioDecoder::read_done(TAudioBuffer& buffer, nframes_t read, nframes_t count, const QString& detail)
{
    // What the file did not give: zeros (contract point 1)
    buffer.silence(read, count - read, m_channels);
    TAudioFileResult result{read, std::nullopt};
    const QString sourceError = take_source_error();
    // Short before the end: the file ends early or a read failed (points 3, 4)
    if (read < count && m_nextFrame + read < m_frames) {
        m_inErrorState = true;
        result.error = TAudioFileError{TAudioFileError::Code::ReadFailed,
                                       !sourceError.isEmpty() ? sourceError
                                       : detail.isEmpty()     ? QStringLiteral("the audio ends at frame %1 of %2").arg(m_nextFrame + read).arg(m_frames)
                                                              : detail};
    }
    m_nextFrame += read;
    return result;
}
