// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TByteSource, the only way a decoder reaches its file.
// The source itself on bytes in memory: what a read gives, how it is counted.
// Then every format through a source in memory that waits a known time:
// the same samples as libsndfile reads from the file directly (sf_open), and
// the reads in the right kind of work (open, seek, stream), the medium's
// time apart from the decoding. A read that fails halfway gives ReadFailed
// with the source's reason in every decoder. WavPack's correction file is
// read through a source of its own. Bytes past 2 and 4 GiB come from where
// they are.

#include "TAudioBuffer.h"
#include "TByteSource.h"
#include "TAudioReader.h"
#include "TAudioWriter.h"
#include "TResamplerChoice.h"
#include "TTimeRef.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <random>

#include <sndfile.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

static constexpr nframes_t Rate = 48000;
static constexpr nframes_t Frames = 10 * Rate + 1037;
static constexpr uint Channels = 2;
static constexpr uint64_t Wait = 1000 * 1000;
static const QLatin1String Reason("the medium is gone");

// What the sources of one test share
struct Medium
{
    // Reads wait this long, and only then count as the medium's
    bool waits{false};
    // Reads from this byte on fail, -1: none do
    int64_t failFrom{-1};
    // Up to this byte, -1: to the end
    int64_t failTo{-1};
};

// A file's bytes in memory
class MemorySource : public TByteSource
{
public:
    MemorySource(QByteArray bytes, const Medium* medium) : m_bytes(std::move(bytes)), m_medium(medium) {}

    int64_t size() const override { return m_bytes.size(); }

protected:
    std::optional<int64_t> read_cached(int64_t, void*, int64_t) override
    {
        if (m_medium->waits) {
            return 0;
        }
        return std::nullopt;
    }

    TMediumRead read_medium(int64_t position, void* data, int64_t bytes) override
    {
        if (m_medium->waits) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(Wait));
        }
        if (position >= m_bytes.size()) {
            return {};
        }
        const int64_t end = std::min(position + bytes, int64_t(m_bytes.size()));
        const bool failing = m_medium->failFrom >= 0 && (m_medium->failTo < 0 || position < m_medium->failTo);
        const int64_t good = failing ? std::min(end, m_medium->failFrom) : end;
        TMediumRead read;
        if (good < end) {
            read.reason = Reason;
        }
        if (good > position) {
            std::memcpy(data, m_bytes.constData() + position, size_t(good - position));
            read.bytes = good - position;
        }
        return read;
    }

private:
    QByteArray m_bytes;
    const Medium* m_medium;
};

static TByteSourceOpener memory_opener(const Medium* medium)
{
    return [medium](const QString& fileName) -> TByteSourceResult {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly)) {
            return std::unexpected(TByteSourceError{!file.exists(), file.errorString()});
        }
        return std::make_unique<MemorySource>(file.readAll(), medium);
    };
}

static void test_source()
{
    const Medium medium;
    MemorySource source(QByteArray("0123456789"), &medium);
    TReadsByOrigin stats;
    source.count_reads_into(&stats);

    char bytes[10];
    check(source.read(bytes, 4) == 4 && !std::memcmp(bytes, "0123", 4) && source.position() == 4, "a read gives its bytes and moves on");
    check(source.seek(8) && source.position() == 8 && !source.at_end(), "a seek moves the position");
    check(source.read(bytes, 4) == 2 && !std::memcmp(bytes, "89", 2) && source.at_end(), "a read over the end gives what is left");
    check(source.read(bytes, 4) == 0 && !source.take_error(), "a read at the end gives 0, no error");
    check(!source.seek(-1) && source.position() == 10, "a seek below 0 is refused");
    // Three reads of the source: the block, and two at the end that gave
    // nothing (the rest of the read over the end, the read at the end):
    // every read counts; none failed, there was no error
    check(stats.unclassified.reads == 3 && stats.unclassified.bytes == 10 && stats.unclassified.failed == 0 && stats.medium.reads == 0 && stats.cached.reads == 0,
          "small reads share a block; a read that gives nothing counts too; without a word on the cache unclassified");

    const QByteArray large(3 * TByteSource::BlockBytes, 'x');
    MemorySource big(large, &medium);
    TReadsByOrigin bigStats;
    big.count_reads_into(&bigStats);
    std::vector<char> into(size_t(large.size()));
    check(big.read(into.data(), 2) == 2 && big.read(into.data() + 2, large.size() - 2) == large.size() - 2 && !std::memcmp(into.data(), large.constData(), size_t(large.size())),
          "a large read after a small one gives the block's bytes and the rest");
    check(bigStats.unclassified.bytes == uint64_t(large.size()), "no byte is read twice");
    check(big.seek(1) && big.read(into.data(), 4) == 4 && bigStats.unclassified.bytes == uint64_t(large.size()), "a seek back inside the block reads nothing");

    Medium failing{false, 5};
    MemorySource broken(QByteArray("0123456789"), &failing);
    TReadsByOrigin brokenStats;
    broken.count_reads_into(&brokenStats);
    check(broken.read(bytes, 8) == 5 && broken.take_error() == QString(Reason), "a read that fails halfway gives what it has, and why");
    check(!broken.take_error(), "why is told once");
    check(broken.read(bytes, 8) == -1 && broken.take_error() == QString(Reason), "a read that gives nothing and fails is -1");
    check(brokenStats.unclassified.failed == 2 && brokenStats.unclassified.reads == 2 && brokenStats.unclassified.bytes == 5,
          "failed reads count, the one with bytes and the one without");
    // -1 is of this read only: an error that waits for take_error() does not
    // make a later read at the end of the file a failure
    MemorySource halfway(QByteArray("0123456789"), &failing);
    check(halfway.read(bytes, 8) == 5, "a read fails halfway and gives its bytes");
    check(halfway.seek(10) && halfway.read(bytes, 4) == 0, "the next read, at the end of the file, gives 0, not -1, without take_error() between them");
    check(halfway.take_error() == QString(Reason), "take_error() then gives the reason of the first read");

    // Without take_error() between them (a decoder takes the error only at
    // the end of its call): each failed read counts
    MemorySource again(QByteArray("0123456789"), &failing);
    TReadsByOrigin againStats;
    again.count_reads_into(&againStats);
    (void)again.read(bytes, 8);
    (void)again.read(bytes, 8);
    check(againStats.unclassified.failed == 2, "every failed read counts, also before the error is taken");

    const TByteSourceResult missing = TByteSource::open_file(QStringLiteral("/no/such/file"));
    check(!missing && missing.error().notFound && !missing.error().reason.isEmpty(), "a file that is not there: not found, with a reason");
}

