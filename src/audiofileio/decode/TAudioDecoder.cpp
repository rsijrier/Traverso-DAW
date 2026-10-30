// SPDX-FileCopyrightText: 2007-2026 Ben Levitt, Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TAudioDecoder.h"

#include "TAudioBuffer.h"

#include "SFAudioReader.h"
#include "WPAudioReader.h"
#include "MadAudioReader.h"
#include "TScenarioDecoder.h"

#if defined M4A_DECODE_SUPPORT
#include "FaadAudioReader.h"
#endif

#include <QFile>
#include <QFileInfo>

TAudioFileStatus TAudioDecoder::seek(nframes_t frame)
{
    // A seek of the open itself (MAD, FAAD) counts for the open
    if (m_stats.phase == TAudioIOStats::Phase::Open) {
        return seek_frame(frame);
    }
    m_stats.phase = TAudioIOStats::Phase::Seek;
    const quint64 start = TAudioByteSource::now_nanoseconds();
    TAudioFileStatus status = seek_frame(frame);
    m_stats.seek.nanoseconds += TAudioByteSource::now_nanoseconds() - start;
    ++m_stats.seek.calls;
    m_stats.phase = TAudioIOStats::Phase::Stream;
    return status;
}

TAudioFileResult TAudioDecoder::read(TAudioBuffer& buffer, nframes_t count)
{
    const quint64 start = TAudioByteSource::now_nanoseconds();
    TAudioFileResult result = read_frames(buffer, count);
    m_stats.stream.nanoseconds += TAudioByteSource::now_nanoseconds() - start;
    ++m_stats.stream.calls;
    m_stats.stream.frames += result.frames;
    return result;
}

void TAudioDecoder::opened(const TAudioPhaseStats& probes, quint64 nanoseconds)
{
    m_stats.open.nanoseconds = nanoseconds;
    m_stats.open.cached.add(probes.cached);
    m_stats.open.medium.add(probes.medium);
    m_stats.open.untold.add(probes.untold);
    m_stats.phase = TAudioIOStats::Phase::Stream;
}

TAudioByteSource* TAudioDecoder::open_source(const QString& fileName)
{
    std::unique_ptr<TAudioByteSource> source = m_opener ? m_opener(fileName) : TAudioByteSource::open_file(fileName);
    if (!source) {
        return nullptr;
    }
    source->set_stats(&m_stats);
    m_stats.mediumId = source->medium_id();
    m_sources.push_back(std::move(source));
    return m_sources.back().get();
}

QString TAudioDecoder::take_source_error()
{
    QString reason;
    for (const std::unique_ptr<TAudioByteSource>& source : m_sources) {
        const std::optional<QString> error = source->take_error();
        if (error && reason.isEmpty()) {
            reason = *error;
        }
    }
    return reason;
}

TAudioFileResult TAudioDecoder::read_from(TAudioBuffer& buffer, nframes_t first, nframes_t count)
{
    // Nothing to read: zeros, as read() gives past the end
    const auto nothing = [&](std::optional<TAudioFileError> error) -> TAudioFileResult {
        if (count > 0) {
            buffer.check_capacity(count, m_channels);
            buffer.silence(0, count, m_channels);
        }
        return {0, std::move(error)};
    };

    // At or past the end: no seek, no error (contract point 2)
    if (first >= m_frames) {
        return nothing(std::nullopt);
    }
    // A seek only when the decoder does not read from first already
    if (!reads_from(first)) {
        if (TAudioFileStatus sought = seek(first); !sought) {
            return nothing(std::move(sought.error()));
        }
    }
    return read(buffer, count);
}

TAudioFileStatus TAudioDecoder::seek_landed(nframes_t frame)
{
    m_frame = frame;
    m_error = false;
    take_source_error();
    return {};
}

TAudioFileStatus TAudioDecoder::seek_failed(nframes_t frame, const QString& detail)
{
    m_error = true;
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
    if (read < count && m_frame + read < m_frames) {
        m_error = true;
        result.error = TAudioFileError{TAudioFileError::Code::ReadFailed,
                                       !sourceError.isEmpty() ? sourceError
                                       : detail.isEmpty()     ? QStringLiteral("the audio ends at frame %1 of %2").arg(m_frame + read).arg(m_frames)
                                                              : detail};
    }
    m_frame += read;
    return result;
}

enum class DecoderLibrary {
    None,
    Scenario,
    SndFile,
    WavPack,
    Faad,
    Mad
};

