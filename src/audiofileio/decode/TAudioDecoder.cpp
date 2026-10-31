// SPDX-FileCopyrightText: 2007-2026 Ben Levitt, Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TAudioDecoder.h"

#include "TAudioBuffer.h"

#include "SFAudioReader.h"
#include "WPAudioReader.h"
#include "MpgAudioReader.h"
#include "TScenarioDecoder.h"

#if defined M4A_DECODE_SUPPORT
#include "FaadAudioReader.h"
#endif

#include <QFile>
#include <QFileInfo>

#include <cstring>

TAudioFileStatus TAudioDecoder::seek(nframes_t frame)
{
    // A seek of the open itself (mpg123, FAAD) counts for the open
    if (m_stats.phase == TAudioIOStats::Phase::Open) {
        return seek_frame(frame);
    }
    set_phase(TAudioIOStats::Phase::Seek);
    const quint64 start = TByteSource::now_nanoseconds();
    TAudioFileStatus status = seek_frame(frame);
    m_stats.seek.nanoseconds += TByteSource::now_nanoseconds() - start;
    ++m_stats.seek.calls;
    set_phase(TAudioIOStats::Phase::Stream);
    return status;
}

TAudioFileResult TAudioDecoder::read(TAudioBuffer& buffer, nframes_t count)
{
    const quint64 start = TByteSource::now_nanoseconds();
    TAudioFileResult result = read_frames(buffer, count);
    m_stats.stream.nanoseconds += TByteSource::now_nanoseconds() - start;
    ++m_stats.stream.calls;
    m_stats.stream.frames += result.frames;
    return result;
}

void TAudioDecoder::opened(const TAudioPhaseStats& probes, quint64 nanoseconds)
{
    m_stats.open.nanoseconds = nanoseconds;
    m_stats.open.bytes.add(probes.bytes);
    set_phase(TAudioIOStats::Phase::Stream);
}

// The one place the phase changes: every source of this decoder (WavPack
// has two, the .wv and its .wvc) counts into the new phase's bytes, so none
// can be forgotten
void TAudioDecoder::set_phase(TAudioIOStats::Phase phase)
{
    m_stats.phase = phase;
    for (const std::unique_ptr<TByteSource>& source : m_sources) {
        source->set_stats(&m_stats.current().bytes);
    }
}

TByteSource* TAudioDecoder::open_source(const QString& fileName)
{
    std::unique_ptr<TByteSource> source = m_opener ? m_opener(fileName) : TByteSource::open_file(fileName);
    if (!source) {
        return nullptr;
    }
    source->set_stats(&m_stats.current().bytes);
    // The audio file's medium: the first source (WavPack's .wvc comes second)
    if (m_stats.mediumId == 0) {
        m_stats.mediumId = source->medium_id();
    }
    m_sources.push_back(std::move(source));
    return m_sources.back().get();
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
    Mpg
};

// A signature names a container or a format for certain: the library behind it
// decides on the rest. Fixed bytes only, no versions, subtypes or codecs; a
// format that is not here goes through the probes. ID3 is not here: a tag can
// precede MP3, FLAC or WavPack.
struct DecoderSignature
{
    char bytes[5];
    int offset;
    DecoderLibrary library;
};

static constexpr DecoderSignature DecoderSignatures[] = {
    {"RIFF", 0, DecoderLibrary::SndFile},
    {"RF64", 0, DecoderLibrary::SndFile},
    {"riff", 0, DecoderLibrary::SndFile},
    {"FORM", 0, DecoderLibrary::SndFile},
    {"fLaC", 0, DecoderLibrary::SndFile},
    {"OggS", 0, DecoderLibrary::SndFile},
    {"caff", 0, DecoderLibrary::SndFile},
    {".snd", 0, DecoderLibrary::SndFile},
    {"wvpk", 0, DecoderLibrary::WavPack},
    {"ftyp", 4, DecoderLibrary::Faad},
};

