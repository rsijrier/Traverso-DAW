// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TPeakBuildTask and TPeakStream: a build in slices writes
// the same peak file as appending everything at once, whatever the slice
// sizes, also for more channels than stereo; sources that end early, fail
// or are cancelled leave no valid file behind; a recording or a build read while
// it writes level 0 shows the right peaks; a write failure in the middle of
// a recording leaves no file. Peaks of an audio file through the reader and
// its decoder (a scenario as the file) equal those of the same samples.

#include "TPeakBuildTask.h"
#include "TPeakFile.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"
#include "TWaveformColumns.h"
#include "test_seed.h"
#include "test_signals.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <sys/resource.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <random>
#include <thread>
#include <vector>

static int s_failures = 0;

#define CHECK(condition, ...) \
    do { \
        if (!(condition)) { \
            if (++s_failures <= 20) { \
                std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #condition); \
                std::printf(__VA_ARGS__); \
                std::printf("\n"); \
            } \
        } \
    } while (0)

static std::mt19937_64 s_random(20261018);

static qint64 random_between(qint64 low, qint64 high)
{
    return std::uniform_int_distribution<qint64>(low, high)(s_random);
}

struct Signal
{
    uint rate{48000};
    std::vector<std::vector<float>> channels;
    qint64 frames() const { return channels.empty() ? 0 : qint64(channels.front().size()); }
};

static Signal make_signal(int channelCount, qint64 frames)
{
    Signal signal;
    std::uniform_real_distribution<float> noise(-1.2f, 1.2f);
    for (int c = 0; c < channelCount; ++c) {
        std::vector<float> samples(static_cast<size_t>(frames));
        for (qint64 n = 0; n < frames; ++n) {
            samples[size_t(n)] = float(std::sin(double(n) * 0.001 * (c + 1))) * 0.5f + noise(s_random) * 0.3f;
        }
        signal.channels.push_back(std::move(samples));
    }
    return signal;
}

// A source in memory that can report more frames than it has and fail at a
// frame. A read gives the frames asked, fewer only at the end of the signal:
// the contract of TPeakBuildSource (and of the decoders behind it).
class MemoryBuildSource final : public TPeakBuildSource
{
public:
    MemoryBuildSource(const Signal& signal, qint64 extraReported, qint64 failAt)
        : m_signal(signal), m_extraReported(extraReported), m_failAt(failAt) {}

    uint rate() const override { return m_signal.rate; }
    int channel_count() const override { return int(m_signal.channels.size()); }
    qint64 frame_count() const override { return m_signal.frames() + m_extraReported; }

    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override
    {
        if (m_failAt >= 0 && firstFrame + count > m_failAt) {
            return -1;
        }
        const qint64 frames = std::max<qint64>(0, std::min(count, m_signal.frames() - firstFrame));
        for (size_t c = 0; c < m_signal.channels.size(); ++c) {
            std::copy_n(m_signal.channels[c].data() + firstFrame, frames, channels[c]);
        }
        return frames;
    }

private:
    const Signal& m_signal;
    qint64 m_extraReported;
    qint64 m_failAt;
};

static TPeakBuildTask::OpenSource memory_source(const Signal& signal, qint64 extraReported = 0, qint64 failAt = -1)
{
    return [&signal, extraReported, failAt](QString*) -> std::unique_ptr<TPeakBuildSource> {
        return std::make_unique<MemoryBuildSource>(signal, extraReported, failAt);
    };
}

// Steps the task to its end, as DiskIO does
static TPeakBuildTask::State run(TPeakBuildTask& task, int cancelAfterSteps = -1)
{
    int steps = 0;
    while (task.step()) {
        if (++steps == cancelAfterSteps) {
            task.cancel();
        }
    }
    return task.state();
}

// The stream a build of signal writes into
static std::shared_ptr<TPeakStream> stream_for(const Signal& signal, const QString& peakPath)
{
    return std::make_shared<TPeakStream>(signal.rate, int(signal.channels.size()), peakPath);
}

static QByteArray file_bytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Appends the frames [from, to) of signal in pieces of at most piece frames
static void record_into(TPeakStream& stream, const Signal& signal, qint64 from, qint64 to, qint64 piece = 1024)
{
    for (qint64 position = from; position < to;) {
        const qint64 count = std::min(piece, to - position);
        std::vector<const float*> pointers;
        for (const auto& channel : signal.channels) {
            pointers.push_back(channel.data() + position);
        }
        stream.append(pointers.data(), count);
        position += count;
    }
}

