// SPDX-FileCopyrightText: 2007-2026 Ben Levitt, Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TAudioDecoder.h"

#include "TAudioBuffer.h"
#include "TAudioIOScope.h"

#include "SFAudioReader.h"
#include "WPAudioReader.h"
#include "MpgAudioReader.h"
#include "TScenarioDecoder.h"

#if defined M4A_DECODE_SUPPORT
#include "FaadAudioReader.h"
#endif

#include <cstring>

TAudioFileStatus TAudioDecoder::seek(nframes_t frame)
{
    // A seek of the open itself (mpg123, FAAD) counts for the open
    if (m_stats.activity == TAudioIOStats::Activity::Open) {
        return seek_frame(frame);
    }
    TAudioIOScope scope(m_stats, TAudioIOStats::Activity::Seek);
    return seek_frame(frame);
}

TAudioFileResult TAudioDecoder::read(TAudioBuffer& buffer, nframes_t count)
{
    TAudioIOScope scope(m_stats, TAudioIOStats::Activity::Stream);
    TAudioFileResult result = read_frames(buffer, count);
    m_stats.stream.frames += result.frames;
    return result;
}

void TAudioDecoder::adopt_open_cost(const TAudioIOCost& cost)
{
    m_stats.open.calls += cost.calls;
    m_stats.open.nanoseconds += cost.nanoseconds;
    m_stats.open.reads.add(cost.reads);
    m_stats.set_activity(TAudioIOStats::Activity::Stream);
}

