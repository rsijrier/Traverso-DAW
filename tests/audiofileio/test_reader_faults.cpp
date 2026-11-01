// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TAudioReader with a damaged file, described by a scenario:
// what a stream sees when the decoder fails (TAudioDecoder's contract, points
// 3 and 4), on both ways a stream reads, at the file's rate and resampled.
// - A read error in a damaged part: the read that reaches it gives the
//   frames before it and zeros, fewer frames than asked; the next read
//   seeks, and past the damaged part the audio comes again.
// - A seek into a part that cannot be sought: zeros and 0; a read elsewhere
//   seeks and reads.
// - Audio that ends before the frame count: the frames until then, then
//   zeros and 0 up to the frame count.
// - NaN and inf in a float file: passed on today, a known defect (task R6):
//   reported as KNOWN, and a failure once it is fixed (the entry goes then).
// Reads go through read() at a location in time, as the streams read.

#include "TAudioReader.h"
#include "TAudioResampler.h"
#include "TAudioBuffer.h"
#include "TTimeRef.h"
#include "test_signals.h"

#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
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

static constexpr uint FileRate = 48000;
static constexpr nframes_t FileFrames = 4 * FileRate;
static constexpr nframes_t Block = 1024;

static QString write_scenario(const QTemporaryDir& dir, const char* name, const char* faults)
{
    const QString path = dir.filePath(QLatin1String(name));
    const QByteArray json = QStringLiteral(R"(# tscenario 1
{"rate": %1, "frames": %2, "channels": [
        {"signal": "sine", "frequency": 441, "amplitude": 0.5}, {"signal": "ramp"}], "faults": %3})")
                                .arg(FileRate)
                                .arg(FileFrames)
                                .arg(QLatin1String(faults))
                                .toUtf8();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size()) {
        return QString();
    }
    return path;
}

// A reader at outputRate (the file's own, or resampled with soxr at its
// default quality)
static std::unique_ptr<TAudioReader> open_at(const QString& path, uint outputRate)
{
    std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
    if (reader && outputRate != reader->file_rate()) {
        if (!reader->set_output_rate_and_resampler(
                outputRate, {TResamplerChoice::Backend::LibSoxr, qint16(TAudioResampler::default_quality(TResamplerChoice::Backend::LibSoxr))})) {
            return nullptr;
        }
    }
    return reader;
}

struct ReadBlock
{
    nframes_t first;
    nframes_t read;
    std::vector<float> samples;   // channel 1, Block frames
    std::optional<TAudioFileError::Code> error;
};

// Blocks of Block frames read one after the other from output frame from, as
// a stream reads: each at the location where the last one ended
static std::vector<ReadBlock> read_blocks(TAudioReader& reader, nframes_t from, nframes_t to)
{
    std::vector<ReadBlock> blocks;
    TAudioBuffer buffer;
    for (nframes_t first = from; first < to; first += Block) {
        auto [read, error] = reader.read(buffer, TTimeRef(first, reader.output_rate()), Block);
        const float* data = buffer.get_channel_buffer(1).get_data(Block);
        blocks.push_back({first, read, std::vector<float>(data, data + Block),
                          error ? std::optional<TAudioFileError::Code>(error->code) : std::nullopt});
    }
    return blocks;
}

// The block a reader without the fault gives at first, read there first (a
// seek): what a reader that sought there after an error gives
static std::vector<float> clean_block(const QString& cleanPath, uint outputRate, nframes_t first)
{
    const std::unique_ptr<TAudioReader> reader = open_at(cleanPath, outputRate);
    TAudioBuffer buffer;
    std::ignore = reader->read(buffer, TTimeRef(first, outputRate), Block);
    const float* data = buffer.get_channel_buffer(1).get_data(Block);
    return std::vector<float>(data, data + Block);
}

static bool all_zero(const std::vector<float>& samples, nframes_t from)
{
    return std::all_of(samples.begin() + from, samples.end(), [](float sample) { return sample == 0.0f; });
}

static const char* way(uint outputRate)
{
    return outputRate == FileRate ? "at the file's rate" : "resampled";
}

