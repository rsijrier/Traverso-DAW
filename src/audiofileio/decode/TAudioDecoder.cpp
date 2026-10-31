// SPDX-FileCopyrightText: 2007-2026 Ben Levitt, Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TAudioDecoder.h"

#include "TAudioBuffer.h"
#include "TClock.h"

#include "SFAudioReader.h"
#include "WPAudioReader.h"
#include "MpgAudioReader.h"
#include "TScenarioDecoder.h"

#if defined M4A_DECODE_SUPPORT
#include "FaadAudioReader.h"
#endif

#include <cstring>

/**
 * @brief Measures one seek, read or open: when it ends, also by an early return
 * or an error, the time and the call go to the cost. For a decoder it sets the
 * activity at the start, so its sources count there, and puts the one before it
 * back. An open has no decoder yet: its sources were pointed at the cost once.
 */
class TAudioIOScope
{
public:
    explicit TAudioIOScope(TAudioIOCost& cost)
        : m_cost(cost)
        , m_start(TClock::now_nanoseconds())
    {
    }

    TAudioIOScope(TAudioDecoder& decoder, TAudioIOStats::Activity activity)
        : m_cost(decoder.m_stats.cost_of(activity))
        , m_decoder(&decoder)
        , m_previous(decoder.m_stats.activity)
        , m_start(TClock::now_nanoseconds())
    {
        decoder.set_activity(activity);
    }

    ~TAudioIOScope()
    {
        m_cost.nanoseconds += TClock::now_nanoseconds() - m_start;
        ++m_cost.calls;
        if (m_decoder) {
            m_decoder->set_activity(m_previous);
        }
    }

    TAudioIOScope(const TAudioIOScope&) = delete;
    TAudioIOScope& operator=(const TAudioIOScope&) = delete;

private:
    TAudioIOCost& m_cost;
    TAudioDecoder* m_decoder{nullptr};
    TAudioIOStats::Activity m_previous{};
    uint64_t m_start;
};

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
        return nullptr;
    }
    (*source)->count_reads_into(&m_stats.current_cost().reads);
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
    {"# ts", 0, DecoderLibrary::Scenario},
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

static std::optional<DecoderLibrary> library_of_signature(TByteSource& source)
{
    char head[8]{};
    source.seek(0);
    source.read(head, sizeof(head));
    for (const DecoderSignature& signature : DecoderSignatures) {
        if (std::memcmp(head + signature.offset, signature.bytes, 4) == 0) {
            return signature.library;
        }
    }
    return std::nullopt;
}

// What libsndfile found: MP3 in a WAV goes to mpg123, which reads it whole where
// libsndfile takes the length from the file size
static DecoderLibrary library_of_sndfile_format(int format)
{
    const bool mp3InWav = (format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV && (format & SF_FORMAT_SUBMASK) == SF_FORMAT_MPEG_LAYER_III;
    return mp3InWav ? DecoderLibrary::Mpg : DecoderLibrary::SndFile;
}

// Why no decoder reads the file its source holds: a failed read goes first
static TAudioFileError why_no_decoder(TByteSource& source, const QString& detail)
{
    return {TAudioFileError::Code::CannotOpen, source.take_error().value_or(detail)};
}

// The library for the file, by its content, or why there is none; the probes'
// reads count where source counts. With a signature that library's open decides
// and says why not; libsndfile's route looks first, for MP3 in a WAV.
static std::expected<DecoderLibrary, TAudioFileError> find_decoder(TByteSource& source)
{
    const std::optional<DecoderLibrary> signature = library_of_signature(source);
    if (signature == DecoderLibrary::SndFile) {
        const std::expected<int, QString> format = SFAudioReader::format(source);
        if (format) {
            return library_of_sndfile_format(*format);
        }
        // A RIFF libsndfile does not know (RMP3) can still hold MP3
        if (MpgAudioReader::can_decode(source)) {
            return DecoderLibrary::Mpg;
        }
        return std::unexpected(why_no_decoder(source, format.error()));
    }
    if (signature) {
        return *signature;
    }

    const std::expected<void, QString> mpeg = MpgAudioReader::can_decode(source);
    if (mpeg) {
        return DecoderLibrary::Mpg;
    }
    // Only when it looked like MPEG audio: the other probes' refusals are noise
    const QString none = mpeg.error().isEmpty() ? QStringLiteral("no decoder reads this format") : QStringLiteral("no decoder reads this format (%1)").arg(mpeg.error());

    // libsndfile's MPEG goes nowhere: a file the strict probe refused
    if (const std::expected<int, QString> format = SFAudioReader::format(source)) {
        if ((*format & SF_FORMAT_TYPEMASK) == SF_FORMAT_MPEG) {
            return std::unexpected(why_no_decoder(source, none));
        }
        return library_of_sndfile_format(*format);
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
    return std::unexpected(why_no_decoder(source, none));
}

bool can_decode_audio(const QString& fileName)
{
    TByteSourceResult source = TByteSource::open_file(fileName);
    if (!source) {
        return false;
    }
    const std::expected<DecoderLibrary, TAudioFileError> library = find_decoder(**source);
    if (!library) {
        return false;
    }
    // A signature alone proves nothing, and opening is too much for a probe (an
    // MP3 scans the whole file). libsndfile and the MP3 probe looked already
    switch (*library) {
    case DecoderLibrary::Scenario:
        return TScenarioDecoder::check(**source).has_value();
    case DecoderLibrary::WavPack:
        return WPAudioReader::can_decode(**source);
    case DecoderLibrary::Faad:
#if defined M4A_DECODE_SUPPORT
        return FaadAudioReader::can_decode(**source);
#else
        return false;
#endif
    case DecoderLibrary::SndFile:
    case DecoderLibrary::Mpg:
        return true;
    }
    Q_UNREACHABLE();
}

using TDecoderResult = std::expected<std::unique_ptr<TAudioDecoder>, TAudioFileError>;

static TDecoderResult open_faad(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
{
#if defined M4A_DECODE_SUPPORT
    return FaadAudioReader::open(fileName, std::move(source), opener);
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

    const std::expected<DecoderLibrary, TAudioFileError> library = find_decoder(**source);
    if (!library) {
        return std::unexpected(library.error());
    }
    switch (*library) {
    case DecoderLibrary::Scenario:
        return TScenarioDecoder::open(fileName, std::move(*source), opener);
    case DecoderLibrary::SndFile:
        return SFAudioReader::open(fileName, std::move(*source), opener);
    case DecoderLibrary::WavPack:
        return WPAudioReader::open(fileName, std::move(*source), opener);
    case DecoderLibrary::Faad:
        return open_faad(fileName, std::move(*source), opener);
    case DecoderLibrary::Mpg:
        return MpgAudioReader::open(fileName, std::move(*source), opener);
    }
    Q_UNREACHABLE();
}

std::expected<std::unique_ptr<TAudioDecoder>, TAudioFileError> open_audio_decoder(const QString& fileName, const TByteSourceOpener& opener)
{
    TAudioIOStats openStats;
    TDecoderResult decoder = [&] {
        TAudioIOScope scope(openStats.open);
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
