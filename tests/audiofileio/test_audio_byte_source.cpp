// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TAudioByteSource, the only way a decoder reaches its file.
// The source itself on bytes in memory: what a read gives, how it is counted.
// Then every format through a source in memory that waits a known time:
// the same samples as libsndfile reads from the file directly (sf_open), and
// the reads in the right kind of work (open, seek, stream), the medium's
// time apart from the decoding. A read that fails halfway gives ReadFailed
// with the source's reason in every decoder. WavPack's correction file is
// read through a source of its own. Bytes past 2 and 4 GiB come from where
// they are.

#include "TAudioBuffer.h"
#include "TAudioByteSource.h"
#include "TAudioReader.h"
#include "TAudioWriter.h"
#include "TResamplerChoice.h"
#include "TTimeRef.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

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
static constexpr quint64 Wait = 1000 * 1000;
static const QLatin1String Reason("the medium is gone");

// What the sources of one test share
struct Medium
{
    // Reads wait this long, and only then count as the medium's
    bool waits{false};
    // Reads from this byte on fail, -1: none do
    qint64 failFrom{-1};
};

// A file's bytes in memory
class MemorySource : public TAudioByteSource
{
public:
    MemorySource(QByteArray bytes, const Medium* medium) : m_bytes(std::move(bytes)), m_medium(medium) {}

    qint64 size() const override { return m_bytes.size(); }

protected:
    std::optional<qint64> read_cached(qint64, void*, qint64) override
    {
        if (m_medium->waits) {
            return 0;
        }
        return std::nullopt;
    }

    qint64 read_medium(qint64 position, void* data, qint64 bytes) override
    {
        if (m_medium->waits) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(Wait));
        }
        if (position >= m_bytes.size()) {
            return 0;
        }
        const qint64 end = std::min(position + bytes, qint64(m_bytes.size()));
        const qint64 good = m_medium->failFrom < 0 ? end : std::min(end, m_medium->failFrom);
        if (good < end) {
            read_failed(Reason);
        }
        if (good <= position) {
            return 0;
        }
        std::memcpy(data, m_bytes.constData() + position, size_t(good - position));
        return good - position;
    }

private:
    QByteArray m_bytes;
    const Medium* m_medium;
};

static TAudioSourceOpener memory_opener(const Medium* medium)
{
    return [medium](const QString& fileName) -> std::unique_ptr<TAudioByteSource> {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly)) {
            return nullptr;
        }
        return std::make_unique<MemorySource>(file.readAll(), medium);
    };
}

