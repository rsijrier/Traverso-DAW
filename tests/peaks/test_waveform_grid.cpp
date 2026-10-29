// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the power-of-two grid (TWaveformGrid) and the device
// columns of a raster view (TWaveformColumns::device_columns()):
// - the grid: the shift chosen, which rates have one, exact edges;
// - one record per grid column: a pyramid whose other records are poisoned
//   (saturated) shows any other visit, also above the top level and in the
//   chunk being written;
// - the device columns, on a finished file and on one being written: every
//   pixel shows the grid column that holds its centre, or the exact column
//   where the grid column reaches outside the clip; the grid columns in view
//   follow each other without a gap, each 1 or 2 pixels wide; and, against
//   the samples, every sample in view lies within the shown column of a
//   pixel at most 2 pixels from its own; a grid column's min and max are
//   the general query's exactly, its RMS within one code step (the grid
//   walk of a finished file decodes the stored RMS);
// - the grid walk's RMS against the true RMS of the samples, within one
//   code step.

#include "TPeakFile.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"
#include "TWaveformColumns.h"
#include "test_seed.h"

#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <random>
#include <vector>

static int failures = 0;

#define CHECK(condition, ...) \
    do { \
        if (!(condition)) { \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__); \
            std::printf("\n"); \
            ++failures; \
        } \
    } while (false)

static std::mt19937_64 s_random(20261025);

static qint64 random_between(qint64 low, qint64 high)
{
    return std::uniform_int_distribution<qint64>(low, high)(s_random);
}

static double random_unit()
{
    return std::uniform_real_distribution<double>(0.0, 1.0)(s_random);
}

static bool same_column(const TWaveformColumn& a, const TWaveformColumn& b)
{
    return a.min == b.min && a.max == b.max && a.rms == b.rms;
}

// One code step of the stored RMS: 1/1024 octave
static bool within_one_step(float value, double reference)
{
    if (reference <= 0.0 || value <= 0.0f) {
        return value == float(reference);
    }
    return std::fabs(std::log2(double(value) / reference)) * TPeakLevels::CodesPerOctave <= 1.0;
}

// A grid column: min and max exactly, the RMS within one code step (the
// grid walk decodes a record's stored RMS; the general query squares and
// roots it again)
static bool same_grid_column(const TWaveformColumn& a, const TWaveformColumn& b)
{
    return a.min == b.min && a.max == b.max && within_one_step(a.rms, b.rms);
}

// The grid: shift, rates, exact edges
static void test_grid()
{
    const struct {
        double framesPerPixel;
        int shift;
    } shifts[] = {{32.1, 6}, {63.9, 6}, {64.0, 6}, {64.0001, 7}, {100.0, 7}, {4096.0, 12}, {45000.0, 16}, {65536.0, 16},
                  {65537.0, 17}, {1.0e6, 20}};
    for (const auto& s : shifts) {
        const TWaveformGrid grid = TWaveformGrid::power_of_two(s.framesPerPixel, 48000);
        CHECK(grid.is_valid() && grid.shift == s.shift, "%g frames per pixel: shift %d, expected %d", s.framesPerPixel, grid.shift, s.shift);
    }
    // Up to 32 frames per pixel the samples are read: no grid
    for (const double none : {32.0, 31.9, 8.0, 1.0, 0.999, 0.5, 0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity(), std::ldexp(1.0, TWaveformGrid::MaxShift) * 2.0}) {
        CHECK(!TWaveformGrid::power_of_two(none, 48000).is_valid(), "%g frames per pixel has a grid", none);
    }
    CHECK(!TWaveformGrid::power_of_two(4.0, 0).is_valid(), "rate 0 has a grid");

    // Rates that do not divide UNIVERSAL_SAMPLE_RATE: a grid only where 2^s
    // frames are whole universal frames (384 kHz from 2 frames, 768 kHz from
    // 4: both from the grid's smallest columns on; 37.8 and 50 kHz never)
    CHECK(TWaveformGrid::power_of_two(64.0, 384000).is_valid() && TWaveformGrid::power_of_two(64.0, 768000).is_valid(),
          "384 and 768 kHz: a grid");
    bool none = true;
    for (int shift = TWaveformGrid::MinShift; shift <= 20; ++shift) {
        none = none && !TWaveformGrid::power_of_two(std::ldexp(1.0, shift), 37800).is_valid()
               && !TWaveformGrid::power_of_two(std::ldexp(1.0, shift), 50000).is_valid();
    }
    CHECK(none, "37.8 and 50 kHz: no grid");

    // The edges are exact: whole frames, whole universal frames
    for (const uint rate : {44100u, 48000u, 96000u, 384000u}) {
        bool exact = true;
        for (int shift = TWaveformGrid::MinShift; shift <= 24; ++shift) {
            const TWaveformGrid grid = TWaveformGrid::power_of_two(std::ldexp(1.0, shift), rate);
            if (!grid.is_valid() || grid.shift != shift) {
                exact = false;
                continue;
            }
            const qint64 hour = qint64(3600) * rate;
            for (const qint64 column : {qint64(0), qint64(1), std::min<qint64>(1000003, hour >> shift), (hour >> shift) - 1, hour >> shift}) {
                double fraction = 1.0;
                exact = exact && TTimeRef::frame_at_or_before(grid.start_of(column), rate, &fraction) == column << shift && fraction == 0.0;
            }
            exact = exact && TTimeRef::to_frames(grid.column_width(), rate) == std::ldexp(1.0, shift)
                    && grid.start_of(7) == grid.column_width() * 7.0;
        }
        CHECK(exact, "%u Hz: the grid's edges are whole frames", rate);
    }
}