static std::vector<float> test_samples()
{
    std::vector<float> samples(size_t(Frames) * Channels);
    for (nframes_t frame = 0; frame < Frames; ++frame) {
        samples[size_t(frame) * 2] = 0.5f * std::sin(0.03f * float(frame));
        samples[size_t(frame) * 2 + 1] = 0.3f * std::sin(0.011f * float(frame) * (1.0f + float(frame) / float(Frames)));
    }
    return samples;
}

static bool write_sndfile(const QString& path, int format)
{
    SF_INFO info{};
    info.samplerate = Rate;
    info.channels = Channels;
    info.format = format;
    SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
    if (!file) {
        return false;
    }
    const std::vector<float> samples = test_samples();
    const bool written = sf_writef_float(file, samples.data(), Frames) == Frames;
    return sf_close(file) == 0 && written;
}

static bool write_wavpack(const QString& path)
{
    TAudioEncodeSettings settings;
    settings.writerType = TraversoDAW::WriterType::WAVPACK;
    settings.fileFormat = TraversoDAW::FileFormat::WAVPACK;
    settings.dataFormat = TraversoDAW::DataFormat::FLOAT;
    settings.channelCount = Channels;
    settings.sampleRate = Rate;
    const std::unique_ptr<TAudioWriter> file = TAudioWriter::open(settings, path, Rate).value_or(nullptr);
    if (!file) {
        return false;
    }
    TAudioBuffer buffer;
    buffer.check_capacity(Frames, Channels);
    const std::vector<float> samples = test_samples();
    for (uint channel = 0; channel < Channels; ++channel) {
        float* data = buffer.get_channel_buffer(channel).get_data(Frames);
        for (nframes_t frame = 0; frame < Frames; ++frame) {
            data[frame] = samples[size_t(frame) * Channels + channel];
        }
    }
    const bool written = file->write(buffer, Frames).frames == Frames;
    return file->close().has_value() && written;
}

static bool write_ffmpeg(const QString& path, const QTemporaryDir& dir, const QStringList& encode)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString wav = dir.filePath(QStringLiteral("ffmpeg-source.wav"));
    if (ffmpeg.isEmpty() || !write_sndfile(wav, SF_FORMAT_WAV | SF_FORMAT_PCM_16)) {
        return false;
    }
    QStringList arguments{QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-i"), wav};
    arguments << encode << path;
    return QProcess::execute(ffmpeg, arguments) == 0;
}

struct Format
{
    const char* name;
    const char* suffix;
    // libsndfile's format; 0: made otherwise, not compared with sf_open
    int sndfile;
    std::function<bool(const QString&)> write;
};

// Every frame of the file through reader, in steps a stream could take
static std::vector<float> read_all(TAudioReader& reader, nframes_t step)
{
    TAudioBuffer buffer;
    std::vector<float> all;
    for (nframes_t first = 0; first < reader.file_frame_count(); first += step) {
        const nframes_t read = reader.read_file_frames(buffer, first, step).frames;
        for (nframes_t frame = 0; frame < read; ++frame) {
            for (uint channel = 0; channel < reader.channel_count(); ++channel) {
                all.push_back(buffer.get_channel_buffer(channel).get_data(read)[frame]);
            }
        }
    }
    return all;
}

static std::vector<float> read_with_sndfile(const QString& path)
{
    SF_INFO info{};
    SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_READ, &info);
    if (!file) {
        return {};
    }
    std::vector<float> samples(size_t(info.frames) * info.channels);
    samples.resize(size_t(sf_readf_float(file, samples.data(), info.frames)) * info.channels);
    sf_close(file);
    return samples;
}