static void test_source()
{
    const Medium medium;
    MemorySource source(QByteArray("0123456789"), &medium);
    TAudioIOStats stats;
    source.set_stats(&stats);
    stats.phase = TAudioIOStats::Phase::Stream;

    char bytes[10];
    check(source.read(bytes, 4) == 4 && !std::memcmp(bytes, "0123", 4) && source.position() == 4, "a read gives its bytes and moves on");
    check(source.seek(8) && source.position() == 8 && !source.at_end(), "a seek moves the position");
    check(source.read(bytes, 4) == 2 && !std::memcmp(bytes, "89", 2) && source.at_end(), "a read over the end gives what is left");
    check(source.read(bytes, 4) == 0 && !source.take_error(), "a read at the end gives 0, no error");
    check(!source.seek(-1) && source.position() == 10, "a seek below 0 is refused");
    check(stats.stream.untold.reads == 1 && stats.stream.untold.bytes == 10 && stats.stream.medium.reads == 0 && stats.open.untold.reads == 0,
          "small reads share a block; without a word on the cache untold, in the current phase");

    const QByteArray large(3 * TAudioByteSource::BlockBytes, 'x');
    MemorySource big(large, &medium);
    TAudioIOStats bigStats;
    big.set_stats(&bigStats);
    std::vector<char> into(size_t(large.size()));
    check(big.read(into.data(), 2) == 2 && big.read(into.data() + 2, large.size() - 2) == large.size() - 2 && !std::memcmp(into.data(), large.constData(), size_t(large.size())),
          "a large read after a small one gives the block's bytes and the rest");
    check(bigStats.open.untold.bytes == quint64(large.size()), "no byte is read twice");
    check(big.seek(1) && big.read(into.data(), 4) == 4 && bigStats.open.untold.bytes == quint64(large.size()), "a seek back inside the block reads nothing");

    Medium failing{false, 5};
    MemorySource broken(QByteArray("0123456789"), &failing);
    check(broken.read(bytes, 8) == 5 && broken.take_error() == QString(Reason), "a read that fails halfway gives what it has, and why");
    check(!broken.take_error(), "why is told once");
    check(broken.read(bytes, 8) == -1 && broken.take_error() == QString(Reason), "a read that gives nothing and fails is -1");

    check(TAudioByteSource::open_file(QStringLiteral("/no/such/file")) == nullptr, "a file that does not open: nullptr");
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
    check(stats.open.medium.bytes > 0 && stats.seek.medium.reads == 0 && stats.stream.medium.reads == 0 && stats.open.nanoseconds >= stats.open.medium.nanoseconds, what);

    const quint64 fileBytes = quint64(QFile(path).size());
    TAudioBuffer buffer;
    const nframes_t step = 4800;
    nframes_t read = 0;
    for (nframes_t first = 0; first < (*reader)->file_frame_count(); first += step) {
        read += (*reader)->read_file_frames(buffer, first, step).frames;
    }
    const TAudioPhaseStats stream = stats.stream;
    std::snprintf(what, sizeof(what), "%s: a read to the end counts every frame and goes through the source", format.name);
    check(read == (*reader)->file_frame_count() && stream.frames == read && stream.calls > 0 && stream.medium.reads > 0 && stream.medium.bytes <= fileBytes, what);
    std::snprintf(what, sizeof(what), "%s: the medium's time is what its reads waited, the decoding the rest", format.name);
    check(stream.medium.nanoseconds >= stream.medium.reads * Wait && stream.medium.worstNanoseconds >= Wait &&
              stream.nanoseconds >= stream.read_nanoseconds() && stream.decode_nanoseconds() == stream.nanoseconds - stream.read_nanoseconds(),
          what);

    // A seek back, to a place that has to be found again
    (*reader)->read_file_frames(buffer, (*reader)->file_frame_count() / 3, step);
    std::snprintf(what, sizeof(what), "%s: a seek counts apart, the read after it as stream", format.name);
    check(stats.seek.calls == 1 && stats.seek.nanoseconds >= stats.seek.read_nanoseconds() && stats.stream.calls == stream.calls + 1, what);
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
    const auto bytes = [](const TAudioPhaseStats& phase) { return phase.cached.bytes + phase.medium.bytes + phase.untold.bytes; };
    check(bytes(stats.open) + bytes(stats.stream) >= quint64(Frames) * Channels * 2, "the file source counts its reads");
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
    const qint64 two = 2LL * 1024 * 1024 * 1024;
    const qint64 four = 2 * two;
    const qint64 size = four + two;
    const QByteArray below("below ");
    const QByteArray above("above ");
    QFile file(path);
    if (!dir.isValid() || !file.open(QIODevice::WriteOnly) || !file.resize(size)) {
        std::printf("no sparse file of 6 GiB here: large offsets not tested\n");
        return;
    }
    // Across 2 GiB, across 4 GiB, and at the end
    const std::vector<qint64> places{two - below.size(), four - below.size(), size - below.size() - above.size()};
    for (const qint64 place : places) {
        if (!file.seek(place) || file.write(below + above) != below.size() + above.size()) {
            check(false, "a sparse file takes bytes past 2 GiB");
            return;
        }
    }
    file.close();

    std::unique_ptr<TAudioByteSource> source = TAudioByteSource::open_file(path);
    if (!source) {
        check(false, "the sparse file opens");
        return;
    }
    check(source->size() == size, "the size of a file past 4 GiB");
    char bytes[32];
    const qsizetype length = below.size() + above.size();
    bool right = true;
    for (const qint64 place : places) {
        right = right && source->seek(place) && source->read(bytes, length) == length && QByteArray(bytes, length) == below + above &&
                source->position() == place + length;
    }
    check(right, "the bytes across 2 GiB, across 4 GiB and at the end, small reads");

    std::vector<char> large(size_t(2 * TAudioByteSource::BlockBytes));
    const qint64 first = two - qint64(large.size()) / 2;
    const bool whole = source->seek(first) && source->read(large.data(), qint64(large.size())) == qint64(large.size());
    check(whole && QByteArray(large.data() + large.size() / 2 - below.size(), length) == below + above, "the bytes across 2 GiB, a large read");
    check(source->seek(size) && source->at_end() && source->read(bytes, 4) == 0, "the end of a file past 4 GiB");
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

    std::unique_ptr<TAudioByteSource> source = TAudioByteSource::open_file(path);
    if (!source) {
        check(false, "the file opens");
        return;
    }
    TAudioIOStats stats;
    source->set_stats(&stats);
    std::vector<char> into(size_t(bytes.size()));

    const bool read = source->read(into.data(), bytes.size()) == bytes.size();
    if (stats.open.untold.bytes > 0) {
        std::printf("this file system cannot tell the cache from the medium: not tested\n");
        return;
    }
    check(read && stats.open.medium.reads > 0 && stats.open.medium.bytes > 0, "a file out of the page cache waits for the medium");
    const TAudioTransfer medium = stats.open.medium;
    check(source->seek(0) && source->read(into.data(), bytes.size()) == bytes.size() && stats.open.medium.bytes == medium.bytes &&
              stats.open.cached.bytes >= quint64(bytes.size()),
          "the same bytes again come from the page cache, no medium");
    check(stats.open.untold.bytes == 0, "nothing is untold");
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
    check(corrected && (*corrected)->io_stats().stream.cached.bytes + (*corrected)->io_stats().stream.medium.bytes + (*corrected)->io_stats().stream.untold.bytes >
                           quint64(QFile(hybrid).size()) * 9 / 10,
          "the correction file's reads count too");

    QFile::remove(hybrid + QLatin1Char('c'));
    auto lossy = TAudioReader::open(hybrid);
    check(lossy && read_all(**lossy, 4099) != original, "without it the file reads, lossy");
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
        const quint64 seeks = (*reader)->io_stats().seek.calls;
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
        file.write(R"({"version": 1, "rate": 48000, "frames": 4800, "channels": [{"signal": "silence"}]})") < 0) {
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
    (*reader)->read_file_frames(buffer, 0, 4800);
    const TAudioPhaseStats& stream = (*reader)->io_stats().stream;
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
        // Through MAD, not libsndfile: not compared with sf_open
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
    }
    test_file_source(dir);
    test_large_offsets();
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