// The peak file of signal, appended at once and post-processed
static QByteArray reference_file(const QString& path, const Signal& signal, const QString& audioPath)
{
    QFile::remove(path);
    TPeakStream stream(signal.rate, int(signal.channels.size()), path);
    CHECK(stream.open() == TPeakFile::Status::Ok, "reference open");
    record_into(stream, signal, 0, signal.frames(), signal.frames());
    CHECK(stream.finish(audioPath) == TPeakFile::Status::Ok, "reference finish");
    while (stream.post_process()) {
    }
    CHECK(stream.published(), "reference: %s", qPrintable(stream.error()));
    return file_bytes(path);
}

// The samples of a signal for TWaveformColumns (max_amplitude's ends)
class SignalSource final : public TWaveformSampleSource
{
public:
    explicit SignalSource(const Signal& signal) : m_signal(signal) {}

    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override
    {
        const qint64 available = std::max<qint64>(0, std::min(count, m_signal.frames() - firstFrame));
        for (size_t c = 0; c < m_signal.channels.size(); ++c) {
            std::copy_n(m_signal.channels[c].data() + firstFrame, available, channels[c]);
        }
        return available;
    }

private:
    const Signal& m_signal;
};

// Columns from level 0 alone equal the finished pyramid's: min and max
// exactly, the RMS within a step
static bool same_column(const TWaveformColumn& live, const TWaveformColumn& finished)
{
    const double step = TPeakCodeMath::octave_fraction(1) * 1.000001;
    if (live.is_empty() || finished.is_empty()) {
        return live.is_empty() == finished.is_empty();
    }
    const bool silent = live.rms <= TPeakLevels::to_value(1) && finished.rms <= TPeakLevels::to_value(1);
    return live.min == finished.min && live.max == finished.max
           && (silent || (live.rms <= finished.rms * step && live.rms >= finished.rms / step));
}