static void test_format(const QTemporaryDir& dir, const Format& format)
{
    const QString path = dir.filePath(QStringLiteral("file.") + QLatin1String(format.suffix));
    if (!format.write(path)) {
        std::printf("%s: not written here, not tested\n", format.name);
        return;
    }
    char what[200];

    if (format.sndfile) {
        const std::vector<float> direct = read_with_sndfile(path);
        const Medium medium;
        auto reader = TAudioReader::open(path, memory_opener(&medium));
        std::snprintf(what, sizeof(what), "%s: the same samples as libsndfile from the file", format.name);
        check(reader && !direct.empty() && read_all(**reader, 4099) == direct, what);
    }

    // Where the time goes: every read of the medium waits
    Medium waiting{true, -1};
    auto reader = TAudioReader::open(path, memory_opener(&waiting));
    if (!reader) {
        std::snprintf(what, sizeof(what), "%s: opens", format.name);
        check(false, what);
        return;
    }
    const TAudioIOStats& stats = (*reader)->io_stats();

    std::snprintf(what, sizeof(what), "%s: opening reads, counted as the open", format.name);
    check(stats.open.reads.medium.bytes > 0 && stats.seek.reads.medium.reads == 0 && stats.stream.reads.medium.reads == 0 && stats.open.nanoseconds >= stats.open.reads.medium.nanoseconds, what);

    const uint64_t fileBytes = uint64_t(QFile(path).size());
    TAudioBuffer buffer;
    const nframes_t step = 4800;
    nframes_t read = 0;
    for (nframes_t first = 0; first < (*reader)->file_frame_count(); first += step) {
        read += (*reader)->read_file_frames(buffer, first, step).frames;
    }
    const TAudioIOCost stream = stats.stream;
    std::snprintf(what, sizeof(what), "%s: a read to the end counts every frame and goes through the source", format.name);
    check(read == (*reader)->file_frame_count() && stream.frames == read && stream.calls > 0 && stream.reads.medium.reads > 0 && stream.reads.medium.bytes <= fileBytes, what);
    std::snprintf(what, sizeof(what), "%s: the medium's time is what its reads waited, the decoding the rest", format.name);
    check(stream.reads.medium.nanoseconds >= stream.reads.medium.reads * Wait && stream.reads.medium.worstNanoseconds >= Wait &&
              stream.nanoseconds >= stream.read_nanoseconds() && stream.decode_nanoseconds() == stream.nanoseconds - stream.read_nanoseconds(),
          what);

    // A seek back, to a place that has to be found again
    (void)(*reader)->read_file_frames(buffer, (*reader)->file_frame_count() / 3, step);
    std::snprintf(what, sizeof(what), "%s: a seek counts apart, the read after it as stream", format.name);
    check(stats.seek.calls == 1 && stats.seek.nanoseconds >= stats.seek.read_nanoseconds() && stats.stream.calls == stream.calls + 1, what);
}

// An ID3v2 tag at the start of bytes
static QByteArray with_id3_tag(const QByteArray& bytes, int tagBytes)
{
    QByteArray tagged("ID3\x04\x00\x00", 6);
    for (const int shift : {21, 14, 7, 0}) {
        tagged.append(char((tagBytes >> shift) & 0x7f));
    }
    tagged.append(QByteArray(tagBytes, '\0'));
    tagged.append(bytes);
    return tagged;
}

// A medium that fails from the first byte on: every probe and the open fail,
// and the open says why, it does not hang, also after a large ID3 tag on a NAS
// that then answers EIO
static void test_open_failure(const QTemporaryDir& dir, const Format& format)
{
    const QString path = dir.filePath(QStringLiteral("file.") + QLatin1String(format.suffix));
    if (!QFile::exists(path)) {
        return;
    }
    Medium medium;
    medium.failFrom = 0;
    char what[160];
    std::snprintf(what, sizeof(what), "%s: a medium that fails at once: no decoder, no hang", format.name);
    const auto refused = TAudioReader::open(path, memory_opener(&medium));
    check(!refused && refused.error().detail == QString(Reason), what);

    // An MP3 behind an ID3v2 tag larger than the source's block (album art):
    // the tag reads, the first audio byte after it fails
    if (std::strcmp(format.suffix, "mp3") != 0) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const int tagBytes = 20 * 1024;
    const QByteArray tagged = with_id3_tag(file.readAll(), tagBytes);
    const QString taggedPath = dir.filePath(QStringLiteral("tagged.mp3"));
    QFile out(taggedPath);
    if (!out.open(QIODevice::WriteOnly) || out.write(tagged) != tagged.size()) {
        check(false, "the tagged MP3 is written");
        return;
    }
    out.close();
    Medium whole;
    check(TAudioReader::open(taggedPath, memory_opener(&whole)).has_value(), "MP3: an MP3 behind a 20 KiB tag opens");
    medium.failFrom = 10 + tagBytes;
    check(!TAudioReader::open(taggedPath, memory_opener(&medium)).has_value(), "MP3: the tag reads, the first audio byte fails: no decoder, no hang");
}

