// SPDX-FileCopyrightText: 2007-2026 Ben Levitt, Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TAudioDecoderFactory.h"

#include "TAudioIOScope.h"

#include "SFAudioReader.h"
#include "WPAudioReader.h"
#include "MpgAudioReader.h"
#include "TScenarioDecoder.h"

#if defined M4A_DECODE_SUPPORT
#include "FaadAudioReader.h"
#endif

#include <cstring>

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

// Whether the MP3 probe may look at a file libsndfile refused. mpg123 walks any
// RIFF byte by byte to its data chunk, with no limit: a video would be read
// whole, on the GUI thread for a drag. Only the forms that can hold MP3 go on
static bool riff_may_hold_mp3(TByteSource& source)
{
    char head[12]{};
    source.seek(0);
    source.read(head, sizeof(head));
    if (std::memcmp(head, "RIFF", 4) != 0) {
        return true;
    }
    return std::memcmp(head + 8, "WAVE", 4) == 0 || std::memcmp(head + 8, "RMP3", 4) == 0;
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
        if (riff_may_hold_mp3(source) && MpgAudioReader::can_decode(source)) {
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

bool TAudioDecoderFactory::can_read(const QString& fileName)
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

std::expected<std::unique_ptr<TAudioDecoder>, TAudioFileError> TAudioDecoderFactory::open_file(const QString& fileName, const TByteSourceOpener& opener)
{
    TAudioIOStats openStats;
    TDecoderResult decoder = std::unexpected(TAudioFileError{});
    {
        TAudioIOScope scope(openStats.open);
        decoder = open_measured(fileName, opener, openStats);
    }
    if (decoder) {
        (*decoder)->adopt_open_cost(openStats.open);
    }
    return decoder;
}
