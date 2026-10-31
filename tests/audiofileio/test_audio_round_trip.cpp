// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless round trip of every writer and reader (task T44): every writer x
// data format x 1, 2 and 6 channels written through TAudioWriter and read
// back through TAudioReader. The source is a scenario (TScenarioDecoder):
// for the lossless formats a ramp in channel 0 and sweeps in the others,
// for the lossy ones sweeps only (a codec drops a ramp's DC); every channel
// has its own amplitude, so a swapped or silent channel shows.
//
// Expected:
// - float: exact;
// - 8, 16 and 24 bit: within 2 LSB of the format (triangular dither, at
//   most 1 LSB, and the rounding);
// - 32 bit integer: within float's resolution (the samples are floats on
//   both sides, so 1 LSB of 32 bit is not reachable);
// - lossy (Ogg, MP3, AAC): each channel's level, where it lies (the lag at
//   which it matches the source best) and the channel order (each channel
//   matches its own source channel best);
// - every file: the frame count (lossy: as close as the format says).
// A combination the format refuses (libsndfile's rules, FAAC's 16 bit) is
// reported as refused with the writer's reason; whether a refusal is right
// is the list at the end of the run (task T49).
// A failure known when this test came (the same before and after W1 of the
// API work order) has its task in known(): it prints KNOWN, and once it
// passes it fails, so the entry goes with the fix.

#include "TAudioReader.h"
#include "TAudioWriter.h"
#include "TAudioBuffer.h"

#include <QFile>
#include <QTemporaryDir>

#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <tuple>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

// A check of a combination with a known failure: KNOWN while it fails, a
// failure once it passes (the defect is gone, its entry has to go too)
static void check_known(bool ok, const char* what, const char* knownWhy)
{
    if (!knownWhy) {
        check(ok, what);
    } else if (!ok) {
        std::printf("%s: KNOWN (%s)\n", what, knownWhy);
    } else {
        std::printf("%s: passes now, the KNOWN entry (%s) is out of date: remove it\n", what, knownWhy);
        ++failures;
    }
}

static constexpr uint Rate = 48000;
static constexpr nframes_t Frames = 2 * Rate + 1037;
static constexpr nframes_t Block = 4096;

using Channels = std::vector<std::vector<float>>;

static QString write_scenario(const QTemporaryDir& dir, uint channels, bool ramp)
{
    QString channelList;
    for (uint c = 0; c < channels; ++c) {
        const double amplitude = 0.2 + 0.05 * c;
        if (c == 0 && ramp) {
            channelList += QStringLiteral(R"({"signal": "ramp"})");
        } else if (c % 2 == 0) {
            channelList += QStringLiteral(R"({"signal": "sweep", "mode": "exponential", "start": %1, "end": %2, "amplitude": %3})")
                           .arg(100 + 50 * c).arg(8000 + 500 * c).arg(amplitude);
        } else {
            channelList += QStringLiteral(R"({"signal": "sweep", "mode": "linear", "start": %1, "end": %2, "amplitude": %3})")
                           .arg(5000 - 300 * c).arg(300 + 40 * c).arg(amplitude);
        }
        if (c + 1 < channels) {
            channelList += QLatin1Char(',');
        }
    }
    const QString path = dir.filePath(QStringLiteral("source-%1-%2.tscenario").arg(channels).arg(ramp ? "ramp" : "sweeps"));
    QFile file(path);
    const QByteArray json = QStringLiteral(R"({"version": 1, "rate": %1, "frames": %2, "channels": [%3]})")
                                .arg(Rate).arg(Frames).arg(channelList).toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size()) {
        return QString();
    }
    return path;
}

// At most this many frames are read back: what was written and a margin. A
// file that reports more (a header that says 2^32 - 1, WavPack's unknown
// length) is a failure of the frame count check, not a buffer of gigabytes:
// on 2026-10-28 such a file made this test take 13 GB and the kernel kill it
static constexpr nframes_t ReadLimit = Frames + 16 * Block;