// The library that can read fileName, tried in order of preference; the
// probes' reads go into stats
static DecoderLibrary find_decoder(const QString& fileName, const TAudioSourceOpener& opener, TAudioIOStats& stats)
{
    // A scenario (a test signal described, TScenarioDecoder) by its name,
    // before any library probes it; one that does not parse is no audio file.
    // First, because the libraries probe the bytes: MAD looks for frame
    // syncs and can find one in any text, so a scenario (or a broken one)
    // could pass as an MP3. Cheap for every other file: only the name is
    // compared, the file is opened only when it is called .tscenario.
    if (TScenarioDecoder::is_scenario_file(fileName)) {
        return TScenarioDecoder::check(fileName) ? DecoderLibrary::Scenario : DecoderLibrary::None;
    }
    const std::unique_ptr<TAudioByteSource> source = opener ? opener(fileName) : TAudioByteSource::open_file(fileName);
    if (!source) {
        return DecoderLibrary::None;
    }
    source->set_stats(&stats);
    if (const int format = SFAudioReader::major_format(*source)) {
        // MP3 through MAD, whose seeks are sample accurate; libsndfile's are
        // not (1.2.2, mpg123 lands some hundreds of frames off)
        if (format == SF_FORMAT_MPEG && MadAudioReader::can_decode(*source)) {
            return DecoderLibrary::Mad;
        }
        return DecoderLibrary::SndFile;
    }
    if (WPAudioReader::can_decode(*source)) {
        return DecoderLibrary::WavPack;
    }
#if defined M4A_DECODE_SUPPORT
    if (FaadAudioReader::can_decode(*source)) {
        return DecoderLibrary::Faad;
    }
#endif
    if (MadAudioReader::can_decode(*source)) {
        return DecoderLibrary::Mad;
    }
    return DecoderLibrary::None;
}

bool can_decode_audio(const QString& fileName)
{
    TAudioIOStats stats;
    return find_decoder(fileName, {}, stats) != DecoderLibrary::None;
}

// Why no decoder reads fileName: not there, not readable, or of a format no
// decoder knows. Asked after the probes failed, not before: a check before
// the open is a race and costs a system call on every open.
static TAudioFileError no_decoder(const QString& fileName)
{
    if (!QFileInfo::exists(fileName)) {
        return {TAudioFileError::Code::NotFound, QStringLiteral("no such file")};
    }
    if (QFile file(fileName); !file.open(QIODevice::ReadOnly)) {
        return {TAudioFileError::Code::CannotOpen, file.errorString()};
    }
    if (TScenarioDecoder::is_scenario_file(fileName)) {
        if (std::expected<void, QString> scenario = TScenarioDecoder::check(fileName); !scenario) {
            return {TAudioFileError::Code::CannotOpen, QStringLiteral("not a scenario: %1").arg(scenario.error())};
        }
    }
    return {TAudioFileError::Code::CannotOpen, QStringLiteral("no decoder reads this format")};
}

std::expected<std::unique_ptr<TAudioDecoder>, TAudioFileError> open_audio_decoder(const QString& fileName, const TAudioSourceOpener& opener)
{
    std::unique_ptr<TAudioDecoder> decoder;
    const quint64 start = TAudioByteSource::now_nanoseconds();
    TAudioIOStats probes;

    switch (find_decoder(fileName, opener, probes)) {
    case DecoderLibrary::Scenario:
        decoder = std::make_unique<TScenarioDecoder>(fileName);
        break;
    case DecoderLibrary::SndFile:
        decoder = std::make_unique<SFAudioReader>(fileName, opener);
        break;
    case DecoderLibrary::WavPack:
        decoder = std::make_unique<WPAudioReader>(fileName, opener);
        break;
    case DecoderLibrary::Faad:
#if defined M4A_DECODE_SUPPORT
        decoder = std::make_unique<FaadAudioReader>(fileName, opener);
#endif
        break;
    case DecoderLibrary::Mad:
        decoder = std::make_unique<MadAudioReader>(fileName, opener);
        break;
    case DecoderLibrary::None:
        return std::unexpected(no_decoder(fileName));
    }

    if (!decoder || !decoder->is_valid()) {
        // The decoder's own reason, else what it found (a file that went
        // between the probe and the open lands here too)
        if (decoder && decoder->open_error()) {
            return std::unexpected(*decoder->open_error());
        }
        return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen,
                                               decoder ? QStringLiteral("the %1 decoder cannot read it (%2 channels, %3 Hz, %4 frames)")
                                                             .arg(decoder->decoder_type())
                                                             .arg(decoder->channel_count())
                                                             .arg(decoder->rate())
                                                             .arg(decoder->frame_count())
                                                       : QStringLiteral("no decoder reads this format")});
    }

    decoder->opened(probes.open, TAudioByteSource::now_nanoseconds() - start);
    return decoder;
}