// Stress test of what threads share: one thread records in buffers of 256
// frames (or builds from a decoder, in steps), the GUI thread meanwhile
// reads level 0 below the count and queries columns at changing zoom, with
// RMS and max_amplitude. Every column of blocks that are there equals the
// finished pyramid's, the count never goes back, and the finished file
// answers like the reference.
static void stress_stream(const TestSeed& seed, const QString& peakPath, const QString& audioPath, const QString& referencePath, bool build)
{
    const Signal signal = make_signal(2, 1024 * 400 + 333);
    reference_file(referencePath, signal, audioPath);
    TPeakFile reference;
    const auto source = TPeakFile::SourceInfo::from_file(audioPath, signal.rate, 2, signal.frames());
    CHECK(reference.open(referencePath, source) == TPeakFile::Status::Ok, "stress: reference");
    const TPeakLevels& expected = reference.levels();
    const double step = TPeakCodeMath::octave_fraction(1);

    QFile::remove(peakPath);
    const std::shared_ptr<TPeakStream> stream = stream_for(signal, peakPath);
    TPeakBuildTask task(memory_source(signal), audioPath, stream, 1000);
    if (!build) {
        CHECK(stream->open() == TPeakFile::Status::Ok, "stress: open");
    }
    std::atomic<bool> done{false};
    std::thread writer([&] {
        std::mt19937_64 random(seed(42));
        if (build) {
            // Level 0 and post-processing, as DiskIO steps it
            while (task.step()) {
                if (std::uniform_int_distribution<int>(0, 30)(random) == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            }
            CHECK(task.state() == TPeakBuildTask::State::Done, "stress: build %s", qPrintable(task.error()));
            done.store(true);
            return;
        }
        for (qint64 position = 0; position < signal.frames(); position += 256) {
            record_into(*stream, signal, position, std::min<qint64>(position + 256, signal.frames()));
            if (std::uniform_int_distribution<int>(0, 30)(random) == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        CHECK(stream->finish(audioPath) == TPeakFile::Status::Ok, "stress: finish");
        done.store(true);
    });

    const qint64 widths[] = {64, 64 * 3, 64 * 16, 64 * 100 + 7, 64 * 1000};
    std::vector<TWaveformColumn> got(400);
    std::vector<TWaveformColumn> want(400);
    qint64 lastFrames = 0;
    int reads = 0;
    int compared = 0;
    bool failed = false;
    SignalSource samples(signal);
    while ((!done.load() || reads == 0) && !failed) {
        const qint64 frames = stream->written_frames();
        const qint64 blocks = (frames + TPeakLevels::BlockFrames - 1) / TPeakLevels::BlockFrames;
        if (frames < lastFrames) {
            CHECK(false, "stress read %d: %lld frames after %lld", reads, (long long)frames, (long long)lastFrames);
            break;
        }
        lastFrames = frames;
        ++reads;
        if (blocks == 0) {
            continue;
        }
        const qint64 columnFrames = widths[random_between(0, 4)];
        const TTimeRef width(uint64_t(columnFrames), signal.rate);
        const int count = int(got.size()) / 2;
        const qint64 startFrame = random_between(0, blocks * TPeakLevels::BlockFrames - 1);
        const TTimeRef start(uint64_t(startFrame), signal.rate);
        // What is written, as the waveform reads it
        stream->read([&](const TPeakLevels& view) {
        const qint64 have = view.block_count(0);
        TWaveformColumns::query(view, nullptr, start, width, count, got.data());
        TWaveformColumns::query(expected, nullptr, start, width, count, want.data());
        for (int c = 0; c < 2 && !failed; ++c) {
            for (int i = 0; i < count; ++i) {
                // Only columns that have all their blocks
                if (TTimeRef::frame_at_or_after(start + width * double(i + 1), signal.rate) > have * TPeakLevels::BlockFrames) {
                    break;
                }
                const TWaveformColumn& a = got[size_t(c * count + i)];
                const TWaveformColumn& b = want[size_t(c * count + i)];
                if (!same_column(a, b)) {
                    CHECK(false, "stress read %d: column %d of %lld frames: %g..%g rms %g, expected %g..%g rms %g", reads, i,
                          (long long)columnFrames, a.min, a.max, a.rms, b.min, b.max, b.rms);
                    failed = true;
                    break;
                }
                ++compared;
            }
        }
        });

        // Now and then max_amplitude over a stretch of what is recorded
        if (reads % 16 == 0) {
            stream->read([&](const TPeakLevels& all) {
            // After finish() the last block may be partial: the frames end first
            const qint64 recorded = std::min(all.block_count(0) * TPeakLevels::BlockFrames, all.frame_count());
            const qint64 first = random_between(0, recorded - 1);
            const qint64 last = std::min(first + random_between(1, 30000), recorded);
            float real = 0.0f;
            for (qint64 n = first; n < last; ++n) {
                real = std::max({real, std::fabs(signal.channels[0][size_t(n)]), std::fabs(signal.channels[1][size_t(n)])});
            }
            const float amplitude = TWaveformColumns::max_amplitude(all, &samples, first, last);
            if (amplitude < real || amplitude > real * float(step) * 1.000001f) {
                CHECK(false, "stress read %d: max_amplitude %g, real %g", reads, amplitude, real);
                failed = true;
            }
            });
        }
    }
    writer.join();
    CHECK(compared > 1000, "stress: only %d columns compared in %d reads", compared, reads);
    std::printf("    stress (%s): %d reads, %d columns compared\n", build ? "build" : "recording", reads, compared);

    if (!build) {
        // A recording's post-processing: a task of its own
        TPeakBuildTask post(stream);
        CHECK(run(post) == TPeakBuildTask::State::Done, "stress: post-processing %s", qPrintable(post.error()));
    }
    CHECK(file_bytes(peakPath) == file_bytes(referencePath), "stress: the file differs from the reference");
}

// A scenario file (TScenarioDecoder) with these channels and faults
static QString write_scenario(const QString& path, uint rate, qint64 frames, const char* channels, const char* faults = "[]")
{
    const QByteArray json = QStringLiteral(R"(# tscenario 1
{"rate": %1, "frames": %2, "channels": %3, "faults": %4})")
                                .arg(rate)
                                .arg(frames)
                                .arg(QLatin1String(channels), QLatin1String(faults))
                                .toUtf8();
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly) && file.write(json) == json.size(), "scenario %s", qPrintable(path));
    return path;
}

// Peaks of an audio file, as the app builds them: TPeakBuildTask::audio_file()
// through TAudioReader and the decoder, with a scenario as the file. The same
// signal gives the same file as from memory; an impulse lands in its block;
// a damaged part, an early end and NaN as the decoder contract says.
static void test_audio_file_builds(const QTemporaryDir& dir, const QString& referencePath, const QString& peakPath)
{
    const uint rate = 48000;

    // The same signal from the file and from memory: the same peak file
    {
        const qint64 frames = 300000 + 77;
        const QString path = write_scenario(dir.filePath("signals.tscenario"), rate, frames,
                                            R"([{"signal": "sine", "frequency": 441, "amplitude": 0.5},
                                                {"signal": "sweep", "mode": "exponential", "start": 20, "end": 20000, "amplitude": 0.9}])");
        Signal signal;
        signal.channels.assign(2, std::vector<float>(size_t(frames)));
        for (qint64 n = 0; n < frames; ++n) {
            signal.channels[0][size_t(n)] = TestSignals::sine(nframes_t(n), rate, 441, 0.5);
            signal.channels[1][size_t(n)] = TestSignals::sweep_exponential(nframes_t(n), rate, nframes_t(frames), 20, 20000, 0.9);
        }
        const QByteArray reference = reference_file(referencePath, signal, path);
        TPeakBuildTask task(TPeakBuildTask::audio_file(path), path, stream_for(signal, peakPath));
        CHECK(run(task) == TPeakBuildTask::State::Done, "audio file: %s", qPrintable(task.error()));
        CHECK(file_bytes(peakPath) == reference, "audio file: the peak file differs from the one of the same signal in memory");
    }

    // An impulse: one level-0 block holds it, every other block is silence
    {
        const qint64 frames = 200000;
        const qint64 at = 123457;
        const QString path = write_scenario(dir.filePath("impulse.tscenario"), rate, frames,
                                            R"([{"signal": "impulse", "frame": 123457, "amplitude": 0.8}])");
        TPeakBuildTask task(TPeakBuildTask::audio_file(path), path, std::make_shared<TPeakStream>(rate, 1, peakPath));
        CHECK(run(task) == TPeakBuildTask::State::Done, "impulse: %s", qPrintable(task.error()));
        TPeakFile file;
        CHECK(file.open(peakPath, TPeakFile::SourceInfo::from_file(path, rate, 1, frames)) == TPeakFile::Status::Ok, "impulse: the file opens");
        const TPeakLevels& levels = file.levels();
        const qint64 impulseBlock = at >> TPeakLevels::BlockFramesShift;
        bool placed = levels.block_count(0) == (frames + TPeakLevels::BlockFrames - 1) / TPeakLevels::BlockFrames;
        for (qint64 index = 0; placed && index < levels.block_count(0); ++index) {
            const TPeakBlock block = levels.block(0, 0, index);
            placed = index == impulseBlock ? block.max > 0 && block.min == 0 : block.min == 0 && block.max == 0;
        }
        CHECK(placed, "impulse: only block %lld of level 0 holds it", (long long)impulseBlock);
        file.close();
    }

    // Audio that ends before the frame count: the pyramid ends there and the
    // file opens for the reported length (contract point 4)
    {
        const qint64 frames = 200000;
        const QString path = write_scenario(dir.filePath("end.tscenario"), rate, frames, R"([{"signal": "sine", "frequency": 441, "amplitude": 0.5}])",
                                            R"([{"kind": "end", "frame": 150000}])");
        TPeakBuildTask task(TPeakBuildTask::audio_file(path), path, std::make_shared<TPeakStream>(rate, 1, peakPath));
        CHECK(run(task) == TPeakBuildTask::State::Done, "early end: %s", qPrintable(task.error()));
        TPeakFile file;
        CHECK(file.open(peakPath, TPeakFile::SourceInfo::from_file(path, rate, 1, frames)) == TPeakFile::Status::Ok
                  && file.levels().frame_count() == 150000,
              "early end: the pyramid ends where the audio does (%lld)", (long long)file.levels().frame_count());
        file.close();
    }

    // A damaged part: the build stops there, the audio after it gets no
    // waveform. What it should do is open (work order O1): KNOWN until
    // decided, a failure once it changes, to update this entry.
    {
        const qint64 frames = 200000;
        const QString path = write_scenario(dir.filePath("damaged.tscenario"), rate, frames, R"([{"signal": "sine", "frequency": 441, "amplitude": 0.5}])",
                                            R"([{"kind": "read-error", "frame": 100000, "frames": 1000}])");
        TPeakBuildTask task(TPeakBuildTask::audio_file(path), path, std::make_shared<TPeakStream>(rate, 1, peakPath));
        CHECK(run(task) == TPeakBuildTask::State::Done, "damaged part: %s", qPrintable(task.error()));
        TPeakFile file;
        const bool opened = file.open(peakPath, TPeakFile::SourceInfo::from_file(path, rate, 1, frames)) == TPeakFile::Status::Ok;
        const qint64 built = opened ? file.levels().frame_count() : -1;
        if (built == 100000) {
            std::printf("damaged part: the pyramid ends at the damaged part (frame 100000 of 200000): KNOWN (work order O1)\n");
        } else {
            CHECK(false, "damaged part: the pyramid holds %lld frames, the KNOWN entry (O1) is out of date", (long long)built);
        }
        file.close();
    }

    // NaN and inf: through the file the same peak file as the same samples
    // from memory (the stream leaves them out of min and max)
    {
        const qint64 frames = 100000;
        const QString path = write_scenario(dir.filePath("values.tscenario"), rate, frames, R"([{"signal": "sine", "frequency": 441, "amplitude": 0.5}])",
                                            R"([{"kind": "value", "frame": 5000, "channel": 0, "value": "nan"},
                                                {"kind": "value", "frame": 9000, "channel": 0, "value": "inf"}])");
        Signal signal;
        signal.channels.assign(1, std::vector<float>(size_t(frames)));
        for (qint64 n = 0; n < frames; ++n) {
            signal.channels[0][size_t(n)] = TestSignals::sine(nframes_t(n), rate, 441, 0.5);
        }
        signal.channels[0][5000] = std::numeric_limits<float>::quiet_NaN();
        signal.channels[0][9000] = std::numeric_limits<float>::infinity();
        const QByteArray reference = reference_file(referencePath, signal, path);
        TPeakBuildTask task(TPeakBuildTask::audio_file(path), path, stream_for(signal, peakPath));
        CHECK(run(task) == TPeakBuildTask::State::Done, "NaN and inf: %s", qPrintable(task.error()));
        CHECK(file_bytes(peakPath) == reference, "NaN and inf: the peak file differs from the one of the same samples in memory");
    }
}

