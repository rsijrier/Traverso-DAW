// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the audio decoders, through TAudioReader at the file's own
// rate (read_file_frames()): every decoder reads a file of every format it
// serves with the file's channels, rate and frames, and with the
// audio where it was written; a read after a seek gives what a read from the
// start gives at that place; a read that crosses the end of the file gives
// what is left and zeros after it, whatever the buffer held before. A file
// cut short while it is open reads short before its end, its frames until
// then, zeros and 0 after (the error state of TAudioDecoder's contract).
//
// The files are made here: a scenario (the signal described, no samples),
// WAV, AIFF, W64, RF64, CAF, FLAC, Ogg Vorbis, Opus and MP3 with libsndfile, WavPack and AAC (ADTS) with
// Traverso's own writers, and when ffmpeg is installed AAC (ADTS and M4A)
// and an MP3 without gapless information with ffmpeg. Each channel holds a sweep of its own, so channels that are
// swapped or silent show, and audio that is shifted shows too (a sweep
// matches itself at one place only).

#include "TAudioReader.h"
#include "TAudioWriter.h"
#include "TAudioBuffer.h"
#include "test_signals.h"

#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <vector>
#include <tuple>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

constexpr float Garbage = 0.7f;

// After a seek mpg123's synthesis phase differs from a read from the start, so
// the last bits do (up to 3e-7 at peak 1.13); the position is exact
constexpr float MpgSeekTolerance = 1e-6f;

// The audio of a test file: an odd length (no codec block ends there),
// channel 0 sweeping up from 200 Hz (exponentially) at 0.5, channel 1 down
// from 3 kHz to 300 Hz (linearly) at 0.3
struct Source
{
    uint rate{44100};
    uint channels{2};
    // Longer, for the cut file: more than a decoder keeps in its buffers
    uint extraSeconds{0};

    nframes_t frames() const { return rate * (1 + extraSeconds) + 1037; }

    float sample(nframes_t frame, uint channel) const
    {
        if (channel == 0) {
            return TestSignals::sweep_exponential(frame, rate, frames(), 200.0, std::min(6000.0, 0.4 * rate), 0.5);
        }
        return TestSignals::sweep_linear(frame, rate, frames(), 3000.0, 300.0, 0.3);
    }

    double level(uint channel) const { return (channel == 0 ? 0.5 : 0.3) / M_SQRT2; }

    std::vector<float> channel(uint channel) const
    {
        std::vector<float> samples(frames());
        for (nframes_t frame = 0; frame < frames(); ++frame) {
            samples[frame] = sample(frame, channel);
        }
        return samples;
    }

    std::vector<float> interleaved() const
    {
        std::vector<float> samples(size_t(frames()) * channels);
        for (nframes_t frame = 0; frame < frames(); ++frame) {
            for (uint c = 0; c < channels; ++c) {
                samples[size_t(frame) * channels + c] = sample(frame, c);
            }
        }
        return samples;
    }
};

static bool write_with_sndfile(const QString& path, const Source& source, int format)
{
    SF_INFO info{};
    info.samplerate = int(source.rate);
    info.channels = int(source.channels);
    info.format = format;
    SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
    if (!file) {
        return false;
    }
    const std::vector<float> samples = source.interleaved();
    const sf_count_t written = sf_writef_float(file, samples.data(), source.frames());
    sf_close(file);
    return written == sf_count_t(source.frames());
}

static bool write_with_traverso(const QString& path, const Source& source, TraversoDAW::WriterType writer, TraversoDAW::FileFormat format)
{
    TAudioEncodeSettings settings;
    settings.writerType = writer;
    settings.fileFormat = format;
    // AAC: 16 bit (dithered there by TAudioWriter), as FaacAudioWriter takes it
    const bool aac = writer == TraversoDAW::WriterType::M4A;
    settings.dataFormat = aac ? TraversoDAW::DataFormat::PCM_16 : TraversoDAW::DataFormat::FLOAT;
    settings.channelCount = source.channels;
    settings.sampleRate = source.rate;
    const std::unique_ptr<TAudioWriter> file = TAudioWriter::open(settings, path, source.rate).value_or(nullptr);
    if (!file) {
        return false;
    }
    TAudioBuffer buffer;
    buffer.check_capacity(source.frames(), source.channels);
    for (uint channel = 0; channel < source.channels; ++channel) {
        const std::vector<float> samples = source.channel(channel);
        std::copy(samples.begin(), samples.end(), buffer.get_channel_buffer(channel).get_data(source.frames()));
    }
    const nframes_t written = file->write(buffer, source.frames()).frames;
    return file->close() && written == source.frames();
}

// A scenario (TScenarioDecoder) that describes the source: its two sweeps
static bool write_scenario(const QString& path, const Source& source)
{
    if (source.channels != 2) {
        return false;
    }
    const QString json = QStringLiteral(R"({"version": 1, "rate": %1, "frames": %2, "channels": [
        {"signal": "sweep", "mode": "exponential", "start": 200, "end": %3, "amplitude": 0.5},
        {"signal": "sweep", "mode": "linear", "start": 3000, "end": 300, "amplitude": 0.3}]})")
                             .arg(source.rate)
                             .arg(source.frames())
                             .arg(std::min(6000.0, 0.4 * source.rate), 0, 'g', 17);
    QFile file(path);
    const QByteArray bytes = json.toUtf8();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// With ffmpeg, from a 16 bit WAV of the source; false when ffmpeg is not
// installed
static bool write_with_ffmpeg(const QString& path, const Source& source, const QStringList& encode)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString wav = path + QStringLiteral(".source.wav");
    if (ffmpeg.isEmpty() || !write_with_sndfile(wav, source, SF_FORMAT_WAV | SF_FORMAT_PCM_16)) {
        return false;
    }
    QStringList arguments{QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-i"), wav};
    arguments << encode << path;
    return QProcess::execute(ffmpeg, arguments) == 0;
}