static DecoderLibrary library_of_signature(TByteSource& source)
{
    char head[8]{};
    source.seek(0);
    source.read(head, sizeof(head));
    for (const DecoderSignature& signature : DecoderSignatures) {
        if (std::memcmp(head + signature.offset, signature.bytes, 4) == 0) {
            return signature.library;
        }
    }
    return DecoderLibrary::None;
}

// What libsndfile found: its MPEG goes nowhere (a file the strict probe
// refused), MP3 in a WAV to mpg123, which reads it whole where libsndfile takes
// the length from the file size
static DecoderLibrary library_of_sndfile_format(int format)
{
    const int major = format & SF_FORMAT_TYPEMASK;
    if (major == SF_FORMAT_WAV && (format & SF_FORMAT_SUBMASK) == SF_FORMAT_MPEG_LAYER_III) {
        return DecoderLibrary::Mpg;
    }
    return major == 0 || major == SF_FORMAT_MPEG ? DecoderLibrary::None : DecoderLibrary::SndFile;
}

// The library that can read fileName, by the file's content; the probes' reads
// go into stats. A match really opens the file: can_decode_audio() has no
// other check, a signature alone proves nothing.
static DecoderLibrary find_decoder(const QString& fileName, const TByteSourceOpener& opener, TAudioIOStats& stats)
{
    // A scenario (a test signal described, TScenarioDecoder) by its name, the
    // one exception to "by content": a test facility, no audio format; one
    // that does not parse is no audio file. Cheap for every other file: only
    // the name is compared.
    if (TScenarioDecoder::is_scenario_file(fileName)) {
        return TScenarioDecoder::check(fileName) ? DecoderLibrary::Scenario : DecoderLibrary::None;
    }
    const std::unique_ptr<TByteSource> source = opener ? opener(fileName) : TByteSource::open_file(fileName);
    if (!source) {
        return DecoderLibrary::None;
    }
    source->set_stats(&stats.open.bytes);

    switch (library_of_signature(*source)) {
    case DecoderLibrary::SndFile:
        // A RIFF libsndfile does not know (RMP3) can still hold MP3
        if (const int format = SFAudioReader::format(*source)) {
            return library_of_sndfile_format(format);
        }
        return MpgAudioReader::can_decode(*source) ? DecoderLibrary::Mpg : DecoderLibrary::None;
    case DecoderLibrary::WavPack:
        return WPAudioReader::can_decode(*source) ? DecoderLibrary::WavPack : DecoderLibrary::None;
    case DecoderLibrary::Faad:
#if defined M4A_DECODE_SUPPORT
        return FaadAudioReader::can_decode(*source) ? DecoderLibrary::Faad : DecoderLibrary::None;
#else
        return DecoderLibrary::None;
#endif
    default:
        break;
    }

    if (MpgAudioReader::can_decode(*source)) {
        return DecoderLibrary::Mpg;
    }
    if (const int format = SFAudioReader::format(*source)) {
        return library_of_sndfile_format(format);
    }
    // ADTS and WavPack behind an ID3 tag have no signature, and libsndfile
    // does not read them
    if (WPAudioReader::can_decode(*source)) {
        return DecoderLibrary::WavPack;
    }
#if defined M4A_DECODE_SUPPORT
    if (FaadAudioReader::can_decode(*source)) {
        return DecoderLibrary::Faad;
    }
#endif
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

std::expected<std::unique_ptr<TAudioDecoder>, TAudioFileError> open_audio_decoder(const QString& fileName, const TByteSourceOpener& opener)
{
    std::unique_ptr<TAudioDecoder> decoder;
    const quint64 start = TByteSource::now_nanoseconds();
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
    case DecoderLibrary::Mpg: {
        std::unique_ptr<MpgAudioReader> mpg = std::make_unique<MpgAudioReader>(fileName, opener);
        mpg->scan();
        decoder = std::move(mpg);
        break;
    }
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

    decoder->opened(probes.open, TByteSource::now_nanoseconds() - start);
    return decoder;
}