// The frames of a reader at the file's rate, per channel, at most ReadLimit
static Channels read_all(TAudioReader& reader)
{
    const uint channels = reader.channel_count();
    const nframes_t frames = std::min(reader.file_frame_count(), ReadLimit);
    Channels samples(channels, std::vector<float>(frames));
    TAudioBuffer buffer;
    for (nframes_t at = 0; at < frames; at += Block) {
        const nframes_t count = std::min(Block, frames - at);
        std::ignore = reader.read_file_frames(buffer, at, count);
        for (uint c = 0; c < channels; ++c) {
            const float* data = buffer.get_channel_buffer(c).get_data(count);
            std::copy(data, data + count, samples[c].begin() + at);
        }
    }
    return samples;
}

static double rms(const std::vector<float>& samples, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i) {
        sum += double(samples[i]) * samples[i];
    }
    return std::sqrt(sum / double(to - from));
}

// The lag (b against a, within search frames) at which length frames from
// frame from match best; and the mean square difference there
static long best_lag(const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t length, long search, double* error = nullptr)
{
    long bestLag = 0;
    double best = -1.0;
    for (long lag = -search; lag <= search; ++lag) {
        const long start = long(from) + lag;
        if (start < 0 || size_t(start) + length > b.size() || from + length > a.size()) {
            continue;
        }
        double sum = 0.0;
        for (size_t i = 0; i < length; ++i) {
            const double d = double(b[size_t(start) + i]) - a[from + i];
            sum += d * d;
        }
        if (best < 0.0 || sum < best) {
            best = sum;
            bestLag = lag;
        }
    }
    if (error) {
        *error = best / double(length);
    }
    return bestLag;
}

struct Format
{
    const char* name;
    const char* suffix;
    TraversoDAW::WriterType writer;
    TraversoDAW::FileFormat file;
    bool lossy;
    // How far the frame count of a lossy file may be off (the codec's frame)
    long frameTolerance;
};

static const char* data_name(TraversoDAW::DataFormat data)
{
    switch (data) {
    case TraversoDAW::DataFormat::FLOAT: return "float";
    case TraversoDAW::DataFormat::PCM_S8: return "8 bit";
    case TraversoDAW::DataFormat::PCM_16: return "16 bit";
    case TraversoDAW::DataFormat::PCM_24: return "24 bit";
    case TraversoDAW::DataFormat::PCM_32: return "32 bit";
    }
    return "?";
}

// The largest difference a sample of this data format may have after the
// round trip
static float tolerance(TraversoDAW::DataFormat data)
{
    switch (data) {
    case TraversoDAW::DataFormat::FLOAT: return 0.0f;
    case TraversoDAW::DataFormat::PCM_S8: return 2.0f / 128.0f;
    case TraversoDAW::DataFormat::PCM_16: return 2.0f / 32768.0f;
    case TraversoDAW::DataFormat::PCM_24: return 2.0f / 8388608.0f;
    case TraversoDAW::DataFormat::PCM_32: return 1.0e-7f;
    }
    return 0.0f;
}

// The failures known when this test came, measured 2026-10-28, each with its
// task. checkName: "frames" (channels, rate and frame count), "samples",
// "levels", "placement", "order".
static const char* known(const Format& format, TraversoDAW::DataFormat data, uint channels, const char* checkName)
{
    const auto is = [checkName](const char* name) { return std::strcmp(checkName, name) == 0; };
    const bool bits8 = data == TraversoDAW::DataFormat::PCM_S8;
    if (format.file == TraversoDAW::FileFormat::FLAC && bits8 && is("frames")) {
        return "T46: the file reports 2^32 - 1 frames";
    }
    if (format.file == TraversoDAW::FileFormat::AIFF && bits8 && (is("samples") || (channels == 1 && is("frames")))) {
        return "T47: samples off by up to 1.0, mono one frame more";
    }
    if (format.writer == TraversoDAW::WriterType::M4A) {
        if (channels == 1 && is("frames")) {
            return "T48: mono reads back as 2 channels";
        }
        // The level of 16 bit stereo comes out right
        const bool levelRight = channels == 2 && data == TraversoDAW::DataFormat::PCM_16;
        if (channels > 1 && (is("placement") || is("order") || (is("levels") && !levelRight))) {
            return "T48: channels mixed up and shifted";
        }
    }
    return nullptr;
}