// A pyramid in memory: N chunks of a stereo source, every record poisoned
// (saturated: a visit makes the column saturated) unless keep(level, index)
// says to give it a value of its own
struct PoisonedPyramid
{
    static constexpr int Channels = 2;
    static constexpr int Top = TPeakLevels::ChunkLevel;
    qint64 chunks;
    std::vector<qint16> records;
    std::vector<qint16> tail;

    static qint16 minimum(int level, qint64 index, int channel) { return qint16(-(1000 + (index * 7 + level * 3 + channel) % 97)); }
    static qint16 maximum(int level, qint64 index, int channel) { return qint16(1000 + (index * 5 + level + channel * 11) % 89); }
    static qint16 rms(int level, qint64 index, int channel) { return qint16(700 + (index * 3 + level * 5 + channel) % 61); }

    template<typename Keep>
    PoisonedPyramid(qint64 chunkCount, qint64 tailBlocks, Keep keep)
        : chunks(chunkCount)
    {
        const int codes = TPeakLevels::record_codes(Channels);
        records.assign(size_t(chunks * TPeakLevels::chunk_records(Top) * codes), 0);
        for (qint64 chunk = 0; chunk < chunks; ++chunk) {
            for (int level = 0; level <= Top; ++level) {
                for (qint64 local = 0; local < (qint64(1) << (Top - level)); ++local) {
                    const qint64 index = (chunk << (Top - level)) + local;
                    qint16* record = records.data()
                                     + (chunk * TPeakLevels::chunk_records(Top) + TPeakLevels::level_offset(Top, level) + local) * codes;
                    fill(record, level, index, keep(level, index));
                }
            }
        }
        tail.assign(size_t(tailBlocks * codes), 0);
        for (qint64 block = 0; block < tailBlocks; ++block) {
            fill(tail.data() + block * codes, 0, (chunks << Top) + block, keep(0, (chunks << Top) + block));
        }
    }

    static void fill(qint16* record, int level, qint64 index, bool kept)
    {
        for (int c = 0; c < Channels; ++c) {
            record[2 * c] = kept ? minimum(level, index, c) : qint16(-TPeakLevels::MaxCode);
            record[2 * c + 1] = kept ? maximum(level, index, c) : qint16(TPeakLevels::MaxCode);
            record[2 * Channels + c] = kept ? rms(level, index, c) : qint16(TPeakLevels::MaxCode);
        }
    }