static void test_read_error(const QTemporaryDir& dir, const QString& cleanPath, uint outputRate)
{
    char what[200];
    // The damaged part: file frames [100000, 110000)
    const QString path = write_scenario(dir, "read-error.tscenario", R"([{"kind": "read-error", "frame": 100000, "frames": 10000}])");
    const std::unique_ptr<TAudioReader> reader = open_at(path, outputRate);
    const nframes_t damageStart = TTimeRef::to_nframes(TTimeRef::frame_at_or_before(TTimeRef(nframes_t(100000), FileRate), outputRate));
    const nframes_t damageEnd = TTimeRef::to_nframes(TTimeRef::frame_at_or_after(TTimeRef(nframes_t(110000), FileRate), outputRate));
    const std::vector<ReadBlock> blocks = read_blocks(*reader, 0, reader->output_frame_count());

    // The first short read: it gives the frames up to the damaged part (a
    // resampler flushed there, as at the end of a file; one frame of
    // rounding), and zeros after them
    const ReadBlock* shortBlock = nullptr;
    for (const ReadBlock& block : blocks) {
        if (block.read < Block && block.first + Block <= reader->output_frame_count()) {
            shortBlock = &block;
            break;
        }
    }
    std::snprintf(what, sizeof(what), "a read error %s: the read that reaches it comes short (at output frame %u, the damage at %u)", way(outputRate),
                  shortBlock ? shortBlock->first + shortBlock->read : 0, damageStart);
    check(shortBlock && std::labs(long(shortBlock->first + shortBlock->read) - long(damageStart)) <= 1, what);
    if (!shortBlock) {
        return;
    }
    std::snprintf(what, sizeof(what), "a read error %s: zeros after the frames it gave", way(outputRate));
    check(all_zero(shortBlock->samples, shortBlock->read), what);
    // The error is the read's that met it: the short one, or for the
    // resampled path one block before it, which reads ahead of its output
    // (its frames are all there)
    std::snprintf(what, sizeof(what), "a read error %s: the read that comes short says ReadFailed, the reads well before it no error",
                  way(outputRate));
    bool errorsRight = shortBlock->error == TAudioFileError::Code::ReadFailed;
    const nframes_t aheadBlocks = outputRate == FileRate ? 0 : Block;
    for (const ReadBlock& block : blocks) {
        if (block.first + aheadBlocks < shortBlock->first) {
            errorsRight = errorsRight && !block.error;
        }
    }
    check(errorsRight, what);

    // In the damaged part: 0 and zeros; past it the audio again, as a reader
    // without the damage gives it after a seek there
    bool inside = true;
    const ReadBlock* after = nullptr;
    for (const ReadBlock& block : blocks) {
        if (block.first > shortBlock->first && block.first + Block <= damageEnd) {
            inside = inside && block.read == 0 && all_zero(block.samples, 0);
        }
        if (!after && block.first >= damageEnd) {
            after = &block;
        }
    }
    for (const ReadBlock& block : blocks) {
        if (block.first > shortBlock->first && block.first + Block <= damageEnd) {
            inside = inside && block.error == TAudioFileError::Code::ReadFailed;
        }
    }
    std::snprintf(what, sizeof(what), "a read error %s: reads inside the damaged part give 0, zeros and ReadFailed", way(outputRate));
    check(inside, what);
    const bool again = after && after->read == Block && !after->error && after->samples == clean_block(cleanPath, outputRate, after->first);
    std::snprintf(what, sizeof(what), "a read error %s: past the damaged part the next read seeks and the audio comes again (frame %u)", way(outputRate),
                  after ? after->first : 0);
    check(again, what);
}

static void test_seek_error(const QTemporaryDir& dir, const QString& cleanPath, uint outputRate)
{
    char what[200];
    // A seek into file frames [60000, 70000) fails; reading through the part
    // without a seek works (no seek is asked)
    const QString path = write_scenario(dir, "seek-error.tscenario", R"([{"kind": "seek-error", "frame": 60000, "frames": 10000}])");
    const std::unique_ptr<TAudioReader> reader = open_at(path, outputRate);
    TAudioBuffer buffer;
    const nframes_t inside = TTimeRef::to_nframes(TTimeRef::frame_at_or_before(TTimeRef(nframes_t(65000), FileRate), outputRate));
    buffer.check_capacity(Block, 2);
    std::fill_n(buffer.get_channel_buffer(1).get_data(Block), Block, 0.7f);
    auto [read, error] = reader->read(buffer, TTimeRef(inside, outputRate), Block);
    const float* data = buffer.get_channel_buffer(1).get_data(Block);
    std::snprintf(what, sizeof(what), "a seek that fails %s: zeros, 0 and SeekFailed (%u)", way(outputRate), read);
    check(read == 0 && error && error->code == TAudioFileError::Code::SeekFailed
              && std::all_of(data, data + Block, [](float sample) { return sample == 0.0f; }),
          what);

    // The same location again: the failed seek left the reader without a
    // position, so it seeks again (and fails again), it does not read from
    // where the decoder is
    std::fill_n(buffer.get_channel_buffer(1).get_data(Block), Block, 0.7f);
    const nframes_t again = reader->read(buffer, TTimeRef(inside, outputRate), Block).frames;
    std::snprintf(what, sizeof(what), "a seek that fails %s: the same location again, zeros and 0 (%u)", way(outputRate), again);
    check(again == 0 && std::all_of(data, data + Block, [](float sample) { return sample == 0.0f; }), what);

    // Elsewhere: it seeks, the error clears, the audio is there
    const nframes_t elsewhere = TTimeRef::to_nframes(TTimeRef::frame_at_or_before(TTimeRef(nframes_t(90000), FileRate), outputRate));
    const nframes_t back = reader->read(buffer, TTimeRef(elsewhere, outputRate), Block).frames;
    const std::vector<float> samples(data, data + Block);
    std::snprintf(what, sizeof(what), "a seek that fails %s: a read elsewhere seeks and reads (%u)", way(outputRate), back);
    check(back == Block && samples == clean_block(cleanPath, outputRate, elsewhere), what);
}