TAudioDecoder::TAudioDecoder(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
    : m_fileName(fileName)
    , m_opener(opener)
{
    // It goes on counting where the open is measured until adopt_open_cost(),
    // so a failed open keeps what it read
    m_stats.mediumId = source->medium_id();
    m_stats.sources.push_back(source.get());
    m_sources.push_back(std::move(source));
}

TByteSource* TAudioDecoder::open_extra_source(const QString& fileName)
{
    TByteSourceResult source = m_opener ? m_opener(fileName) : TByteSource::open_file(fileName);
    if (!source) {
        return nullptr;
    }
    (*source)->count_reads_into(&m_stats.current_cost().reads);
    m_stats.sources.push_back(source->get());
    m_sources.push_back(std::move(*source));
    return m_sources.back().get();
}

TAudioFileStatus TAudioDecoder::has_audio() const
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
    if (!next_read_is_at(first)) {
        if (TAudioFileStatus sought = seek(first); !sought) {
            return nothing(std::move(sought.error()));
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

// The library that can read the file, by its content; the probes' reads count
// where source counts. A match really opens the file: can_decode_audio() has no
// other check, a signature alone proves nothing.
static DecoderLibrary find_decoder(const QString& fileName, TByteSource& source)
{
    // A scenario (a test signal described, TScenarioDecoder) by its name, the
    // one exception to "by content": a test facility, no audio format; one
    // that does not parse is no audio file.
    if (TScenarioDecoder::is_scenario_file(fileName)) {
        return TScenarioDecoder::check(source) ? DecoderLibrary::Scenario : DecoderLibrary::None;
    }

    switch (library_of_signature(source)) {
    case DecoderLibrary::SndFile:
        // A RIFF libsndfile does not know (RMP3) can still hold MP3
        if (const int format = SFAudioReader::format(source)) {
            return library_of_sndfile_format(format);
        }
        return MpgAudioReader::can_decode(source) ? DecoderLibrary::Mpg : DecoderLibrary::None;
    case DecoderLibrary::WavPack:
        return WPAudioReader::can_decode(source) ? DecoderLibrary::WavPack : DecoderLibrary::None;
    case DecoderLibrary::Faad:
#if defined M4A_DECODE_SUPPORT
        return FaadAudioReader::can_decode(source) ? DecoderLibrary::Faad : DecoderLibrary::None;
#else
        return DecoderLibrary::None;
#endif
    default:
        break;
    }

    if (MpgAudioReader::can_decode(source)) {
        return DecoderLibrary::Mpg;
    }
    if (const int format = SFAudioReader::format(source)) {
        return library_of_sndfile_format(format);
    }
    // ADTS and WavPack behind an ID3 tag have no signature, and libsndfile
    // does not read them
    if (WPAudioReader::can_decode(source)) {
        return DecoderLibrary::WavPack;
    }
#if defined M4A_DECODE_SUPPORT
    if (FaadAudioReader::can_decode(source)) {
        return DecoderLibrary::Faad;
    }
#endif
    return DecoderLibrary::None;
}

bool can_decode_audio(const QString& fileName)
{
    TByteSourceResult source = TByteSource::open_file(fileName);
    return source && find_decoder(fileName, **source) != DecoderLibrary::None;
}

// Why no decoder reads the file its source holds. Asked after the probes
// failed, not before.
static TAudioFileError why_no_decoder(const QString& fileName, TByteSource& source)
{
    if (std::optional<QString> reason = source.take_error()) {
        return {TAudioFileError::Code::CannotOpen, *reason};
    }
    if (TScenarioDecoder::is_scenario_file(fileName)) {
        if (std::expected<void, QString> scenario = TScenarioDecoder::check(source); !scenario) {
            return {TAudioFileError::Code::CannotOpen, QStringLiteral("not a scenario: %1").arg(scenario.error())};
        }
    }
    return {TAudioFileError::Code::CannotOpen, QStringLiteral("no decoder reads this format")};
}

using TDecoderResult = std::expected<std::unique_ptr<TAudioDecoder>, TAudioFileError>;

template <typename Decoder>
static TDecoderResult as_decoder(std::expected<std::unique_ptr<Decoder>, TAudioFileError> decoder)
{
    if (!decoder) {
        return std::unexpected(std::move(decoder.error()));
    }
    return std::unique_ptr<TAudioDecoder>(std::move(*decoder));
}

static TDecoderResult open_faad(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
{
#if defined M4A_DECODE_SUPPORT
    return as_decoder(FaadAudioReader::open(fileName, std::move(source), opener));
#else
    return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, QStringLiteral("no decoder reads this format")});
#endif
}

// The decoder for fileName; all the reads of the open, the probes' and the
// decoder's own, count into openStats
static TDecoderResult open_measured(const QString& fileName, const TByteSourceOpener& opener, TAudioIOStats& openStats)
{
    TByteSourceResult source = opener ? opener(fileName) : TByteSource::open_file(fileName);
    if (!source) {
        return std::unexpected(TAudioFileError{source.error().notFound ? TAudioFileError::Code::NotFound : TAudioFileError::Code::CannotOpen, source.error().reason});
    }
    (*source)->count_reads_into(&openStats.open.reads);

    TDecoderResult decoder = std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, QString()});
    switch (find_decoder(fileName, **source)) {
    case DecoderLibrary::Scenario:
        decoder = as_decoder(TScenarioDecoder::open(fileName, std::move(*source), opener));
        break;
    case DecoderLibrary::SndFile:
        decoder = as_decoder(SFAudioReader::open(fileName, std::move(*source), opener));
        break;
    case DecoderLibrary::WavPack:
        decoder = as_decoder(WPAudioReader::open(fileName, std::move(*source), opener));
        break;
    case DecoderLibrary::Faad:
        decoder = open_faad(fileName, std::move(*source), opener);
        break;
    case DecoderLibrary::Mpg:
        decoder = as_decoder(MpgAudioReader::open(fileName, std::move(*source), opener));
        break;
    case DecoderLibrary::None:
        return std::unexpected(why_no_decoder(fileName, **source));
    }

    return decoder;
}

std::expected<std::unique_ptr<TAudioDecoder>, TAudioFileError> open_audio_decoder(const QString& fileName, const TByteSourceOpener& opener)
{
    TAudioIOStats openStats;
    TDecoderResult decoder = [&] {
        TAudioIOScope scope(openStats, TAudioIOStats::Activity::Open);
        return open_measured(fileName, opener, openStats);
    }();
    if (decoder) {
        (*decoder)->adopt_open_cost(openStats.open);
    } else {
        // No decoder keeps the measurement: it goes with the error
        decoder.error().openCost = std::make_shared<const TAudioIOCost>(openStats.open);
    }
    return decoder;
}