    TPeakLevels levels() const
    {
        const qint64 blocks = (chunks << Top) + qint64(tail.size()) / TPeakLevels::record_codes(Channels);
        return TPeakLevels(48000, Channels, blocks * TPeakLevels::BlockFrames, Top, records.data(), chunks,
                           tail.empty() ? nullptr : tail.data());
    }
};

// On an aligned grid the query reads one record per column (below the top),
// the top level's records above it, level 0 in the chunk being written
static void test_one_record()
{
    constexpr int Top = PoisonedPyramid::Top;
    for (int level = 0; level <= Top + 2; ++level) {
        const int kept = std::min(level, Top);
        const PoisonedPyramid pyramid(16, 0, [&](int l, qint64) { return l == kept; });
        const TPeakLevels levels = pyramid.levels();
        const TWaveformGrid grid = TWaveformGrid::power_of_two(std::ldexp(1.0, level + TPeakLevels::BlockFramesShift), 48000);
        const int count = int(levels.block_count(0) >> level);
        std::vector<TWaveformColumn> out(size_t(count) * 2);
        TWaveformColumns::query(levels, nullptr, grid.start_of(0), grid.column_width(), count, out.data());
        bool right = count > 0;
        for (int c = 0; c < 2; ++c) {
            for (int k = 0; k < count && right; ++k) {
                // Above the top level: the top's blocks of the column, all of equal frames
                qint16 low = TPeakLevels::MaxCode, high = -TPeakLevels::MaxCode;
                double squares = 0.0;
                const qint64 per = qint64(1) << (level - kept);
                for (qint64 j = k * per; j < (k + 1) * per; ++j) {
                    low = std::min(low, PoisonedPyramid::minimum(kept, j, c));
                    high = std::max(high, PoisonedPyramid::maximum(kept, j, c));
                    const double value = TPeakLevels::magnitude_value(PoisonedPyramid::rms(kept, j, c));
                    squares += value * value;
                }
                const TWaveformColumn& column = out[size_t(c) * size_t(count) + size_t(k)];
                right = column.min == TPeakLevels::to_value(low) && column.max == TPeakLevels::to_value(high)
                        && std::fabs(column.rms - float(std::sqrt(squares / double(per)))) <= column.rms * 1.0e-6f;
            }
        }
        CHECK(right, "columns of 2^%d frames: a record other than level %d's was read", level + TPeakLevels::BlockFramesShift, kept);
    }

    // A file being written: level 0 alone in the chunk being written
    const qint64 tailBlocks = 200;
    const int level = 3;
    const PoisonedPyramid pyramid(4, tailBlocks, [&](int l, qint64 index) { return l == level || (l == 0 && index >= (4 << Top)); });
    const TPeakLevels levels = pyramid.levels();
    const TWaveformGrid grid = TWaveformGrid::power_of_two(std::ldexp(1.0, level + TPeakLevels::BlockFramesShift), 48000);
    const int count = int(levels.block_count(0) >> level);
    std::vector<TWaveformColumn> out(size_t(count) * 2);
    TWaveformColumns::query(levels, nullptr, grid.start_of(0), grid.column_width(), count, out.data());
    bool right = true;
    for (int k = 0; k < count && right; ++k) {
        const qint64 first = qint64(k) << level;
        qint16 high = -TPeakLevels::MaxCode;
        if (first < (4 << Top)) {
            high = PoisonedPyramid::maximum(level, k, 0);
        } else {
            for (qint64 j = first; j < first + (1 << level); ++j) {
                high = std::max(high, PoisonedPyramid::maximum(0, j, 0));
            }
        }
        right = out[size_t(k)].max == TPeakLevels::to_value(high);
    }
    CHECK(right, "a file being written: a record other than level %d's, or level 0's in the chunk being written, was read", level);
}

class MemorySource final : public TWaveformSampleSource
{
public:
    explicit MemorySource(const std::vector<std::vector<float>>& channels) : m_channels(channels) {}
    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override
    {
        const qint64 frames = qint64(m_channels.front().size());
        const qint64 available = std::max<qint64>(0, std::min(count, frames - firstFrame));
        for (size_t c = 0; c < m_channels.size(); ++c) {
            std::copy_n(m_channels[c].data() + firstFrame, available, channels[c]);
        }
        return available;
    }

private:
    const std::vector<std::vector<float>>& m_channels;
};