// The size of the first frame of an MP3 by libsndfile, its Info frame: 128
// kbps at this file's rate
static qsizetype first_frame_bytes(const QByteArray& bytes)
{
    const uchar* header = reinterpret_cast<const uchar*>(bytes.constData());
    static const int bitrates[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    return 144 * bitrates[header[2] >> 4] * 1000 / Rate + ((header[2] >> 1) & 1);
}

// Album art of 1 MiB is skipped, not read: libmpg123 (MPG123_SKIP_ID3V2) reads
// none of the tag, in the probe or the reader. What may be read: all the audio
// (the scan) and what goes around it. libmpg123 reads in small pieces, so every
// place it goes to fetches a block of the source: the probe three, the reader
// two, the seek one, and the ends of the file (ID3v1). 98550 bytes measured on
// top of the audio, 6 blocks and a little; a margin of 8 blocks keeps 2 spare.
// It does not grow with the tag, so a tag that is read fails.
static void test_large_tag(const QTemporaryDir& dir)
{
    const QString plain = dir.filePath(QStringLiteral("file.mp3"));
    QFile file(plain);
    if (!file.open(QIODevice::ReadOnly)) {
        check(false, "a tag of 1 MiB: the MP3 is there");
        return;
    }
    const QByteArray audio = file.readAll();
    const int tagBytes = 1024 * 1024;
    const QString path = dir.filePath(QStringLiteral("art.mp3"));
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(with_id3_tag(audio, tagBytes)) == -1) {
        check(false, "the MP3 with a tag of 1 MiB is written");
        return;
    }
    out.close();
    const int64_t fileBytes = out.size();

    Medium medium;
    auto plainReader = TAudioReader::open(plain, memory_opener(&medium));
    auto reader = TAudioReader::open(path, memory_opener(&medium));
    if (!reader || !plainReader) {
        check(false, "an MP3 behind a tag of 1 MiB opens");
        return;
    }
    check((*reader)->file_frame_count() == (*plainReader)->file_frame_count(), "behind a tag of 1 MiB: its length");

    TAudioBuffer buffer;
    TAudioBuffer plainBuffer;
    const nframes_t first = (*reader)->file_frame_count() / 2;
    const nframes_t frames = (*reader)->read_file_frames(buffer, first, 4800).frames;
    (void)(*plainReader)->read_file_frames(plainBuffer, first, 4800);
    const float* data = buffer.get_channel_buffer(0).get_data(4800);
    const float* plainData = plainBuffer.get_channel_buffer(0).get_data(4800);
    float worst = 0.0f;
    for (nframes_t frame = 0; frame < frames; ++frame) {
        worst = std::max(worst, std::fabs(data[frame] - plainData[frame]));
    }
    check(frames == 4800 && worst <= 1e-6f, "behind a tag of 1 MiB: a seek reads what is there");

    const auto bytes = [](const TAudioIOCost& activity) { return activity.reads.cached.bytes + activity.reads.medium.bytes + activity.reads.unclassified.bytes; };
    const TAudioIOStats& stats = (*reader)->io_stats();
    const uint64_t total = bytes(stats.open) + bytes(stats.seek) + bytes(stats.stream);
    char what[160];
    std::snprintf(what, sizeof(what), "behind a tag of 1 MiB: the tag is not read (%llu bytes read of %lld)", static_cast<unsigned long long>(total), static_cast<long long>(fileBytes));
    check(total <= uint64_t(fileBytes - tagBytes) + 8 * uint64_t(TByteSource::BlockBytes), what);
}

// A read that fails during the scan: libmpg123 takes it for the end of the
// file, the open must not. The probe's frames and the end of the file read
// well, so only the scan meets it. Without a tag the place lies inside the 64
// KiB that may follow the audio: only the source's error tells.
static void test_scan_failure(const QTemporaryDir& dir)
{
    const QString tagged = dir.filePath(QStringLiteral("file.mp3"));
    QFile file(tagged);
    if (!file.open(QIODevice::ReadOnly)) {
        check(false, "a failed scan: the MP3 is there");
        return;
    }
    const QByteArray bytes = file.readAll();
    const QString bare = dir.filePath(QStringLiteral("bare.mp3"));
    QFile out(bare);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes.mid(first_frame_bytes(bytes))) == -1) {
        check(false, "the MP3 without a tag is written");
        return;
    }
    out.close();

    for (const QString& path : {tagged, bare}) {
        const int64_t size = QFile(path).size();
        Medium medium;
        medium.failFrom = size - 20 * 1024;
        medium.failTo = size - 16 * 1024;
        const auto reader = TAudioReader::open(path, memory_opener(&medium));
        char what[200];
        std::snprintf(what, sizeof(what), "a read that fails during the scan (%s): CannotOpen with the reason (%s)", path == tagged ? "a tag" : "no tag",
                      reader ? "it opened" : qPrintable(reader.error().detail));
        check(!reader && reader.error().code == TAudioFileError::Code::CannotOpen && reader.error().detail == QString(Reason), what);
    }
}

// A read that fails inside a decoder's own open: the source's reason, not what
// the library made of it. WavPack has a signature, so the first block is read
// by find_decoder() and all later reads by the decoder's start()
static void test_start_failure(const QTemporaryDir& dir)
{
    const QString path = dir.filePath(QStringLiteral("file.wv"));
    if (!QFile::exists(path)) {
        check(false, "a failed start: the WavPack file is there");
        return;
    }
    Medium medium;
    medium.failFrom = TByteSource::BlockBytes;
    const auto reader = TAudioReader::open(path, memory_opener(&medium));
    char what[200];
    std::snprintf(what, sizeof(what), "a read that fails in the open: the reason of the source (%s)", reader ? "it opened" : qPrintable(reader.error().detail));
    check(!reader && reader.error().detail == QString(Reason), what);
}