static void test_end(const QTemporaryDir& dir, uint outputRate)
{
    char what[200];
    // The audio ends at file frame 150000 of 192000: a header that says more
    const QString path = write_scenario(dir, "end.tscenario", R"([{"kind": "end", "frame": 150000}])");
    const std::unique_ptr<TAudioReader> reader = open_at(path, outputRate);
    const nframes_t end = TTimeRef::to_nframes(TTimeRef::frame_at_or_before(TTimeRef(nframes_t(150000), FileRate), outputRate));
    const std::vector<ReadBlock> blocks = read_blocks(*reader, 0, reader->output_frame_count());
    nframes_t total = 0;
    bool zerosAfter = true;
    bool first = true;
    for (const ReadBlock& block : blocks) {
        total += block.read;
        if (block.read < Block && block.first + Block <= reader->output_frame_count()) {
            zerosAfter = zerosAfter && all_zero(block.samples, block.read);
            if (!first) {
                zerosAfter = zerosAfter && block.read == 0;
            }
            first = false;
        }
    }
    // At and past the frame count: nothing to read, no error
    TAudioBuffer buffer;
    const TAudioFileResult past = reader->read(buffer, TTimeRef(reader->output_frame_count(), outputRate), Block);
    zerosAfter = zerosAfter && past.frames == 0 && !past.error;
    std::snprintf(what, sizeof(what), "audio that ends early %s: the frames until then (%u, the end at %u), zeros and 0 after", way(outputRate), total, end);
    check(std::labs(long(total) - long(end)) <= 1 && zerosAfter, what);
}

// Task R6: NaN and inf from a float file are not stopped at the reader. KNOWN
// until that task is done; a failure once it is, to remove this entry.
static void test_values(const QTemporaryDir& dir, uint outputRate)
{
    char what[200];
    const QString path = write_scenario(dir, "values.tscenario",
                                        R"([{"kind": "value", "frame": 30000, "channel": 1, "value": "nan"},
                                            {"kind": "value", "frame": 40000, "channel": 1, "value": "inf"}])");
    const std::unique_ptr<TAudioReader> reader = open_at(path, outputRate);
    const std::vector<ReadBlock> blocks = read_blocks(*reader, 0, reader->output_frame_count() / 2);
    int nonFinite = 0;
    for (const ReadBlock& block : blocks) {
        nonFinite += int(std::count_if(block.samples.begin(), block.samples.end(), [](float sample) { return !std::isfinite(sample); }));
    }
    std::snprintf(what, sizeof(what), "NaN and inf in the file %s: %d samples that are not finite reach the output", way(outputRate), nonFinite);
    if (nonFinite > 0) {
        std::printf("%s: KNOWN (task R6: stopped at the reader boundary)\n", what);
    } else {
        char fixed[260];
        std::snprintf(fixed, sizeof(fixed), "%s: the KNOWN entry (task R6) is out of date: remove it", what);
        check(false, fixed);
    }
}

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }
    const QString cleanPath = write_scenario(dir, "clean.tscenario", "[]");

    for (const uint outputRate : {FileRate, 44100u}) {
        test_read_error(dir, cleanPath, outputRate);
        test_seek_error(dir, cleanPath, outputRate);
        test_end(dir, outputRate);
        test_values(dir, outputRate);
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all reader fault tests passed\n");
    return 0;
}