static std::vector<QString> s_refused;

static void round_trip(const QTemporaryDir& dir, const Format& format, TraversoDAW::DataFormat data, uint channels, const QString& sourcePath)
{
    char what[240];
    const std::unique_ptr<TAudioReader> source = TAudioReader::open(sourcePath).value_or(nullptr);
    if (!source) {
        check(false, "the scenario source opens");
        return;
    }
    const Channels reference = read_all(*source);

    TAudioEncodeSettings settings;
    settings.writerType = format.writer;
    settings.fileFormat = format.file;
    settings.dataFormat = data;
    settings.channelCount = channels;
    settings.sampleRate = Rate;
    settings.ditherType = GDitherTri;

    const QString path = dir.filePath(QStringLiteral("rt-%1-%2-%3.%4").arg(int(format.file)).arg(int(data)).arg(channels).arg(QLatin1String(format.suffix)));
    auto writer = TAudioWriter::open(settings, path, Rate);
    if (!writer) {
        const QString line = QStringLiteral("%1 %2 %3 ch: %4").arg(QLatin1String(format.name), QLatin1String(data_name(data))).arg(channels).arg(writer.error().detail);
        std::printf("%s: refused at open\n", qPrintable(line));
        s_refused.push_back(line);
        return;
    }
    TAudioBuffer buffer;
    bool written = true;
    for (nframes_t at = 0; at < Frames; at += Block) {
        const nframes_t count = std::min(Block, Frames - at);
        buffer.check_capacity(count, channels);
        for (uint c = 0; c < channels; ++c) {
            std::copy_n(reference[c].begin() + at, count, buffer.get_channel_buffer(c).get_data(count));
        }
        const TAudioFileResult result = (*writer)->write(buffer, count);
        written = written && result.frames == count && !result.error;
    }
    const bool closed = (*writer)->close().has_value();
    std::snprintf(what, sizeof(what), "%s %s %u ch: written and closed", format.name, data_name(data), channels);
    check(written && closed, what);
    if (!written || !closed) {
        return;
    }

    const auto back = TAudioReader::open(path);
    std::snprintf(what, sizeof(what), "%s %s %u ch: reads back (%s)", format.name, data_name(data), channels,
                  back ? qPrintable((*back)->decoder_type()) : qPrintable(back.error().detail));
    check(back.has_value(), what);
    if (!back) {
        return;
    }
    TAudioReader& reader = **back;
    const long frameError = long(reader.file_frame_count()) - long(Frames);
    std::snprintf(what, sizeof(what), "%s %s %u ch: %u channels at %u Hz, %u frames (%+ld)", format.name, data_name(data), channels,
                  reader.channel_count(), reader.file_rate(), reader.file_frame_count(), frameError);
    const bool framesRight = reader.channel_count() == channels && reader.file_rate() == Rate && std::labs(frameError) <= format.frameTolerance;
    check_known(framesRight, what, known(format, data, channels, "frames"));
    // A file of other channels or another length is not compared further:
    // the comparisons below read the frames that were written
    if (reader.channel_count() != channels || reader.file_frame_count() < Frames || reader.file_frame_count() > ReadLimit) {
        return;
    }
    const Channels samples = read_all(reader);

    if (!format.lossy) {
        float worst = 0.0f;
        uint worstChannel = 0;
        for (uint c = 0; c < channels; ++c) {
            for (nframes_t f = 0; f < Frames; ++f) {
                const float d = std::fabs(samples[c][f] - reference[c][f]);
                if (d > worst) {
                    worst = d;
                    worstChannel = c;
                }
            }
        }
        std::snprintf(what, sizeof(what), "%s %s %u ch: every sample as written (worst %.3g in channel %u, allowed %.3g)", format.name,
                      data_name(data), channels, double(worst), worstChannel, double(tolerance(data)));
        check_known(worst <= tolerance(data), what, known(format, data, channels, "samples"));
        return;
    }

    // Lossy: per channel its level, where it lies, and that it is its own
    const size_t from = Frames / 3;
    const size_t length = 4096;
    bool levels = true;
    bool placed = true;
    bool own = true;
    long lags[8] = {};
    for (uint c = 0; c < channels; ++c) {
        const double want = rms(reference[c], Frames / 4, Frames * 3 / 4);
        const double got = rms(samples[c], Frames / 4, Frames * 3 / 4);
        levels = levels && std::fabs(got - want) < 0.05 * std::max(want, 0.1);
        lags[c] = best_lag(reference[c], samples[c], from, length, 3000);
        placed = placed && lags[c] == 0;
        // Its own source channel matches it better than any other
        double ownError = 0.0;
        best_lag(reference[c], samples[c], from, length, 0, &ownError);
        for (uint other = 0; other < channels; ++other) {
            if (other == c) {
                continue;
            }
            double otherError = 0.0;
            best_lag(reference[other], samples[c], from, length, 0, &otherError);
            own = own && ownError < otherError;
        }
    }
    std::snprintf(what, sizeof(what), "%s %s %u ch: each channel's level", format.name, data_name(data), channels);
    check_known(levels, what, known(format, data, channels, "levels"));
    std::snprintf(what, sizeof(what), "%s %s %u ch: each channel where it was written (lag of channel 0: %+ld)", format.name, data_name(data),
                  channels, lags[0]);
    check_known(placed, what, known(format, data, channels, "placement"));
    std::snprintf(what, sizeof(what), "%s %s %u ch: each channel is its own (the channel order)", format.name, data_name(data), channels);
    check_known(own, what, known(format, data, channels, "order"));
}