// A read of the medium that fails in the middle of the file: whatever the
// library makes of its short read, the caller gets ReadFailed and the reason
static void test_read_failure(const QTemporaryDir& dir, const Format& format)
{
    const QString path = dir.filePath(QStringLiteral("file.") + QLatin1String(format.suffix));
    if (!QFile::exists(path)) {
        return;
    }
    Medium medium;
    auto reader = TAudioReader::open(path, memory_opener(&medium));
    if (!reader) {
        return;
    }
    medium.failFrom = QFile(path).size() / 2;

    TAudioBuffer buffer;
    const nframes_t step = 4800;
    std::optional<TAudioFileError> error;
    nframes_t first = 0;
    for (; first < (*reader)->file_frame_count() && !error; first += step) {
        error = (*reader)->read_file_frames(buffer, first, step).error;
    }
    char what[240];
    std::snprintf(what, sizeof(what), "%s: a read that fails is ReadFailed, with the reason of the source (%s)", format.name,
                  error ? qPrintable(error->detail) : "no error");
    check(error && error->code == TAudioFileError::Code::ReadFailed && error->detail == QString(Reason), what);
}

// A read that fails during a seek: mpg123's seek takes it for the end of the
// stream and lands, the reason must reach the caller all the same
static void test_seek_failure(const QTemporaryDir& dir)
{
    const QString path = dir.filePath(QStringLiteral("file.mp3"));
    Medium medium;
    auto reader = TAudioReader::open(path, memory_opener(&medium));
    if (!reader) {
        check(false, "a failed seek: the MP3 opens");
        return;
    }
    medium.failFrom = 0;
    TAudioBuffer buffer;
    const TAudioFileResult result = (*reader)->read_file_frames(buffer, (*reader)->file_frame_count() * 3 / 5, 4800);
    char what[200];
    std::snprintf(what, sizeof(what), "a read that fails during a seek: the source's reason (%s)", result.error ? qPrintable(result.error->detail) : "no error");
    check(result.frames == 0 && result.error && result.error->code == TAudioFileError::Code::SeekFailed && result.error->detail == QString(Reason), what);
}

// The probes' source goes on to the decoder: the file is opened once. Where no
// decoder is found, the reason is the source's
static void test_one_source(const QTemporaryDir& dir)
{
    const QString path = dir.filePath(QStringLiteral("file.mp3"));
    Medium medium;
    int opens = 0;
    const TByteSourceOpener counting = [&](const QString& fileName) {
        ++opens;
        return memory_opener(&medium)(fileName);
    };
    check(TAudioReader::open(path, counting).has_value() && opens == 1, "one open of a file, for the probes and the decoder");

    medium.failFrom = 0;
    const auto refused = TAudioReader::open(path, memory_opener(&medium));
    check(!refused && refused.error().code == TAudioFileError::Code::CannotOpen && refused.error().detail == QString(Reason), "no decoder: the reason of the source");
    medium.failFrom = -1;

    const auto missing = TAudioReader::open(dir.filePath(QStringLiteral("not-there.mp3")), memory_opener(&medium));
    check(!missing && missing.error().code == TAudioFileError::Code::NotFound, "an opener that finds nothing: NotFound");
}