// An MP3 (libsndfile's, with a LAME tag) whose tag says padding samples at
// the end. Below the decoder's own delay of 529, which no encoder writes
// (LAME and ffmpeg pad at least that), the tag promises frames past what the
// decoder gives: the decoder must say no more frames than it gives.
static bool write_mp3_with_padding(const QString& path, const Source& source, int padding)
{
    if (!write_with_sndfile(path, source, SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III)) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite)) {
        return false;
    }
    QByteArray head = file.read(4096);
    // The LAME extension: the encoder's name, then 21 bytes in the delay and
    // the padding, 12 bits each
    const qsizetype lame = head.indexOf("LAME");
    if (lame < 0 || lame + 24 > head.size()) {
        return false;
    }
    // The tag's CRC (its last two bytes) is not made again: Traverso's
    // parser does not check it (mpg123 does)
    head[lame + 22] = char((uchar(head[lame + 22]) & 0xf0) | ((padding >> 8) & 0x0f));
    head[lame + 23] = char(padding & 0xff);
    return file.seek(0) && file.write(head) == head.size();
}

// How far b is shifted against a around their frames aFrom and bFrom: the lag
// within search frames either way at which length frames match best (b
// later than a: a positive lag)
static long best_lag(const std::vector<float>& a, size_t aFrom, const std::vector<float>& b, size_t bFrom, size_t length, long search)
{
    long bestLag = 0;
    double bestError = -1.0;
    for (long lag = -search; lag <= search; ++lag) {
        const long from = long(bFrom) + lag;
        if (from < 0 || size_t(from) + length > b.size() || aFrom + length > a.size()) {
            continue;
        }
        double error = 0.0;
        for (size_t i = 0; i < length; ++i) {
            const double d = double(b[size_t(from) + i]) - a[aFrom + i];
            error += d * d;
        }
        if (bestError < 0.0 || error < bestError) {
            bestError = error;
            bestLag = lag;
        }
    }
    return bestLag;
}

struct Format
{
    const char* name;
    const char* suffix;
    Source source;
    // How the file is made; false: the format cannot be written here
    std::function<bool(const QString&, const Source&)> write;
    // Lossless: the samples as written (within the format's resolution);
    // lossy: each channel's level, and the audio where it was written
    bool lossless;
    float resolution;
    // How far the frames may be off what was written: 0 where the format
    // says (MP3 with a LAME tag, Ogg, M4A), an AAC frame for ADTS, which does
    // not say where the audio starts and ends
    long frameTolerance = 0;
    // Whether the file says where the audio starts (an MP3 without a LAME
    // tag does not: the encoder's delay stays in)
    bool placement = true;
    // How far a read after a seek may differ from the read from the start:
    // nothing, bit for bit, but for MP3 (MpgSeekTolerance) and for AAC with
    // noise substitution (PNS, on in ffmpeg's encoder): FAAD fills those bands from a random generator,
    // which a seek starts anew (with -aac_pns 0 those seeks are bit exact
    // too, measured 2026-10-25)
    float seekTolerance = 0.0f;
    // A defect known of the writer of this file, and only that (review of
    // 2026-10-24): its audio lies knownLag frames off where it was written.
    // The placement check takes exactly that lag as known, and fails once
    // the audio is in place (the entry is out of date then); every other
    // check counts, as for any file.
    long knownLag = 0;
    const char* knownWhy = nullptr;
};

static void fill_with_garbage(TAudioBuffer& buffer, nframes_t count, uint channels)
{
    buffer.check_capacity(count, channels);
    for (uint channel = 0; channel < channels; ++channel) {
        std::fill_n(buffer.get_channel_buffer(channel).get_data(count), count, Garbage);
    }
}

static double rms(const std::vector<float>& samples, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i) {
        sum += double(samples[i]) * samples[i];
    }
    return std::sqrt(sum / double(to - from));
}

