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

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all decoder tests passed\n");
    return 0;
}