int main(int argc, char** argv)
{
    const TestSeed seed(argc, argv, 20261018);
    s_random.seed(seed.seed());
    QTemporaryDir dir;
    CHECK(dir.isValid(), "no temporary directory");
    // Any file stands in for the audio file: its size and time go into the header
    const QString audioPath = dir.filePath("source.wav");
    {
        QFile audio(audioPath);
        CHECK(audio.open(QIODevice::WriteOnly) && audio.write("audio") == 5, "audio stand-in");
    }
    const QString referencePath = dir.filePath("reference.peaks");
    const QString peakPath = dir.filePath("sub/dir/built.peaks");   // the stream creates the directory

    // Slices do not change the file
    for (int round = 0; round < 40; ++round) {
        const qint64 frames = random_between(0, 1) ? random_between(1, 300000) : (qint64(64) << random_between(0, 14)) + random_between(-2, 2);
        if (frames <= 0) {
            continue;
        }
        const Signal signal = make_signal(int(random_between(1, 2)), frames);
        const QByteArray reference = reference_file(referencePath, signal, audioPath);

        // Sometimes the default slice, a fifth of a second
        const qint64 readFrames = random_between(0, 3) == 0 ? 0 : random_between(64, 40000);
        TPeakBuildTask task(memory_source(signal), audioPath, stream_for(signal, peakPath), readFrames);
        CHECK(run(task) == TPeakBuildTask::State::Done, "round %d: %s", round, qPrintable(task.error()));
        CHECK(task.progress() == 100, "round %d: progress %d", round, task.progress());
        CHECK(file_bytes(peakPath) == reference, "round %d frames %lld: file differs from the one-go file", round, (long long)frames);
        CHECK(!QFile::exists(task.stream()->part_path()), "round %d: part file left", round);
    }

    // More channels than stereo: built like any other
    {
        const Signal six = make_signal(6, 50000);
        const QByteArray reference = reference_file(referencePath, six, audioPath);
        TPeakBuildTask task(memory_source(six), audioPath, stream_for(six, peakPath), 4096);
        CHECK(run(task) == TPeakBuildTask::State::Done, "six channels: %s", qPrintable(task.error()));
        CHECK(file_bytes(peakPath) == reference, "six channels: file differs from the one-go file");
    }

    // A source that ends before the length it reports (MP3): the file holds
    // what was read and opens for the reported length
    {
        const Signal signal = make_signal(2, 100000);
        TPeakBuildTask task(memory_source(signal, 1152), audioPath, stream_for(signal, peakPath));
        CHECK(run(task) == TPeakBuildTask::State::Done, "ends early: %s", qPrintable(task.error()));
        TPeakFile file;
        const auto expected = TPeakFile::SourceInfo::from_file(audioPath, signal.rate, 2, signal.frames() + 1152);
        CHECK(file.open(peakPath, expected) == TPeakFile::Status::Ok && file.levels().frame_count() == signal.frames(),
              "ends early: the file does not open for the reported length");
        file.close();
        CHECK(file_bytes(peakPath) == reference_file(referencePath, signal, audioPath), "ends early: file differs");
    }

    // A recording: the writer thread appends level 0 to the part file while
    // the GUI thread reads it; every block read is right, finish() leaves the
    // file in its part file, its post-processing gives the one-go file
    for (int round = 0; round < 6; ++round) {
        const Signal signal = make_signal(int(random_between(1, 2)), random_between(200000, 600000));
        const QByteArray reference = reference_file(referencePath, signal, audioPath);
        TPeakFile expectedFile;
        CHECK(expectedFile.open(referencePath, TPeakFile::SourceInfo::from_file(audioPath, signal.rate, int(signal.channels.size()),
                                                                                signal.frames())) == TPeakFile::Status::Ok,
              "recording %d: reference", round);
        const TPeakLevels& expected = expectedFile.levels();

        QFile::remove(peakPath);
        const std::shared_ptr<TPeakStream> recording = stream_for(signal, peakPath);
        CHECK(recording->open() == TPeakFile::Status::Ok, "recording %d: open", round);
        std::atomic<bool> done{false};
        TPeakFile::Status finished = TPeakFile::Status::Error;
        std::thread writer([&] {
            std::mt19937_64 random(seed(round));
            qint64 position = 0;
            while (position < signal.frames()) {
                const qint64 count = std::min<qint64>(std::uniform_int_distribution<qint64>(1, 3000)(random), signal.frames() - position);
                record_into(*recording, signal, position, position + count, count);
                position += count;
                if (std::uniform_int_distribution<int>(0, 50)(random) == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
            }
            finished = recording->finish(audioPath);
            done.store(true);
        });

        int reads = 0;
        bool wrong = false;
        while ((!done.load() || reads == 0) && !wrong) {
            ++reads;
            // Every level of the complete chunks, level 0 after them
            recording->read([&](const TPeakLevels& view) {
            for (int level = 0; level < view.level_count() && !wrong; ++level) {
                for (int c = 0; c < view.channel_count() && !wrong; ++c) {
                    for (qint64 k = 0; k < view.block_count(level); k += 1 + k / 7) {
                        if (view.block(level, c, k).min != expected.block(level, c, k).min
                                || view.block(level, c, k).max != expected.block(level, c, k).max
                                || view.rms(level, c, k) != expected.rms(level, c, k)) {
                            wrong = true;
                            CHECK(false, "recording %d: level %d channel %d block %lld differs in read %d", round, level, c,
                                  (long long)k, reads);
                            break;
                        }
                    }
                }
            }
            });
        }
        writer.join();
        CHECK(finished == TPeakFile::Status::Ok, "recording %d: finish", round);
        CHECK(QFile::exists(recording->part_path()) && !QFile::exists(peakPath),
              "recording %d: finished but not post-processed, the file is in its part file", round);
        TPeakBuildTask post(recording);
        CHECK(run(post) == TPeakBuildTask::State::Done, "recording %d: post-processing %s", round, qPrintable(post.error()));
        CHECK(file_bytes(peakPath) == reference, "recording %d: the file differs from the one-go file", round);
        CHECK(!QFile::exists(recording->part_path()), "recording %d: part file left", round);
        // After the rename the stream reads from the file's name
        recording->read([&](const TPeakLevels& last) {
        CHECK(last.block_count(0) == expected.block_count(0) && last.frame_count() == expected.frame_count()
                  && last.block(0, 0, last.block_count(0) - 1).max == expected.block(0, 0, last.block_count(0) - 1).max,
              "recording %d: level 0 after the rename", round);
        });
        std::printf("    recording of %lld frames: %d reads\n", (long long)signal.frames(), reads);
    }

    stress_stream(seed, peakPath, audioPath, referencePath, false);
    stress_stream(seed, peakPath, audioPath, referencePath, true);

    // A build read as it goes, as a waveform shows it growing: after every
    // step the columns over what is built are the finished pyramid's, the
    // columns past it are empty; finished, the file gives the same columns
    {
        const Signal signal = make_signal(2, 300000 + 77);
        reference_file(referencePath, signal, audioPath);
        TPeakFile whole;
        CHECK(whole.open(referencePath, TPeakFile::SourceInfo::from_file(audioPath, signal.rate, 2, signal.frames())) == TPeakFile::Status::Ok,
              "growing build: reference");
        QFile::remove(peakPath);
        const std::shared_ptr<TPeakStream> stream = stream_for(signal, peakPath);
        TPeakBuildTask task(memory_source(signal), audioPath, stream, 20000);
        const qint64 columnFrames = 1024;
        const TTimeRef width(uint64_t(columnFrames), signal.rate);
        const int count = int((signal.frames() + columnFrames - 1) / columnFrames);
        std::vector<TWaveformColumn> got(size_t(count) * 2);
        std::vector<TWaveformColumn> want(size_t(count) * 2);
        TWaveformColumns::query(whole.levels(), nullptr, TTimeRef(), width, count, want.data());
        int steps = 0;
        int growing = 0;
        bool right = true;
        qint64 lastBuilt = 0;
        while (task.step() && right) {
            ++steps;
            stream->read([&](const TPeakLevels& view) {
            const qint64 built = view.block_count(0) * TPeakLevels::BlockFrames;
            right = right && built >= lastBuilt;
            if (built > lastBuilt) {
                ++growing;
            }
            lastBuilt = built;
            TWaveformColumns::query(view, nullptr, TTimeRef(), width, count, got.data());
            for (int c = 0; c < 2 && right; ++c) {
                for (int i = 0; i < count && right; ++i) {
                    const TWaveformColumn& a = got[size_t(c * count + i)];
                    if ((i + 1) * columnFrames <= built) {
                        right = !a.is_empty() && same_column(a, want[size_t(c * count + i)]);
                    } else if (i * columnFrames >= built) {
                        right = a.is_empty();
                    }
                    if (!right) {
                        CHECK(false, "growing build, step %d: column %d of channel %d with %lld frames built", steps, i, c,
                              (long long)built);
                    }
                }
            }
            });
        }
        CHECK(task.state() == TPeakBuildTask::State::Done, "growing build: %s", qPrintable(task.error()));
        CHECK(growing > 5, "growing build: the waveform grew only %d times in %d steps", growing, steps);
        CHECK(file_bytes(peakPath) == file_bytes(referencePath), "growing build: the file differs from the reference");
    }

    // Writing fails in the middle of a recording (a full disk; here the
    // process's file size limit): the stream stops, the count freezes, what
    // was written stays readable until the stop, and the stop leaves no file
#if defined(Q_OS_UNIX)
    {
        const Signal signal = make_signal(2, 2000000);
        QFile::remove(peakPath);
        TPeakStream recording(signal.rate, 2, peakPath);
        CHECK(recording.open() == TPeakFile::Status::Ok, "write failure: open");
        std::signal(SIGXFSZ, SIG_IGN);
        rlimit limit{};
        getrlimit(RLIMIT_FSIZE, &limit);
        const rlimit before = limit;
        limit.rlim_cur = 64 * 1024;
        setrlimit(RLIMIT_FSIZE, &limit);
        record_into(recording, signal, 0, signal.frames(), 256);
        setrlimit(RLIMIT_FSIZE, &before);
        const qint64 frozen = recording.written_frames();
        CHECK(recording.write_failed() && frozen > 0 && frozen < signal.frames(), "write failure: %lld frames written",
              (long long)frozen);
        recording.read([&](const TPeakLevels& view) {
            CHECK(view.block_count(0) == frozen / 64, "write failure: %lld blocks readable, %lld written", (long long)view.block_count(0),
                  (long long)(frozen / 64));
        });
        CHECK(recording.finish(audioPath) != TPeakFile::Status::Ok && !recording.post_process(), "write failure: finish reports Ok");
        CHECK(!QFile::exists(recording.part_path()) && !QFile::exists(peakPath), "write failure: a file was left");
    }
#endif

    // The part file cannot be made: no peaks, no file
    {
        const Signal signal = make_signal(1, 300000);
        const QString blocked = dir.filePath("blocked");
        {
            QFile blocker(blocked);   // a file where the directory would be
            CHECK(blocker.open(QIODevice::WriteOnly), "no part file: blocker");
        }
        TPeakStream recording(signal.rate, 1, blocked + "/x.peaks");
        CHECK(recording.open() != TPeakFile::Status::Ok, "no part file: opened");
        record_into(recording, signal, 0, signal.frames());
        CHECK(recording.written_frames() == 0, "no part file: frames written");
        CHECK(recording.finish(audioPath) != TPeakFile::Status::Ok, "no part file: finish reports Ok");
    }

    auto expect_no_file = [&](const Signal& signal, qint64 reported, const char* what) {
        TPeakFile file;
        const auto expected = TPeakFile::SourceInfo::from_file(audioPath, signal.rate, int(signal.channels.size()), reported);
        CHECK(file.open(peakPath, expected) != TPeakFile::Status::Ok, "%s: left a valid file", what);
    };

    // A read error in the middle
    {
        const Signal signal = make_signal(1, 200000);
        QFile::remove(peakPath);
        TPeakBuildTask task(memory_source(signal, 0, 150000), audioPath, stream_for(signal, peakPath));
        CHECK(run(task) == TPeakBuildTask::State::Failed, "read error not reported");
        expect_no_file(signal, signal.frames(), "read error");
        CHECK(!QFile::exists(task.stream()->part_path()), "read error: part file left");
    }

    // Cancelled half way through level 0, and during post-processing
    {
        const Signal signal = make_signal(2, 300000);
        QFile::remove(peakPath);
        TPeakBuildTask task(memory_source(signal), audioPath, stream_for(signal, peakPath), 1024);
        for (int steps = 0; steps < 150; ++steps) {
            task.step();
        }
        // What is made so far is on disk, in the part file: every block the
        // stream published, the complete chunks with their levels (the
        // source delivers less than asked now and then)
        const QString part = task.stream()->part_path();
        const qint64 written = task.stream()->written_frames();
        const qint64 blocks = written / 64;
        const qint64 chunks = blocks >> TPeakLevels::ChunkLevel;
        const qint64 bytes = TPeakFile::HeaderBytes + chunks * TPeakFile::chunk_bytes(TPeakLevels::ChunkLevel, 2)
                             + (blocks - (chunks << TPeakLevels::ChunkLevel)) * TPeakFile::record_bytes(2);
        CHECK(written > 0 && chunks > 0 && QFileInfo(part).size() == bytes, "%lld bytes for %lld frames: the chunks were not written",
              (long long)QFileInfo(part).size(), (long long)written);
        task.cancel();
        CHECK(run(task) == TPeakBuildTask::State::Cancelled, "cancel not honoured");
        CHECK(!QFile::exists(part) && !QFile::exists(peakPath), "a cancelled build left a file behind");

        // Cancelled after the last chunk, before the header and the name
        const Signal longer = make_signal(1, 64 * 70000);
        TPeakBuildTask late(memory_source(longer), audioPath, stream_for(longer, peakPath), 1 << 20);
        while (late.step() && late.stream()->written_frames() < longer.frames()) {
        }
        CHECK(late.state() == TPeakBuildTask::State::Running && QFile::exists(late.stream()->part_path()),
              "the build ended before its header");
        late.cancel();
        CHECK(run(late) == TPeakBuildTask::State::Cancelled && !QFile::exists(late.stream()->part_path()) && !QFile::exists(peakPath),
              "a build cancelled before its header left a file behind");
    }

    // A stream that goes before its post-processing (the project closes):
    // no file, no part file
    {
        const Signal signal = make_signal(1, 64 * 70000);
        QFile::remove(peakPath);
        QString part;
        {
            TPeakStream recording(signal.rate, 1, peakPath);
            CHECK(recording.open() == TPeakFile::Status::Ok, "gone: open");
            record_into(recording, signal, 0, signal.frames());
            CHECK(recording.finish(audioPath) == TPeakFile::Status::Ok, "gone: finish");
            part = recording.part_path();
        }
        CHECK(!QFile::exists(part) && !QFile::exists(peakPath), "a stream gone before its post-processing left a file");
    }

    // Sources the task refuses
    {
        const Signal empty = make_signal(1, 0);
        TPeakBuildTask none(memory_source(empty), audioPath, std::make_shared<TPeakStream>(48000, 1, peakPath));
        CHECK(run(none) == TPeakBuildTask::State::Failed, "empty source accepted");
        TPeakBuildTask unreadable([](QString* error) -> std::unique_ptr<TPeakBuildSource> {
            *error = QStringLiteral("no decoder");
            return nullptr;
        }, audioPath, std::make_shared<TPeakStream>(48000, 1, peakPath));
        CHECK(run(unreadable) == TPeakBuildTask::State::Failed && unreadable.error() == "no decoder", "unopenable source");
        TPeakBuildTask missing(TPeakBuildTask::audio_file(dir.filePath("none.wav")), audioPath, std::make_shared<TPeakStream>(48000, 1, peakPath));
        CHECK(run(missing) == TPeakBuildTask::State::Failed, "missing audio file accepted");
        // A source that is not what its waveform expects (it changed): no file
        const Signal signal = make_signal(2, 5000);
        QFile::remove(peakPath);
        const QDir peakDir(QFileInfo(peakPath).absolutePath());
        const QStringList partFilter{QFileInfo(peakPath).fileName() + QStringLiteral(".*.part")};
        const QStringList partsBefore = peakDir.entryList(partFilter);
        TPeakBuildTask other(memory_source(signal), audioPath, std::make_shared<TPeakStream>(44100, 2, peakPath));
        CHECK(run(other) == TPeakBuildTask::State::Failed && !QFile::exists(peakPath) && peakDir.entryList(partFilter) == partsBefore,
              "a source of another rate accepted");
        CHECK(partsBefore.isEmpty(), "part files left by the cases before: %s", qPrintable(partsBefore.join(QLatin1Char(' '))));
    }

    test_audio_file_builds(dir, referencePath, peakPath);

    if (s_failures) {
        std::printf("%d failures\n", s_failures);
        return 1;
    }
    std::printf("all peak build tests passed\n");
    return 0;
}