static void test_format(const QTemporaryDir& dir, const Format& format)
{
    char what[200];
    const Source& source = format.source;
    const uint channels = source.channels;
    const QString path = dir.filePath(QStringLiteral("sweeps.%1").arg(QLatin1String(format.suffix)));
    if (!format.write(path, source)) {
        std::printf("%s: cannot be written here, skipped\n", format.name);
        return;
    }
    const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
    std::snprintf(what, sizeof(what), "%s: opens (%s)", format.name, reader ? qPrintable(reader->decoder_type()) : "no decoder");
    check(reader != nullptr, what);
    if (!reader) {
        return;
    }
    TAudioReader* decoder = reader.get();

    const nframes_t frames = decoder->file_frame_count();
    const long frameError = long(frames) - long(source.frames());
    std::snprintf(what, sizeof(what), "%s: %u channels at %u Hz, %u frames (%+ld)", format.name, decoder->channel_count(), decoder->file_rate(),
                  frames, frameError);
    check(decoder->channel_count() == channels && decoder->file_rate() == source.rate && std::labs(frameError) <= format.frameTolerance, what);
    if (decoder->channel_count() != channels || frames == 0) {
        return;
    }

    // From the start in reads of 1000 frames
    std::vector<std::vector<float>> sequential(channels);
    TAudioBuffer buffer;
    nframes_t total = 0;
    for (nframes_t at = 0; at < frames; at += 1000) {
        const nframes_t count = std::min<nframes_t>(1000, frames - at);
        fill_with_garbage(buffer, count, channels);
        const nframes_t read = decoder->read_file_frames(buffer, at, count).frames;
        total += read;
        for (uint channel = 0; channel < channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(count);
            sequential[channel].insert(sequential[channel].end(), data, data + read);
        }
        if (read != count) {
            break;
        }
    }
    std::snprintf(what, sizeof(what), "%s: read from the start, %u of %u frames", format.name, total, frames);
    check(total == frames, what);
    if (total < 3 * 4096) {
        return;
    }

    if (format.lossless) {
        float worst = 0.0f;
        for (uint channel = 0; channel < channels; ++channel) {
            for (nframes_t frame = 0; frame < total; ++frame) {
                worst = std::max(worst, std::fabs(sequential[channel][frame] - source.sample(frame, channel)));
            }
        }
        std::snprintf(what, sizeof(what), "%s: the samples as written (worst %.2g)", format.name, double(worst));
        check(worst <= format.resolution, what);
    } else {
        // The level of each sweep in the middle of the file (codec delay and
        // edges left out)
        for (uint channel = 0; channel < channels; ++channel) {
            const double level = rms(sequential[channel], total / 4, total * 3 / 4);
            std::snprintf(what, sizeof(what), "%s: channel %u its own sweep (level %.3f of %.3f)", format.name, channel, level, source.level(channel));
            check(std::fabs(level - source.level(channel)) < 0.05, what);
        }

        // Where it was written: the sweep of each channel matches itself at
        // the same frame (an encoder's delay is the decoder's to take out)
        for (uint channel = 0; format.placement && channel < channels; ++channel) {
            const size_t from = total / 3;
            const long lag = best_lag(source.channel(channel), from, sequential[channel], from, 4096, 3000);
            std::snprintf(what, sizeof(what), "%s: channel %u where it was written (%+ld frames)", format.name, channel, lag);
            if (format.knownWhy && lag == format.knownLag) {
                std::printf("%s: KNOWN (%s)\n", what, format.knownWhy);
            } else if (format.knownWhy && lag == 0) {
                // The defect is gone: its entry has to go too
                char fixed[240];
                std::snprintf(fixed, sizeof(fixed), "%s: in place now, the KNOWN entry (%s) is out of date: remove it", what, format.knownWhy);
                check(false, fixed);
            } else {
                check(lag == 0, what);
            }
        }
    }

    // A read after a seek gives what the read from the start gave there:
    // forwards, backwards, near the start, and into the last frames
    const nframes_t seekCount = 2000;
    const float tolerance = format.seekTolerance;
    for (const nframes_t first : {total * 2 / 3, total / 8, nframes_t(1), total / 4 + 345, total - 1500}) {
        fill_with_garbage(buffer, seekCount, channels);
        const nframes_t read = decoder->read_file_frames(buffer, first, seekCount).frames;
        const nframes_t expected = std::min(seekCount, total - first);
        float worst = 0.0f;
        for (uint channel = 0; channel < channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(seekCount);
            for (nframes_t i = 0; i < std::min(read, expected); ++i) {
                worst = std::max(worst, std::fabs(data[i] - sequential[channel][first + i]));
            }
        }
        // How far off a seek that missed landed
        long lag = 0;
        if (worst > tolerance && read > 1000) {
            const float* data = buffer.get_channel_buffer(0).get_data(seekCount);
            lag = best_lag(sequential[0], first, std::vector<float>(data, data + read), 0, 512, 3000);
        }
        std::snprintf(what, sizeof(what), "%s: a seek to frame %u reads what was there (%u of %u frames, worst %.2g, %+ld frames)", format.name,
                      first, read, expected, double(worst), lag);
        check(read == expected && worst <= tolerance, what);
    }

    // Across the end of the file: what is left, zeros after it
    {
        const nframes_t count = 500;
        const nframes_t first = total - 100;
        fill_with_garbage(buffer, count, channels);
        const nframes_t read = decoder->read_file_frames(buffer, first, count).frames;
        bool zeros = true;
        for (uint channel = 0; channel < channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(count);
            for (nframes_t i = read; i < count; ++i) {
                zeros = zeros && data[i] == 0.0f;
            }
        }
        std::snprintf(what, sizeof(what), "%s: a read across the end gives the last %u frames and zeros after them", format.name, read);
        check(read == 100 && zeros, what);
    }

    // Past the end: nothing to read, only zeros (no stale audio), whether
    // the decoder is at the end already or is asked for a frame past it
    for (const bool seekPastEnd : {false, true}) {
        const nframes_t count = 500;
        fill_with_garbage(buffer, count, channels);
        const nframes_t read = decoder->read_file_frames(buffer, seekPastEnd ? frames + 10 : frames, count).frames;
        bool zeros = true;
        for (uint channel = 0; channel < channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(count);
            for (nframes_t i = 0; i < count; ++i) {
                zeros = zeros && data[i] == 0.0f;
            }
        }
        std::snprintf(what, sizeof(what), "%s: a read %s gives no frames, only zeros (%u)", format.name,
                      seekPastEnd ? "of a frame past the end" : "at the end", read);
        check(read == 0 && zeros, what);
    }
}

#if !defined(Q_OS_WIN)
// A file cut short while a reader has it open (a file on a disk that went
// away, a copy that did not finish): contract points 3 and 4. Buffers and
// blocks (WavPack blocks, MAD's input buffer, AAC packets) may end the audio
// before the cut, so: a read comes short before the frame count; the frames
// before it are the file's; after it zeros and 0. A seek back into the part
// still there clears the error where it can land (WAV): that part reads
// again. WavPack and FLAC refuse every seek in a file cut while it is open
// (WavPack's library reads the last block on each, "can't read all of last
// block!"; libsndfile's FLAC decoder does not recover after it hit the cut),
// so the error stays: within the contract, a seek that fails is an error. (Windows refuses to
// resize a file another handle holds open.)
enum class SeekBack { NotTried, Reads, Refused };