// A RIFF that is no WAVE and that libsndfile refuses: the MP3 probe does not walk
// it. It would reach the bytes that fail
static void test_riff_not_walked(const QTemporaryDir& dir)
{
    const QString path = dir.filePath(QStringLiteral("video.avi"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        check(false, "a RIFF that is no WAVE is written");
        return;
    }
    file.write(QByteArray("RIFF\xff\xff\xff\x00" "AVI ", 12) + QByteArray(4 * 1024 * 1024, 'x'));
    file.close();
    Medium medium;
    medium.failFrom = 1024 * 1024;
    const auto reader = TAudioReader::open(path, memory_opener(&medium));
    char what[200];
    std::snprintf(what, sizeof(what), "a RIFF that is no WAVE is refused without walking it (%s)", reader ? "it opened" : qPrintable(reader.error().detail));
    check(!reader && reader.error().detail != QString(Reason), what);
}

// The real file: the sources are QFile's, counted and on a medium
static void test_file_source(const QTemporaryDir& dir)
{
    const QString path = dir.filePath(QStringLiteral("file.wav"));
    auto reader = TAudioReader::open(path);
    if (!reader) {
        check(false, "a file opens through the file source");
        return;
    }
    const std::vector<float> all = read_all(**reader, 4099);
    const TAudioIOStats& stats = (*reader)->io_stats();
    check(all == read_with_sndfile(path), "the file source gives the file's samples");
    // The block of the open's last read holds the first of the audio
    const auto bytes = [](const TAudioIOCost& activity) { return activity.reads.cached.bytes + activity.reads.medium.bytes + activity.reads.unclassified.bytes; };
    check(bytes(stats.open) + bytes(stats.stream) >= uint64_t(Frames) * Channels * 2, "the file source counts its reads");
#if defined(Q_OS_UNIX)
    check(stats.mediumId != 0, "the file source knows its medium");
#endif
}

// Bytes past 2 and 4 GiB, read back from where they were written: a sparse
// file, in the build directory, /tmp can be small. A position cut to 32 bits
// reads other bytes there.
static void test_large_offsets()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/byte-source-XXXXXX"));
    const QString path = dir.filePath(QStringLiteral("sparse.bin"));
    const int64_t two = 2LL * 1024 * 1024 * 1024;
    const int64_t four = 2 * two;
    const int64_t size = four + two;
    const QByteArray below("below ");
    const QByteArray above("above ");
    QFile file(path);
    if (!dir.isValid() || !file.open(QIODevice::WriteOnly) || !file.resize(size)) {
        std::printf("no sparse file of 6 GiB here: large offsets not tested\n");
        return;
    }
    // Across 2 GiB, across 4 GiB, and at the end
    const std::vector<int64_t> places{two - below.size(), four - below.size(), size - below.size() - above.size()};
    for (const int64_t place : places) {
        if (!file.seek(place) || file.write(below + above) != below.size() + above.size()) {
            check(false, "a sparse file takes bytes past 2 GiB");
            return;
        }
    }
    file.close();

    std::unique_ptr<TByteSource> source = TByteSource::open_file(path).value_or(nullptr);
    if (!source) {
        check(false, "the sparse file opens");
        return;
    }
    check(source->size() == size, "the size of a file past 4 GiB");
    char bytes[32];
    const qsizetype length = below.size() + above.size();
    bool right = true;
    for (const int64_t place : places) {
        right = right && source->seek(place) && source->read(bytes, length) == length && QByteArray(bytes, length) == below + above &&
                source->position() == place + length;
    }
    check(right, "the bytes across 2 GiB, across 4 GiB and at the end, small reads");

    std::vector<char> large(size_t(2 * TByteSource::BlockBytes));
    const int64_t first = two - int64_t(large.size()) / 2;
    const bool whole = source->seek(first) && source->read(large.data(), int64_t(large.size())) == int64_t(large.size());
    check(whole && QByteArray(large.data() + large.size() / 2 - below.size(), length) == below + above, "the bytes across 2 GiB, a large read");
    check(source->seek(size) && source->at_end() && source->read(bytes, 4) == 0, "the end of a file past 4 GiB");
}

// A large file without audio goes through every probe: refused, without a
// crash. Zeros give the MP3 probe no sync at all, random bytes many false ones.
static void test_large_garbage()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/byte-source-XXXXXX"));
    const QString zeros = dir.filePath(QStringLiteral("zeros.bin"));
    const QString random = dir.filePath(QStringLiteral("random.bin"));
    QFile zeroFile(zeros);
    QFile randomFile(random);
    if (!dir.isValid() || !zeroFile.open(QIODevice::WriteOnly) || !zeroFile.resize(int64_t(256) * 1024 * 1024) ||
        !randomFile.open(QIODevice::WriteOnly)) {
        std::printf("no room for the garbage files: not tested\n");
        return;
    }
    zeroFile.close();
    std::mt19937 generator(71);
    QByteArray block(1024 * 1024, Qt::Uninitialized);
    for (int megabyte = 0; megabyte < 32; ++megabyte) {
        for (char& byte : block) {
            byte = char(generator());
        }
        randomFile.write(block);
    }
    randomFile.close();
    const auto refusedZeros = TAudioReader::open(zeros);
    const auto refusedRandom = TAudioReader::open(random);
    const QLatin1String noDecoder("no decoder reads this format");
    check(!refusedZeros && refusedZeros.error().detail.startsWith(noDecoder), "256 MiB of zeros: no decoder takes it, no crash");
    check(!refusedRandom && refusedRandom.error().detail.startsWith(noDecoder), "32 MiB of random bytes: no decoder takes it, no crash");
}

#if defined(Q_OS_LINUX)
// The page cache apart from the medium, on a real file system: in the build
// directory, /tmp can be tmpfs, which cannot tell them apart
static void test_cache_or_medium()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/byte-source-XXXXXX"));
    const QString path = dir.filePath(QStringLiteral("cold.bin"));
    const QByteArray bytes(1024 * 1024, 'x');
    QFile file(path);
    if (!dir.isValid() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush() || fsync(file.handle()) != 0 ||
        posix_fadvise(file.handle(), 0, 0, POSIX_FADV_DONTNEED) != 0) {
        check(false, "a file is written and leaves the page cache");
        return;
    }
    file.close();

    std::unique_ptr<TByteSource> source = TByteSource::open_file(path).value_or(nullptr);
    if (!source) {
        check(false, "the file opens");
        return;
    }
    TReadsByOrigin stats;
    source->count_reads_into(&stats);
    std::vector<char> into(size_t(bytes.size()));

    const bool read = source->read(into.data(), bytes.size()) == bytes.size();
    if (stats.unclassified.bytes > 0) {
        std::printf("this file system cannot tell the cache from the medium: not tested\n");
        return;
    }
    check(read && stats.medium.reads > 0 && stats.medium.bytes > 0, "a file out of the page cache waits for the medium");
    const TReadTotals medium = stats.medium;
    check(source->seek(0) && source->read(into.data(), bytes.size()) == bytes.size() && stats.medium.bytes == medium.bytes &&
              stats.cached.bytes >= uint64_t(bytes.size()),
          "the same bytes again come from the page cache, no medium");
    check(stats.unclassified.bytes == 0, "nothing is unclassified");
}
#endif