// The device columns of random views of levels (a finished file or one
// being written): written frames only
static int check_views(const TPeakLevels& levels, const std::vector<std::vector<float>>& signal, const char* what)
{
    const uint rate = levels.rate();
    const qint64 frames = levels.frame_count();
    const qint64 universal = TTimeRef::UNIVERSAL_SAMPLE_RATE;
    MemorySource samples(signal);
    int gridViews = 0;
    for (int round = 0; round < 300; ++round) {
        // A zoom from 0.5 to 300000 frames per pixel, sometimes a power of two
        // or just below one; a width of any number of universal frames (as a
        // device pixel ratio makes it)
        double framesPerPixel = std::exp(std::log(0.5) + random_unit() * (std::log(300000.0) - std::log(0.5)));
        if (round % 5 == 0) {
            framesPerPixel = std::ldexp(1.0, int(random_between(0, 18))) * (round % 10 == 0 ? 1.0 : 0.97);
        }
        const TTimeRef width(std::max<qint64>(1, qint64(std::llround(framesPerPixel * double(universal) / rate)) + random_between(-1, 1)));
        const int count = int(random_between(1, 2000));
        const qint64 viewUniversal = width.universal_frame() * count;
        const qint64 fileUniversal = TTimeRef(uint64_t(frames), rate).universal_frame();
        const TTimeRef start(random_between(0, std::max<qint64>(fileUniversal - viewUniversal / 2, 0)));
        // The clip: all of it, or with an edge in view
        TTimeRef clipStart, clipEnd(fileUniversal);
        if (random_between(0, 2) > 0) {
            clipStart = TTimeRef(start.universal_frame() + random_between(-viewUniversal / 4, viewUniversal / 3));
            clipEnd = TTimeRef(start.universal_frame() + random_between(viewUniversal / 2, viewUniversal + viewUniversal / 4));
            if (clipStart < TTimeRef()) {
                clipStart = TTimeRef();
            }
        }

        std::vector<TWaveformColumn> out(size_t(count) * signal.size());
        std::vector<TWaveformColumn> exact(size_t(count) * signal.size());
        TWaveformColumns::device_columns(levels, &samples, start, width, count, clipStart, clipEnd, out.data());
        TWaveformColumns::query(levels, &samples, start, width, count, exact.data());

        const TWaveformGrid grid = TWaveformGrid::power_of_two(TTimeRef::to_frames(width, rate), rate);
        if (!grid.is_valid()) {
            bool same = true;
            for (size_t i = 0; i < out.size(); ++i) {
                same = same && same_column(out[i], exact[i]);
            }
            CHECK(same, "%s round %d (%g frames per pixel): without a grid, not the exact columns", what, round, framesPerPixel);
            continue;
        }
        ++gridViews;

        // Every pixel: the grid column that holds its centre, or the exact
        // column where that grid column reaches outside the clip
        // No edge where the clip reaches the end of the source: past it there is no audio
        const qint64 clipFirst = TTimeRef::frame_at_or_after(clipStart, rate);
        const qint64 clipLast = TTimeRef::frame_at_or_after(clipEnd, rate) >= frames ? std::numeric_limits<qint64>::max()
                                                                                       : TTimeRef::frame_at_or_after(clipEnd, rate);
        std::map<qint64, int> pixelsOf;
        std::vector<qint64> columnOf(static_cast<size_t>(count), -1);
        bool right = true;
        for (int i = 0; i < count && right; ++i) {
            const TTimeRef centre = start + width * double(i) + TTimeRef(width.universal_frame() / 2);
            const qint64 column = std::max<qint64>(TTimeRef::frame_at_or_before(centre, rate), 0) >> grid.shift;
            const bool inside = (column << grid.shift) >= clipFirst && ((column + 1) << grid.shift) <= clipLast;
            std::vector<TWaveformColumn> reference(signal.size());
            if (inside) {
                TWaveformColumns::query(levels, &samples, grid.start_of(column), grid.column_width(), 1, reference.data());
                ++pixelsOf[column];
                columnOf[size_t(i)] = column;
            }
            for (size_t c = 0; c < signal.size() && right; ++c) {
                const TWaveformColumn& shown = out[c * size_t(count) + size_t(i)];
                right = inside ? same_grid_column(shown, reference[c]) : same_column(shown, exact[c * size_t(count) + size_t(i)]);
            }
            CHECK(right, "%s round %d (%g frames per pixel, 2^%d) pixel %d: not the %s column", what, round, framesPerPixel, grid.shift, i,
                  inside ? "grid" : "exact");
        }

        // The grid columns in view follow each other without a gap, 1 or 2
        // pixels each (the first and last may be cut off by the view)
        if (!pixelsOf.empty()) {
            bool gapless = true;
            bool narrow = true;
            qint64 previous = pixelsOf.begin()->first - 1;
            for (const auto& [column, pixels] : pixelsOf) {
                gapless = gapless && column == previous + 1;
                narrow = narrow && pixels <= 2 && (pixels >= 1);
                previous = column;
            }
            CHECK(gapless && narrow, "%s round %d (%g frames per pixel, 2^%d): grid columns %s", what, round, framesPerPixel, grid.shift,
                  gapless ? "wider than 2 pixels" : "missing between pixels");
        }

        // Against the samples: every sample in a grid column in view lies
        // within the column a pixel shows, and that pixel is at most 2
        // pixels from the sample's own
        bool contained = true;
        bool near = true;
        for (int probe = 0; probe < 50 && !pixelsOf.empty(); ++probe) {
            const qint64 firstFrame = pixelsOf.begin()->first << grid.shift;
            const qint64 endFrame = std::min((pixelsOf.rbegin()->first + 1) << grid.shift, frames);
            if (endFrame <= firstFrame) {
                break;
            }
            const qint64 frame = random_between(firstFrame, endFrame - 1);
            const qint64 column = frame >> grid.shift;
            const qint64 own = TTimeRef::frame_at_or_before(TTimeRef(uint64_t(frame), rate) - start, rate) < 0
                                   ? -1 : (TTimeRef(uint64_t(frame), rate) - start).universal_frame() / width.universal_frame();
            int nearest = -1;
            for (int i = 0; i < count; ++i) {
                if (columnOf[size_t(i)] == column && (nearest < 0 || std::abs(i - own) < std::abs(nearest - own))) {
                    nearest = i;
                }
            }
            if (nearest < 0) {
                continue;   // the column holds the clip's edge: exact
            }
            near = near && std::abs(nearest - own) <= 2;
            for (size_t c = 0; c < signal.size(); ++c) {
                const float sample = signal[c][size_t(frame)];
                const TWaveformColumn& shown = out[c * size_t(count) + size_t(nearest)];
                contained = contained && (std::isnan(sample) || (shown.min <= sample && shown.max >= sample)
                                          || std::fabs(sample) > TPeakLevels::max_value());
            }
        }
        CHECK(contained, "%s round %d (%g frames per pixel): a sample outside the column shown", what, round, framesPerPixel);
        CHECK(near, "%s round %d (%g frames per pixel): a sample shown more than 2 pixels from its place", what, round, framesPerPixel);
    }
    return gridViews;
}