static void test_cut_file(const QTemporaryDir& dir, const Format& format, SeekBack seekBack)
{
    char what[240];
    Source source = format.source;
    // Long enough that half of it is more than a decoder keeps in its buffers
    source.extraSeconds = 29;
    const QString path = dir.filePath(QStringLiteral("cut.%1").arg(QLatin1String(format.suffix)));
    if (!format.write(path, source)) {
        std::printf("%s: a cut file: cannot be written here, skipped\n", format.name);
        return;
    }
    const std::unique_ptr<TAudioReader> whole = TAudioReader::open(path).value_or(nullptr);
    const std::unique_ptr<TAudioReader> cut = TAudioReader::open(path).value_or(nullptr);
    if (!whole || !cut) {
        check(false, "a cut file: opens");
        return;
    }
    const uint channels = whole->channel_count();
    const nframes_t frames = whole->file_frame_count();
    std::vector<std::vector<float>> reference(channels);
    TAudioBuffer buffer;
    for (nframes_t at = 0; at < frames; at += 1000) {
        const nframes_t count = std::min<nframes_t>(1000, frames - at);
        const nframes_t read = whole->read_file_frames(buffer, at, count).frames;
        for (uint channel = 0; channel < channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(count);
            reference[channel].insert(reference[channel].end(), data, data + read);
        }
    }

    // A decoder may hold all of a small file in its buffers from the start
    // (MAD reads 40 KB at a time): the cut is no cut for it then
    // The reference must be whole (test_format() reports a short one)
    if (reference[0].size() != frames) {
        std::printf("%s: a cut file: the whole file reads short, skipped\n", format.name);
        return;
    }
    QFile file(path);
    if (file.size() < 64 * 1024) {
        std::printf("%s: a cut file: %lld bytes, a decoder may hold it all, skipped\n", format.name, static_cast<long long>(file.size()));
        return;
    }
    if (!file.resize(file.size() / 2)) {
        check(false, "a cut file: the file cut");
        return;
    }

    // From the start in reads of 1000 frames, until one comes short
    nframes_t shortAt = frames;
    bool before = true;
    bool after = true;
    for (nframes_t at = 0; at < frames; at += 1000) {
        const nframes_t count = std::min<nframes_t>(1000, frames - at);
        fill_with_garbage(buffer, count, channels);
        const nframes_t read = cut->read_file_frames(buffer, at, count).frames;
        if (shortAt == frames && read < count) {
            shortAt = at + read;
        }
        for (uint channel = 0; channel < channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(count);
            for (nframes_t i = 0; i < count; ++i) {
                const nframes_t frame = at + i;
                if (frame < shortAt) {
                    before = before && data[i] == reference[channel][frame];
                } else {
                    after = after && data[i] == 0.0f;
                }
            }
        }
        if (at >= shortAt && read != 0) {
            after = false;
        }
    }
    std::snprintf(what, sizeof(what), "%s: a file cut to half reads short before its end (at frame %u of %u)", format.name, shortAt, frames);
    check(shortAt < frames, what);
    std::snprintf(what, sizeof(what), "%s: a file cut to half, the frames before the error are the file's", format.name);
    check(before, what);
    std::snprintf(what, sizeof(what), "%s: a file cut to half, after the error zeros and 0", format.name);
    check(after, what);

    // A decoder whose seek failed is read again at the frame it is still at:
    // it must seek, not read from where its library was left (contract point
    // 3, task R8: MAD and FAAD move their input before the seek can fail).
    // A seek into the part still there (it lands, the error clears), a seek
    // past the cut (it fails, frame() stays), then the frames right after
    // the first read: a decoder that skipped the seek there reads nothing.
    if (seekBack == SeekBack::NotTried && shortAt > 6000 && frames - shortAt > 4000) {
        const nframes_t first = shortAt / 3;
        std::ignore = cut->read_file_frames(buffer, first, 1000);
        std::ignore = cut->read_file_frames(buffer, shortAt + 2000, 1000);
        fill_with_garbage(buffer, 1000, channels);
        const nframes_t read = cut->read_file_frames(buffer, first + 1000, 1000).frames;
        float worst = 0.0f;
        for (uint channel = 0; channel < channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(1000);
            for (nframes_t i = 0; i < 1000; ++i) {
                worst = std::max(worst, std::fabs(data[i] - reference[channel][first + 1000 + i]));
            }
        }
        const bool same = read == 1000 && worst <= format.seekTolerance;
        std::snprintf(what, sizeof(what), "%s: after a seek that failed, a read where the decoder still is seeks again (%u frames, worst %.2g)", format.name,
                      read, double(worst));
        check(same, what);
    }

    // A seek back into what is still there: it clears the error, or the
    // library refuses it and the error stays
    if (seekBack != SeekBack::NotTried && shortAt > 3000) {
        const nframes_t first = shortAt / 3;
        fill_with_garbage(buffer, 1000, channels);
        const nframes_t read = cut->read_file_frames(buffer, first, 1000).frames;
        bool same = true;
        for (uint channel = 0; channel < channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(1000);
            if (seekBack == SeekBack::Reads) {
                same = same && std::equal(data, data + 1000, reference[channel].begin() + first);
            } else {
                same = same && std::all_of(data, data + 1000, [](float sample) { return sample == 0.0f; });
            }
        }
        if (seekBack == SeekBack::Reads) {
            std::snprintf(what, sizeof(what), "%s: after the error a seek back reads again (%u frames)", format.name, read);
            check(read == 1000 && same, what);
        } else {
            std::snprintf(what, sizeof(what), "%s: after the error a seek back is refused, zeros and 0 (%u frames)", format.name, read);
            check(read == 0 && same, what);
        }
    }
}
#endif

// The bytes of a file, for the files made from other files below
static QByteArray file_bytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static bool write_bytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// An MP3 through libsndfile's LAME: CBR or VBR at a compression level
static bool write_mp3(const QString& path, const Source& source, int bitrateMode, double level)
{
    SF_INFO info{};
    info.samplerate = int(source.rate);
    info.channels = int(source.channels);
    info.format = SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III;
    SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
    if (!file) {
        return false;
    }
    sf_command(file, SFC_SET_BITRATE_MODE, &bitrateMode, sizeof(bitrateMode));
    sf_command(file, SFC_SET_COMPRESSION_LEVEL, &level, sizeof(level));
    const std::vector<float> samples = source.interleaved();
    const sf_count_t written = sf_writef_float(file, samples.data(), source.frames());
    sf_close(file);
    return written == sf_count_t(source.frames());
}

// Where the frames of a Layer III file lie
struct MpegFrame
{
    qsizetype offset;
    qsizetype size;
    bool mpeg1;
    bool mono;
    bool crc;
};

static std::vector<MpegFrame> mpeg_frames(const QByteArray& bytes)
{
    static const int bitratesMpeg1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const int bitratesMpeg2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static const int ratesMpeg1[] = {44100, 48000, 32000};
    std::vector<MpegFrame> frames;
    qsizetype at = 0;
    while (at + 4 <= bytes.size()) {
        const uchar* header = reinterpret_cast<const uchar*>(bytes.constData()) + at;
        const int version = (header[1] >> 3) & 3;
        const int bitrateIndex = header[2] >> 4;
        const int rateIndex = (header[2] >> 2) & 3;
        if (header[0] != 0xff || (header[1] & 0xe0) != 0xe0 || version == 1 || bitrateIndex == 0 || bitrateIndex == 15 || rateIndex == 3) {
            break;
        }
        const bool mpeg1 = version == 3;
        const int rate = ratesMpeg1[rateIndex] / (mpeg1 ? 1 : version == 2 ? 2 : 4);
        const int bitrate = (mpeg1 ? bitratesMpeg1 : bitratesMpeg2)[bitrateIndex];
        const qsizetype size = (mpeg1 ? 144 : 72) * bitrate * 1000 / rate + ((header[2] >> 1) & 1);
        frames.push_back({at, size, mpeg1, (header[3] >> 6) == 3, !(header[1] & 1)});
        at += size;
    }
    return frames;
}