// A hybrid WavPack file reads exact with its correction file beside it, through
// a source of its own, and lossy without it
static void test_correction_file(const QTemporaryDir& dir)
{
    const QString wavpack = QStandardPaths::findExecutable(QStringLiteral("wavpack"));
    const QString wav = dir.filePath(QStringLiteral("hybrid.wav"));
    const QString hybrid = dir.filePath(QStringLiteral("hybrid.wv"));
    if (wavpack.isEmpty() || !write_sndfile(wav, SF_FORMAT_WAV | SF_FORMAT_PCM_16)) {
        std::printf("no wavpack program here: correction file not tested\n");
        return;
    }
    if (QProcess::execute(wavpack, {QStringLiteral("-q"), QStringLiteral("-y"), QStringLiteral("-b128"), QStringLiteral("-c"), wav, QStringLiteral("-o"), hybrid}) != 0 ||
        !QFile::exists(hybrid + QLatin1Char('c'))) {
        check(false, "the wavpack program writes a hybrid file with a correction file");
        return;
    }
    const std::vector<float> original = read_with_sndfile(wav);

    auto corrected = TAudioReader::open(hybrid);
    check(corrected && read_all(**corrected, 4099) == original, "a hybrid file with its correction file is exact");
    check(corrected && (*corrected)->io_stats().stream.reads.cached.bytes + (*corrected)->io_stats().stream.reads.medium.bytes + (*corrected)->io_stats().stream.reads.unclassified.bytes >
                           uint64_t(QFile(hybrid).size()) * 9 / 10,
          "the correction file's reads count too");

    // Each activity's reads land in that activity, for both sources: the decoder
    // points them at the activity (TAudioDecoder::set_activity()). A source left
    // behind would count its bytes in another activity, silently: with the
    // correction file the open and a seek read more than without it
    const auto total = [](const TReadsByOrigin& bytes) { return bytes.cached.bytes + bytes.medium.bytes + bytes.unclassified.bytes; };
    struct PhaseBytes
    {
        uint64_t open, seek, openAfter, streamAfterSeek;
    };
    const auto phases = [&](const QString& path) {
        PhaseBytes result{};
        auto reader = TAudioReader::open(path);
        if (!reader) {
            return result;
        }
        const TAudioIOStats& stats = (*reader)->io_stats();
        result.open = total(stats.open.reads);
        TAudioBuffer buffer;
        (void)(*reader)->read_file_frames(buffer, (*reader)->file_frame_count() / 2, 4096);
        result.seek = stats.seek.calls > 0 ? total(stats.seek.reads) : 0;
        // On past the block the seek read: the stream reads both files again
        const nframes_t after = (*reader)->file_frame_count() / 2 + 4096;
        (void)(*reader)->read_file_frames(buffer, after, std::min<nframes_t>(65536, (*reader)->file_frame_count() - after));
        result.openAfter = total(stats.open.reads);
        result.streamAfterSeek = total(stats.stream.reads);
        return result;
    };
    const PhaseBytes with = phases(hybrid);

    QFile::remove(hybrid + QLatin1Char('c'));
    auto lossy = TAudioReader::open(hybrid);
    check(lossy && read_all(**lossy, 4099) != original, "without it the file reads, lossy");

    const PhaseBytes without = phases(hybrid);
    check(with.open > without.open && without.open > 0, "the correction file's open reads count for the open");
    check(with.seek > without.seek && without.seek > 0, "the correction file's seek reads count for the seek");
    check(with.openAfter == with.open && without.openAfter == without.open, "a seek and a read add nothing to the open");
    check(with.streamAfterSeek > without.streamAfterSeek, "the correction file's reads after the seek count for the stream");
}

// After forget_position() the next read seeks, also to where the reader is,
// and gives what a fresh reader gives there, with a resampler too
static void test_forget_position(const QTemporaryDir& dir)
{
    const QString path = dir.filePath(QStringLiteral("file.wav"));
    const TTimeRef start(TTimeRef::UNIVERSAL_SAMPLE_RATE);
    for (const uint rate : {Rate, 44100u}) {
        char what[120];
        auto reader = TAudioReader::open(path);
        auto fresh = TAudioReader::open(path);
        if (!reader || !fresh || !(*reader)->set_output_rate_and_resampler(rate, TResamplerChoice{}) ||
            !(*fresh)->set_output_rate_and_resampler(rate, TResamplerChoice{})) {
            check(false, "the readers open at the rate");
            return;
        }
        TAudioBuffer first;
        TAudioBuffer again;
        (void)(*fresh)->read(first, start, 4096);
        const TTimeRef next = start + TTimeRef(nframes_t(4096), rate);
        (void)(*reader)->read(again, start, 4096);
        (void)(*reader)->read(again, next, 4096);
        const uint64_t seeks = (*reader)->io_stats().seek.calls;
        (void)(*reader)->read(again, next + TTimeRef(nframes_t(4096), rate), 16);
        std::snprintf(what, sizeof(what), "at %u Hz a read that goes on does not seek", rate);
        check((*reader)->io_stats().seek.calls == seeks, what);

        (*reader)->forget_position();
        (void)(*reader)->read(again, start, 4096);
        std::snprintf(what, sizeof(what), "at %u Hz forget_position() seeks", rate);
        check((*reader)->io_stats().seek.calls == seeks + 1, what);
        std::snprintf(what, sizeof(what), "at %u Hz it reads as a fresh reader", rate);
        check(std::equal(first.get_channel_buffer(0).get_data(4096), first.get_channel_buffer(0).get_data(4096) + 4096, again.get_channel_buffer(0).get_data(4096)), what);

        // To the very frame the decoder is at: a seek all the same
        const TTimeRef end = start + TTimeRef(nframes_t(4096), rate);
        (*reader)->forget_position();
        (void)(*reader)->read(again, end, 16);
        std::snprintf(what, sizeof(what), "at %u Hz also to the frame the reader is at", rate);
        check((*reader)->io_stats().seek.calls == seeks + 2, what);
    }
}