static void test_device_columns()
{
    QTemporaryDir dir;
    QFile audio(dir.filePath("source.wav"));
    if (!audio.open(QIODevice::WriteOnly) || audio.write("audio") != 5) {
        CHECK(false, "cannot write the audio stand-in");
        return;
    }
    audio.close();
    for (const int channelCount : {1, 2, 3}) {
        const uint rate = channelCount == 3 ? 44100 : 48000;
        const qint64 frames = qint64(rate) * 200 + random_between(0, 100000);
        std::vector<std::vector<float>> signal(static_cast<size_t>(channelCount), std::vector<float>(static_cast<size_t>(frames)));
        for (int c = 0; c < channelCount; ++c) {
            const double frequency = 30.0 + 3000.0 * random_unit();
            for (qint64 n = 0; n < frames; ++n) {
                const double envelope = 0.1 + 0.8 * std::fabs(std::sin(double(n) / double(rate) * 0.3));
                double value = envelope * std::sin(2.0 * M_PI * frequency * double(n) / rate) + 0.05 * (random_unit() * 2.0 - 1.0);
                if (random_unit() < 0.0002) {
                    value = (random_unit() * 2.0 - 1.0) * 1.5;   // a spike
                }
                signal[size_t(c)][size_t(n)] = float(value);
            }
        }
        const QString path = dir.filePath(QStringLiteral("%1.peaks").arg(channelCount));
        TPeakStream stream(rate, channelCount, path);
        CHECK(stream.open() == TPeakFile::Status::Ok, "open");
        auto append = [&](qint64 from, qint64 to) {
            for (qint64 position = from; position < to;) {
                const qint64 piece = std::min<qint64>(random_between(1, 20000), to - position);
                std::vector<const float*> pointers;
                for (const auto& channel : signal) {
                    pointers.push_back(channel.data() + position);
                }
                stream.append(pointers.data(), piece);
                position += piece;
            }
        };

        // Being written: some chunks complete, the last one running
        append(0, frames / 2);
        int views = 0;
        stream.read([&](const TPeakLevels& levels) { views += check_views(levels, signal, "being written"); });
        append(frames / 2, frames);
        CHECK(stream.finish(audio.fileName()) == TPeakFile::Status::Ok, "finish");
        while (stream.post_process()) {
        }
        TPeakFile file;
        CHECK(file.open(path, TPeakFile::SourceInfo::from_file(audio.fileName(), rate, channelCount, frames)) == TPeakFile::Status::Ok,
              "open the finished file");
        if (file.is_open()) {
            views += check_views(file.levels(), signal, "finished");

            // The grid walk's RMS against the true RMS of the samples, on
            // every grid level up to the chunk's
            const TPeakLevels& levels = file.levels();
            bool close = true;
            for (int shift = TPeakLevels::BlockFramesShift; shift <= TPeakLevels::BlockFramesShift + TPeakLevels::ChunkLevel && close; ++shift) {
                const TWaveformGrid grid = TWaveformGrid::power_of_two(std::ldexp(1.0, shift), rate);
                const qint64 gridColumns = (frames + (qint64(1) << shift) - 1) >> shift;
                const int count = int(std::min<qint64>(gridColumns, 64));
                const qint64 firstColumn = random_between(0, gridColumns - count);
                std::vector<TWaveformColumn> out(size_t(count) * size_t(channelCount));
                TWaveformColumns::device_columns(levels, nullptr, grid.start_of(firstColumn), grid.column_width(), count, TTimeRef(),
                                                 TTimeRef(uint64_t(frames), rate), out.data());
                for (int i = 0; i < count && close; ++i) {
                    const qint64 from = (firstColumn + i) << shift;
                    const qint64 to = std::min(from + (qint64(1) << shift), frames);
                    for (int c = 0; c < channelCount && close; ++c) {
                        double squares = 0.0;
                        for (qint64 n = from; n < to; ++n) {
                            squares += double(signal[size_t(c)][size_t(n)]) * double(signal[size_t(c)][size_t(n)]);
                        }
                        const double rms = std::sqrt(squares / double(to - from));
                        close = within_one_step(out[size_t(c) * size_t(count) + size_t(i)].rms, rms);
                        CHECK(close, "%d channel(s), 2^%d frames, column %lld channel %d: RMS %g, the samples' %g", channelCount, shift,
                              (long long)(firstColumn + i), c, double(out[size_t(c) * size_t(count) + size_t(i)].rms), rms);
                    }
                }
            }
        }
        std::printf("%d channel(s): %d views on the grid checked\n", channelCount, views);
        CHECK(views > 250, "%d channel(s): only %d views on the grid", channelCount, views);
    }
}

int main(int argc, char** argv)
{
    const TestSeed seed(argc, argv, 20261025);
    s_random.seed(seed.seed());
    test_grid();
    test_one_record();
    test_device_columns();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all waveform grid tests passed\n");
    return 0;
}