// The file without its Info frame: no tag, no delay, no padding, no length
static QByteArray without_tag(const QByteArray& bytes)
{
    const std::vector<MpegFrame> frames = mpeg_frames(bytes);
    return frames.empty() ? QByteArray() : bytes.mid(frames[0].size);
}

// The Info frame and the first count frames of audio
static QByteArray first_frames(const QByteArray& bytes, size_t count)
{
    const std::vector<MpegFrame> frames = mpeg_frames(bytes);
    return count + 1 < frames.size() ? bytes.left(frames[count + 1].offset) : bytes;
}

// How many frames back the bit reservoir of the file reaches, at most
static int reservoir_reach(const QByteArray& bytes)
{
    const std::vector<MpegFrame> frames = mpeg_frames(bytes);
    const auto payload = [](const MpegFrame& frame) {
        const qsizetype side = frame.mpeg1 ? (frame.mono ? 17 : 32) : (frame.mono ? 9 : 17);
        return frame.size - 4 - side - (frame.crc ? 2 : 0);
    };
    int reach = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        const uchar* side = reinterpret_cast<const uchar*>(bytes.constData()) + frames[i].offset + 4 + (frames[i].crc ? 2 : 0);
        qsizetype back = frames[i].mpeg1 ? (side[0] << 1) | (side[1] >> 7) : side[0];
        int count = 0;
        while (back > 0 && size_t(count) < i) {
            ++count;
            back -= payload(frames[i - size_t(count)]);
        }
        reach = std::max(reach, count);
    }
    return reach;
}

// A WAV holding MP3 frames: format tag 0x55
static QByteArray wav_with_mp3(const QByteArray& mp3, uint rate, uint channels, nframes_t frames)
{
    const auto u16 = [](QByteArray& to, uint value) {
        to.append(char(value & 0xff));
        to.append(char((value >> 8) & 0xff));
    };
    const auto u32 = [&](QByteArray& to, uint value) {
        u16(to, value & 0xffff);
        u16(to, value >> 16);
    };
    QByteArray format;
    u16(format, 0x55);
    u16(format, channels);
    u32(format, rate);
    u32(format, 16000);
    u16(format, 1);
    u16(format, 0);
    u16(format, 12);
    u16(format, 1);
    u32(format, 2);
    u16(format, 417);
    u16(format, 1);
    u16(format, 0);

    QByteArray body("WAVEfmt ");
    u32(body, uint(format.size()));
    body += format;
    body += "fact";
    u32(body, 4);
    u32(body, frames);
    body += "data";
    u32(body, uint(mp3.size()));
    body += mp3;

    QByteArray riff("RIFF");
    u32(riff, uint(body.size()));
    return riff + body;
}

// Bytes without a 0xff, so no frame sync in them
static QByteArray junk(qsizetype size)
{
    std::mt19937 random(12345);
    QByteArray bytes(size, '\0');
    for (char& byte : bytes) {
        byte = char(random() % 255);
    }
    return bytes;
}

// What a reader gives from the start in reads of 1000 frames, until one comes
// short, and why that one did
struct ReadBack
{
    std::vector<std::vector<float>> channels;
    std::optional<TAudioFileError> error;
    nframes_t frames() const { return channels.empty() ? 0 : nframes_t(channels[0].size()); }
};

static ReadBack read_back(TAudioReader& reader)
{
    ReadBack back;
    back.channels.resize(reader.channel_count());
    TAudioBuffer buffer;
    for (nframes_t at = 0; at < reader.file_frame_count(); at += 1000) {
        const nframes_t count = std::min<nframes_t>(1000, reader.file_frame_count() - at);
        const TAudioFileResult result = reader.read_file_frames(buffer, at, count);
        for (uint channel = 0; channel < reader.channel_count(); ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(count);
            back.channels[channel].insert(back.channels[channel].end(), data, data + result.frames);
        }
        if (result.frames != count) {
            back.error = result.error;
            break;
        }
    }
    return back;
}

// count frames from first, against what the reference holds there
static float seek_error(TAudioReader& reader, nframes_t first, nframes_t count, const std::vector<float>& reference, TAudioFileResult* result = nullptr)
{
    TAudioBuffer buffer;
    fill_with_garbage(buffer, count, reader.channel_count());
    const TAudioFileResult read = reader.read_file_frames(buffer, first, count);
    if (result) {
        *result = read;
    }
    float worst = read.frames == count ? 0.0f : 1.0f;
    const float* data = buffer.get_channel_buffer(0).get_data(count);
    for (nframes_t i = 0; i < read.frames; ++i) {
        worst = std::max(worst, std::fabs(data[i] - reference[first + i]));
    }
    return worst;
}

static bool opens(const QString& path)
{
    return TAudioReader::open(path).has_value();
}