int main()
{
    // The test's memory capped, so whatever a file claims, the test fails and
    // nothing else on the machine does. Not under a sanitizer (address or
    // thread): they reserve terabytes of address space for their shadow
    // memory. GCC says so with __SANITIZE_*__, Clang with __has_feature.
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define ROUND_TRIP_SANITIZER
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define ROUND_TRIP_SANITIZER
#endif
#if defined(Q_OS_UNIX) && !defined(ROUND_TRIP_SANITIZER)
    const rlimit limit{rlim_t(4) << 30, rlim_t(4) << 30};
    setrlimit(RLIMIT_AS, &limit);
#endif

    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }

    using TraversoDAW::DataFormat;
    using TraversoDAW::FileFormat;
    using TraversoDAW::WriterType;
    const std::vector<Format> formats = {
        {"WAV", "wav", WriterType::SNDFILE, FileFormat::WAV, false, 0},
        {"AIFF", "aiff", WriterType::SNDFILE, FileFormat::AIFF, false, 0},
        {"W64", "w64", WriterType::SNDFILE, FileFormat::W64, false, 0},
        {"FLAC", "flac", WriterType::SNDFILE, FileFormat::FLAC, false, 0},
        {"WavPack", "wv", WriterType::WAVPACK, FileFormat::WAVPACK, false, 0},
        {"Ogg Vorbis", "ogg", WriterType::SNDFILE, FileFormat::OGG, true, 0},
        {"MP3", "mp3", WriterType::SNDFILE, FileFormat::MP3, true, 0},
#if defined(M4A_ENCODE_SUPPORT) && defined(M4A_DECODE_SUPPORT)
        {"AAC (ADTS)", "aac", WriterType::M4A, FileFormat::UNKNOWN, true, 2048},
#endif
    };
    const DataFormat datas[] = {DataFormat::FLOAT, DataFormat::PCM_S8, DataFormat::PCM_16, DataFormat::PCM_24, DataFormat::PCM_32};

    for (const uint channels : {1u, 2u, 6u}) {
        const QString lossless = write_scenario(dir, channels, true);
        const QString lossy = write_scenario(dir, channels, false);
        for (const Format& format : formats) {
            for (const DataFormat data : datas) {
                round_trip(dir, format, data, channels, format.lossy ? lossy : lossless);
            }
        }
    }

    std::printf("\n%zu combination(s) refused at open:\n", s_refused.size());
    for (const QString& line : s_refused) {
        std::printf("  %s\n", qPrintable(line));
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all round-trip checks passed\n");
    return 0;
}