static void test_scenario(const QTemporaryDir& dir)
{
    const QString path = dir.filePath(QStringLiteral("signal.tscenario"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(R"(# tscenario 1
{"rate": 48000, "frames": 4800, "channels": [{"signal": "silence"}]})") < 0) {
        check(false, "a scenario is written");
        return;
    }
    file.close();
    auto reader = TAudioReader::open(path);
    if (!reader) {
        check(false, "a scenario opens");
        return;
    }
    TAudioBuffer buffer;
    (void)(*reader)->read_file_frames(buffer, 0, 4800);
    const TAudioIOCost& stream = (*reader)->io_stats().stream;
    check(stream.calls == 1 && stream.frames == 4800 && stream.read_nanoseconds() == 0, "a scenario reads no bytes: all its time is decoding");
}

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }

    test_source();

    const auto sndfile = [&](int format) { return [format](const QString& path) { return write_sndfile(path, format); }; };
    const auto ffmpeg = [&](QStringList encode) { return [&dir, encode](const QString& path) { return write_ffmpeg(path, dir, encode); }; };
    const std::vector<Format> formats = {
        {"WAV 16 bit", "wav", SF_FORMAT_WAV | SF_FORMAT_PCM_16, sndfile(SF_FORMAT_WAV | SF_FORMAT_PCM_16)},
        {"WAV float", "float.wav", SF_FORMAT_WAV | SF_FORMAT_FLOAT, sndfile(SF_FORMAT_WAV | SF_FORMAT_FLOAT)},
        {"AIFF 24 bit", "aiff", SF_FORMAT_AIFF | SF_FORMAT_PCM_24, sndfile(SF_FORMAT_AIFF | SF_FORMAT_PCM_24)},
        {"W64 24 bit", "w64", SF_FORMAT_W64 | SF_FORMAT_PCM_24, sndfile(SF_FORMAT_W64 | SF_FORMAT_PCM_24)},
        {"RF64 float", "rf64", SF_FORMAT_RF64 | SF_FORMAT_FLOAT, sndfile(SF_FORMAT_RF64 | SF_FORMAT_FLOAT)},
        {"CAF 24 bit", "caf", SF_FORMAT_CAF | SF_FORMAT_PCM_24, sndfile(SF_FORMAT_CAF | SF_FORMAT_PCM_24)},
        {"FLAC 24 bit", "flac", SF_FORMAT_FLAC | SF_FORMAT_PCM_24, sndfile(SF_FORMAT_FLAC | SF_FORMAT_PCM_24)},
        {"Ogg Vorbis", "ogg", SF_FORMAT_OGG | SF_FORMAT_VORBIS, sndfile(SF_FORMAT_OGG | SF_FORMAT_VORBIS)},
        {"Ogg Opus", "opus", SF_FORMAT_OGG | SF_FORMAT_OPUS, sndfile(SF_FORMAT_OGG | SF_FORMAT_OPUS)},
        // Through mpg123, not libsndfile: not compared with sf_open
        {"MP3", "mp3", 0, sndfile(SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III)},
        {"WavPack float", "wv", 0, write_wavpack},
#if defined(M4A_DECODE_SUPPORT)
        {"AAC in ADTS (ffmpeg)", "aac", 0, ffmpeg({QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-f"), QStringLiteral("adts")})},
        {"AAC in M4A (ffmpeg)", "m4a", 0, ffmpeg({QStringLiteral("-c:a"), QStringLiteral("aac")})},
#endif
    };
    for (const Format& format : formats) {
        test_format(dir, format);
    }
    for (const Format& format : formats) {
        test_read_failure(dir, format);
        test_open_failure(dir, format);
    }
    test_large_tag(dir);
    test_scan_failure(dir);
    test_seek_failure(dir);
    test_start_failure(dir);
    test_riff_not_walked(dir);
    test_one_source(dir);
    test_file_source(dir);
    test_large_offsets();
    test_large_garbage();
#if defined(Q_OS_LINUX)
    test_cache_or_medium();
#endif
    test_correction_file(dir);
    test_forget_position(dir);
    test_scenario(dir);

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all byte source tests passed\n");
    return 0;
}