// Every case of doc/design/mp3-decoder.md section 6 that goes through a file.
// The audio is the sweeps, so a shift shows; the files are made from one MP3
// by libsndfile's LAME, which has an Info frame with delay and padding.
static void test_mpeg_files(const QTemporaryDir& dir)
{
    char what[240];
    Source source;
    source.extraSeconds = 29;
    const QString tagged = dir.filePath(QStringLiteral("t73-tagged.mp3"));
    if (!write_mp3(tagged, source, SF_BITRATE_MODE_CONSTANT, 0.5)) {
        check(false, "MP3 files: written");
        return;
    }
    const QByteArray bytes = file_bytes(tagged);
    const QByteArray bare = without_tag(bytes);
    const std::vector<float> written = source.channel(0);
    const std::vector<MpegFrame> frames = mpeg_frames(bytes);
    const std::unique_ptr<TAudioReader> reference = TAudioReader::open(tagged).value_or(nullptr);
    if (!reference || frames.size() < 100) {
        check(false, "MP3 files: the file made opens");
        return;
    }
    const ReadBack whole = read_back(*reference);
    check(reference->file_frame_count() == source.frames() && whole.frames() == source.frames() && !whole.error, "MP3 with a tag: all its frames, no more");

    // VBR without a tag: only a scan knows the length
    {
        const QString vbrPath = dir.filePath(QStringLiteral("t73-vbr.mp3"));
        const QString path = dir.filePath(QStringLiteral("t73-vbr-bare.mp3"));
        const bool made = write_mp3(vbrPath, source, SF_BITRATE_MODE_VARIABLE, 0.3) && write_bytes(path, without_tag(file_bytes(vbrPath)));
        std::vector<qsizetype> sizes;
        for (const MpegFrame& frame : mpeg_frames(file_bytes(path))) {
            sizes.push_back(frame.size);
        }
        std::sort(sizes.begin(), sizes.end());
        const size_t kinds = size_t(std::distance(sizes.begin(), std::unique(sizes.begin(), sizes.end())));
        std::snprintf(what, sizeof(what), "VBR without a tag: the file made is VBR (%zu frame sizes)", kinds);
        check(made && kinds > 2, what);
        const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
        const nframes_t count = reader ? reader->file_frame_count() : 0;
        const ReadBack back = reader ? read_back(*reader) : ReadBack{};
        std::snprintf(what, sizeof(what), "VBR without a tag: the whole length (%u frames of %u, %u read)", count, source.frames(), back.frames());
        check(count >= source.frames() && count < source.frames() + 3 * 1152 && back.frames() == count && !back.error, what);
    }

    // A bit reservoir that reaches further than MAD's 16 frames of pre-roll
    {
        Source low{24000, 2, 19};
        const QString path = dir.filePath(QStringLiteral("t73-reservoir.mp3"));
        const bool made = write_mp3(path, low, SF_BITRATE_MODE_CONSTANT, 1.0);
        const int reach = made ? reservoir_reach(file_bytes(path)) : 0;
        std::snprintf(what, sizeof(what), "The reservoir: the file reaches %d frames back", reach);
        check(reach > 16, what);
        const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
        if (reader) {
            const ReadBack back = read_back(*reader);
            float worst = 0.0f;
            for (const nframes_t first : {back.frames() / 2, back.frames() / 3 + 777, back.frames() / 8, back.frames() - 3000}) {
                worst = std::max(worst, seek_error(*reader, first, 2000, back.channels[0]));
            }
            std::snprintf(what, sizeof(what), "The reservoir: seeks read what was there (worst %.2g)", double(worst));
            check(!back.error && worst <= MpgSeekTolerance, what);
        } else {
            check(false, "The reservoir: opens");
        }
    }

    // Junk in front of the audio: found below the limit, refused above it
    {
        const QString path = dir.filePath(QStringLiteral("t73-junk.mp3"));
        write_bytes(path, junk(512 * 1024) + bytes);
        const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
        const ReadBack back = reader ? read_back(*reader) : ReadBack{};
        std::snprintf(what, sizeof(what), "Junk of 512 KiB in front: read whole (%u frames)", back.frames());
        check(back.frames() == whole.frames() && !back.channels.empty() && back.channels[0] == whole.channels[0], what);
        write_bytes(path, junk(2 * 1024 * 1024) + bytes);
        check(!opens(path), "Junk of 2 MiB in front: refused");
    }

    // The rules of the strict probe. The open reads one frame, then it walks
    {
        const QString path = dir.filePath(QStringLiteral("t73-short.mp3"));
        const auto probed = [&](const QByteArray& file) { return write_bytes(path, file) && opens(path); };
        check(!probed(first_frames(bare, 0).left(frames[1].size)), "One frame, no tag: refused");
        check(!probed(without_tag(first_frames(bytes, 2))), "Two frames, no tag: refused");
        check(probed(without_tag(first_frames(bytes, 3))), "Three frames, no tag: a clean end after 2 walked");
        check(probed(without_tag(first_frames(bytes, 7))), "Seven frames, no tag: 4 walked");
        check(!probed(first_frames(bytes, 1)), "One frame and a tag: refused");
        check(probed(first_frames(bytes, 2)), "Two frames and a tag: a clean end after 1 walked");

        // Random bytes are no audio, also the ones that start with a frame
        // sync. Fixed seed: a false hit shows every time
        std::mt19937 random(2026);
        int hits = 0;
        for (int file = 0; file < 300; ++file) {
            QByteArray noise(qsizetype(100 + random() % 70000), '\0');
            for (char& byte : noise) {
                byte = char(random());
            }
            if (file % 2) {
                noise[0] = char(0xff);
                noise[1] = char(0xfb);
                noise[2] = char(0x90);
            }
            hits += probed(noise) ? 1 : 0;
        }
        std::snprintf(what, sizeof(what), "300 files of random bytes: %d read as audio", hits);
        check(hits == 0, what);
    }

    // Damage in the middle, with a tag: exact up to the damage, an error at
    // it, a seek past it refused, a seek back reads
    {
        const QString path = dir.filePath(QStringLiteral("t73-damage.mp3"));
        QByteArray damaged = bytes;
        const MpegFrame& middle = frames[frames.size() / 2];
        damaged.replace(middle.offset, 3000, QByteArray(3000, '\0'));
        write_bytes(path, damaged);
        const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
        if (!reader) {
            check(false, "Damage with a tag: opens");
        } else {
            const ReadBack back = read_back(*reader);
            const nframes_t end = back.frames();
            const nframes_t expected = nframes_t((frames.size() / 2 - 1) * 1152);
            std::snprintf(what, sizeof(what), "Damage with a tag: the frame count is the tag's, the read fails at the damage (at %u, damage near %u, %s)", end, expected,
                          back.error ? qPrintable(back.error->detail) : "no error");
            check(reader->file_frame_count() == source.frames() && back.error && back.error->code == TAudioFileError::Code::ReadFailed && end + 3 * 1152 > expected && end < expected, what);
            const bool same = end > 0 && std::equal(back.channels[0].begin(), back.channels[0].end(), whole.channels[0].begin());
            const long lag = end > 8192 ? best_lag(written, end - 4096, back.channels[0], end - 4096, 2048, 3000) : -1;
            std::snprintf(what, sizeof(what), "Damage with a tag: bit exact up to the damage, not shifted (%+ld frames)", lag);
            check(same && lag == 0, what);

            TAudioFileResult past;
            seek_error(*reader, end + 3 * 1152, 1000, whole.channels[0], &past);
            check(past.frames == 0 && past.error && past.error->code == TAudioFileError::Code::SeekFailed, "Damage with a tag: a seek past it is refused");
            const float worst = seek_error(*reader, end / 2, 2000, whole.channels[0]);
            std::snprintf(what, sizeof(what), "Damage with a tag: a seek back reads (worst %.2g)", double(worst));
            check(worst <= MpgSeekTolerance, what);
        }
    }

    // Damage without a tag: nothing says how long the file is
    {
        const QString path = dir.filePath(QStringLiteral("t73-damage-bare.mp3"));
        QByteArray damaged = bare;
        const std::vector<MpegFrame> bareFrames = mpeg_frames(bare);
        const qsizetype place = bareFrames[bareFrames.size() / 2].offset;
        damaged.replace(place, 3000, QByteArray(3000, '\0'));
        write_bytes(path, damaged);
        const auto opened = TAudioReader::open(path);
        std::snprintf(what, sizeof(what), "Damage without a tag: CannotOpen with the byte position (%s, damage at %lld)", opened ? "opened" : qPrintable(opened.error().detail),
                      static_cast<long long>(place));
        check(!opened && opened.error().code == TAudioFileError::Code::CannotOpen && opened.error().detail.contains(QString::number(place + 4)), what);

        // Near the end it is the end of the file
        damaged = bare;
        const qsizetype late = bareFrames[bareFrames.size() - 20].offset;
        damaged.replace(late, 2000, QByteArray(2000, '\0'));
        write_bytes(path, damaged);
        const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
        const ReadBack back = reader ? read_back(*reader) : ReadBack{};
        std::snprintf(what, sizeof(what), "Damage without a tag, less than 64 KiB before the end: opens, what the scan found (%u frames)", back.frames());
        check(reader && back.frames() == reader->file_frame_count() && !back.error && back.frames() + 40 * 1152 > source.frames(), what);
    }

    // A file cut short before it is opened: the tag's length, the error at
    // the end, the audio in place
    {
        const QString path = dir.filePath(QStringLiteral("t73-cut.mp3"));
        write_bytes(path, bytes.left(bytes.size() / 2));
        const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
        if (!reader) {
            check(false, "Cut before the open: opens");
        } else {
            const ReadBack back = read_back(*reader);
            const nframes_t end = back.frames();
            std::snprintf(what, sizeof(what), "Cut before the open: the tag's length, an error where the audio ends (at %u of %u)", end, reader->file_frame_count());
            check(reader->file_frame_count() == source.frames() && back.error && back.error->code == TAudioFileError::Code::ReadFailed && end > source.frames() / 3 && end < source.frames() * 2 / 3, what);
            const long lag = end > 8192 ? best_lag(written, end - 4096, back.channels[0], end - 4096, 2048, 3000) : -1;
            const bool same = end > 0 && std::equal(back.channels[0].begin(), back.channels[0].end(), whole.channels[0].begin());
            std::snprintf(what, sizeof(what), "Cut before the open: the audio in place (%+ld frames)", lag);
            check(same && lag == 0, what);
            TAudioFileResult past;
            seek_error(*reader, end + 3 * 1152, 1000, whole.channels[0], &past);
            check(past.frames == 0 && past.error && past.error->code == TAudioFileError::Code::SeekFailed, "Cut before the open: a seek past the end of the audio is refused");
        }
    }

    // MP3 in a WAV: read by mpg123, whole
    {
        const QString path = dir.filePath(QStringLiteral("t73-in-wav.wav"));
        write_bytes(path, wav_with_mp3(bare, source.rate, source.channels, source.frames()));
        const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
        std::snprintf(what, sizeof(what), "MP3 in a WAV: opens (%s)", reader ? qPrintable(reader->decoder_type()) : "no decoder");
        check(reader && reader->decoder_type() == QLatin1String("mpg123"), what);
        const ReadBack back = reader ? read_back(*reader) : ReadBack{};
        std::snprintf(what, sizeof(what), "MP3 in a WAV: whole (%u frames of %u)", back.frames(), source.frames());
        check(back.frames() >= source.frames() && back.frames() < source.frames() + 3 * 1152 && !back.error, what);
    }

    // Two files one after the other, the second tag in the middle
    {
        const QString path = dir.filePath(QStringLiteral("t73-joined.mp3"));
        write_bytes(path, bytes + bytes);
        const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
        const ReadBack back = reader ? read_back(*reader) : ReadBack{};
        const nframes_t count = reader ? reader->file_frame_count() : 0;
        std::snprintf(what, sizeof(what), "Concatenated: both parts (%u frames of twice %u, %u read)", count, source.frames(), back.frames());
        check(count > 2 * source.frames() && count < 2 * source.frames() + 6 * 1152 && back.frames() == count && !back.error, what);
        if (back.frames() == count && count > source.frames()) {
            const nframes_t from = count - source.frames() / 2;
            const long lag = best_lag(written, source.frames() / 2, back.channels[0], from, 4096, 6000);
            const double level = rms(back.channels[0], count - source.frames() / 2, count - source.frames() / 4);
            std::snprintf(what, sizeof(what), "Concatenated: the second part is there (%+ld frames, level %.3f)", lag, level);
            check(std::labs(lag) <= 3 * 1152 + 1 && std::fabs(level - source.level(0)) < 0.05, what);
        }
    }

    // Another format inside a file: damage, stop with an error
    for (const Source& second : {Source{44100, 1, 3}, Source{22050, 2, 3}}) {
        const QString other = dir.filePath(QStringLiteral("t73-other.mp3"));
        const QString path = dir.filePath(QStringLiteral("t73-format.mp3"));
        if (!write_mp3(other, second, SF_BITRATE_MODE_CONSTANT, 0.5) || !write_bytes(path, bytes + without_tag(file_bytes(other)))) {
            check(false, "A change of format: written");
            continue;
        }
        const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
        const ReadBack back = reader ? read_back(*reader) : ReadBack{};
        std::snprintf(what, sizeof(what), "A change of format to %u channels at %u Hz: an error after %u frames (%s)", second.channels, second.rate, back.frames(),
                      back.error ? qPrintable(back.error->detail) : "no error");
        check(reader && back.error && back.error->detail.contains(QLatin1String("format changes")) && back.frames() + 3 * 1152 > source.frames() && back.frames() < source.frames() + 3 * 1152, what);
        if (reader) {
            const bool same = back.frames() > source.frames() && std::equal(whole.channels[0].begin(), whole.channels[0].end(), back.channels[0].begin());
            check(same, "A change of format: exact up to the change");
            // The first seek after a change of rate can fail, with an error;
            // the seek after it lands (contract point 3: a caller seeks again)
            TAudioFileResult first;
            float worst = seek_error(*reader, 5000, 2000, whole.channels[0], &first);
            if (first.error) {
                worst = seek_error(*reader, 5000, 2000, whole.channels[0]);
            }
            std::snprintf(what, sizeof(what), "A change of format: a seek back reads (worst %.2g)", double(worst));
            check(worst <= MpgSeekTolerance, what);
        }
    }
}

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }

    const auto sndfile = [](int format) {
        return [format](const QString& path, const Source& source) { return write_with_sndfile(path, source, format); };
    };
    const auto ffmpeg = [](QStringList encode) {
        return [encode](const QString& path, const Source& source) { return write_with_ffmpeg(path, source, encode); };
    };
    const Source stereo;
    const Source mono22{22050, 1};
    const std::vector<Format> formats = {
        {"WAV float (sndfile)", "wav", stereo, sndfile(SF_FORMAT_WAV | SF_FORMAT_FLOAT), true, 0.0f},
        // The test signals described, computed by the decoder on its own:
        // the same sweeps within float rounding
        {"Scenario (test signals)", "tscenario", stereo, write_scenario, true, 1e-6f},
        {"WAV 16 bit (sndfile)", "16.wav", stereo, sndfile(SF_FORMAT_WAV | SF_FORMAT_PCM_16), true, 1.0f / 32768},
        {"FLAC 24 bit (sndfile)", "flac", stereo, sndfile(SF_FORMAT_FLAC | SF_FORMAT_PCM_24), true, 1.0f / 8388608},
        {"AIFF 24 bit (sndfile)", "aiff", stereo, sndfile(SF_FORMAT_AIFF | SF_FORMAT_PCM_24), true, 1.0f / 8388608},
        {"W64 24 bit (sndfile)", "w64", stereo, sndfile(SF_FORMAT_W64 | SF_FORMAT_PCM_24), true, 1.0f / 8388608},
        {"RF64 float (sndfile)", "rf64", stereo, sndfile(SF_FORMAT_RF64 | SF_FORMAT_FLOAT), true, 0.0f},
        {"CAF 24 bit (sndfile)", "caf", stereo, sndfile(SF_FORMAT_CAF | SF_FORMAT_PCM_24), true, 1.0f / 8388608},
        {"Ogg Vorbis (sndfile)", "ogg", stereo, sndfile(SF_FORMAT_OGG | SF_FORMAT_VORBIS), false, 0.0f},
        {"Ogg Opus (sndfile)", "opus", Source{48000, 2}, sndfile(SF_FORMAT_OGG | SF_FORMAT_OPUS), false, 0.0f},
        {"MP3 with a LAME tag", "mp3", stereo, sndfile(SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III), false, 0.0f, 0, true, MpgSeekTolerance},
        {"MP3 MPEG-2 mono 22.05 kHz", "22.mp3", mono22, sndfile(SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III), false, 0.0f, 0, true, MpgSeekTolerance},
        // The tag's padding is a lie here: the file holds more frames than
        // were written, the last of them silence
        {"MP3 with a LAME padding below 529", "pad.mp3", stereo,
         [](const QString& path, const Source& source) { return write_mp3_with_padding(path, source, 100); }, false, 0.0f, 2304, true, MpgSeekTolerance},
        {"MP3 without gapless information (ffmpeg)", "nolame.mp3", stereo,
         ffmpeg({QStringLiteral("-c:a"), QStringLiteral("libmp3lame"), QStringLiteral("-b:a"), QStringLiteral("128k"), QStringLiteral("-write_xing"),
                 QStringLiteral("0")}),
         false, 0.0f, 3000, false, MpgSeekTolerance},
        {"WavPack float", "wv", stereo,
         [](const QString& path, const Source& source) {
             return write_with_traverso(path, source, TraversoDAW::WriterType::WAVPACK, TraversoDAW::FileFormat::WAVPACK);
         },
         true, 0.0f},
#if defined(M4A_DECODE_SUPPORT)
        {"AAC in ADTS (ffmpeg)", "aac", stereo,
         ffmpeg({QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("128k"), QStringLiteral("-f"),
                 QStringLiteral("adts")}),
         false, 0.0f, 1024, true, 0.002f},
        // ffmpeg's edit list says 23 frames fewer than it was given (ffprobe
        // too): the decoder takes the file's word
        {"AAC in M4A (ffmpeg)", "m4a", stereo, ffmpeg({QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("128k")}),
         false, 0.0f, 64, true, 0.002f},
#endif
#if defined(M4A_ENCODE_SUPPORT) && defined(M4A_DECODE_SUPPORT)
        {"AAC in ADTS (Traverso's writer)", "traverso.aac", stereo,
         [](const QString& path, const Source& source) {
             return write_with_traverso(path, source, TraversoDAW::WriterType::M4A, TraversoDAW::FileFormat::UNKNOWN);
         },
         false, 0.0f, 1024, true, 0.0f, -2048, "libfaac leaves out the first 2048 frames (task: Traverso's AAC writer)"},
#endif
    };
    for (const Format& format : formats) {
        test_format(dir, format);
    }
#if !defined(Q_OS_WIN)
    for (const Format& format : formats) {
        const QLatin1String suffix(format.suffix);
        const SeekBack seekBack = suffix == QLatin1String("wav") || suffix == QLatin1String("16.wav") ? SeekBack::Reads
                                  : suffix == QLatin1String("wv") || suffix == QLatin1String("flac") ? SeekBack::Refused
                                                                                                     : SeekBack::NotTried;
        test_cut_file(dir, format, seekBack);
    }
#endif
    test_mpeg_files(dir);

    // Everything MP3 again with mpg123's generic decoder, the one without
    // assembly (the synthesis rounds differently)
    qputenv("TRAVERSO_MPG123_DECODER", "generic");
    std::printf("mpg123's generic decoder:\n");
    for (const Format& format : formats) {
        if (QLatin1String(format.suffix).endsWith(QLatin1String("mp3"))) {
            test_format(dir, format);
#if !defined(Q_OS_WIN)
            test_cut_file(dir, format, SeekBack::NotTried);
#endif
        }
    }
    test_mpeg_files(dir);

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all decoder tests passed\n");
    return 0;
}
