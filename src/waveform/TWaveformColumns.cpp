// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TWaveformColumns.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

static constexpr TWaveformColumn EmptyColumn{std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(), 0.0f};

TWaveformGrid TWaveformGrid::power_of_two(double framesPerPixel, uint rate)
{
    TWaveformGrid grid;
    grid.rate = rate;
    if (rate == 0 || !(framesPerPixel > 0.0) || framesPerPixel > double(qint64(1) << MaxShift)) {
        return grid;
    }
    int shift = 0;
    while (double(qint64(1) << shift) < framesPerPixel) {
        ++shift;
    }
    if (shift < MinShift || ((TTimeRef::UNIVERSAL_SAMPLE_RATE << shift) % qint64(rate)) != 0) {
        return grid;
    }
    grid.shift = shift;
    return grid;
}

TWaveformColumns::Source TWaveformColumns::source_for(const TPeakLevels& levels, const TTimeRef& columnWidth)
{
    if (levels.frame_count() <= 0 || levels.rate() == 0) {
        return Source::None;
    }
    if (TTimeRef::to_frames(columnWidth, levels.rate()) < SourceThreshold) {
        return Source::Samples;
    }
    return levels.level_count() > 0 ? Source::Pyramid : Source::None;
}

float TWaveformColumns::max_amplitude(const TPeakLevels& levels, TWaveformSampleSource* samples, qint64 first, qint64 last)
{
    const int channels = levels.channel_count();
    first = std::max<qint64>(first, 0);
    last = std::min(last, levels.frame_count());
    if (first >= last || channels <= 0) {
        return 0.0f;
    }

    std::vector<float> buffer;
    std::vector<float*> pointers(static_cast<size_t>(channels));
    auto max_of_samples = [&](qint64 from, qint64 to) {
        float amplitude = 0.0f;
        if (from >= to) {
            return amplitude;
        }
        buffer.resize(size_t(to - from) * size_t(channels));
        for (int c = 0; c < channels; ++c) {
            pointers[size_t(c)] = buffer.data() + qsizetype(c) * (to - from);
        }
        const qint64 read = samples->read(from, to - from, pointers.data());
        for (int c = 0; c < channels; ++c) {
            for (qint64 i = 0; i < read; ++i) {
                amplitude = std::max(amplitude, std::fabs(pointers[size_t(c)][i]));
            }
        }
        return amplitude;
    };

    // The level-0 blocks fully inside the range; without them (short range
    // or no pyramid) the samples decide
    const qint64 firstBlock = (first + TPeakLevels::BlockFrames - 1) >> TPeakLevels::BlockFramesShift;
    const qint64 lastBlock = std::min(last >> TPeakLevels::BlockFramesShift, levels.level_count() > 0 ? levels.block_count(0) : 0);
    if (firstBlock >= lastBlock) {
        return max_of_samples(first, last);
    }

    // The partial blocks at both ends hold frames outside the range: their
    // samples decide
    float amplitude = std::max(max_of_samples(first, firstBlock << TPeakLevels::BlockFramesShift),
                               max_of_samples(lastBlock << TPeakLevels::BlockFramesShift, last));

    // The blocks between: the pyramid's bound. A stored max is rounded up and
    // a min down, so the bound is never below the real maximum and at most
    // one code step above it
    auto stored_amplitude = [](const TPeakBlock& block) {
        return std::max(std::fabs(TPeakLevels::to_value(block.min)), std::fabs(TPeakLevels::to_value(block.max)));
    };
    float bound = 0.0f;
    for (int c = 0; c < channels; ++c) {
        bound = std::max(bound, stored_amplitude(levels.range(c, firstBlock, lastBlock)));
    }
    if (bound < TPeakLevels::max_value()) {
        return std::max(amplitude, bound);
    }

    // Saturated: a block beyond the code range (+24 dB) only says that it is
    // beyond, so those blocks are read. The others are below every one of them.
    for (qint64 block = firstBlock; block < lastBlock; ++block) {
        bool saturated = false;
        for (int c = 0; c < channels && !saturated; ++c) {
            saturated = stored_amplitude(levels.block(0, c, block)) >= TPeakLevels::max_value();
        }
        if (saturated) {
            const qint64 from = block << TPeakLevels::BlockFramesShift;
            amplitude = std::max(amplitude, max_of_samples(from, from + TPeakLevels::BlockFrames));
        }
    }
    return amplitude;
}

qint64 TWaveformColumns::nearest_zero_crossing(const TPeakLevels& levels, TWaveformSampleSource* samples, qint64 frame, qint64 maxDistance)
{
    const int channels = levels.channel_count();
    const qint64 frames = levels.frame_count();
    if (channels <= 0 || frames < 2 || maxDistance < 0) {
        return -1;
    }

    // One frame beyond the distance on both sides: a crossing's other frame
    // may lie there
    const qint64 first = std::max<qint64>(frame - maxDistance - 1, 0);
    const qint64 last = std::min(frame + maxDistance + 2, frames);   // exclusive
    if (last - first < 2) {
        return -1;
    }

    std::vector<float> buffer(size_t(last - first) * size_t(channels));
    std::vector<float*> pointers(static_cast<size_t>(channels));
    for (int c = 0; c < channels; ++c) {
        pointers[size_t(c)] = buffer.data() + qsizetype(c) * (last - first);
    }
    const qint64 read = samples->read(first, last - first, pointers.data());
    if (read < 2) {
        return -1;
    }

    // The signed sum over the channels decides the sign, the sum of the
    // magnitudes how close to zero a frame is
    auto sum = [&](qint64 i) {
        float value = 0.0f;
        for (int c = 0; c < channels; ++c) {
            value += pointers[size_t(c)][i];
        }
        return value;
    };
    auto magnitude = [&](qint64 i) {
        float value = 0.0f;
        for (int c = 0; c < channels; ++c) {
            value += std::fabs(pointers[size_t(c)][i]);
        }
        return value;
    };

    qint64 best = -1;
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    float previous = sum(0);
    for (qint64 i = 1; i < read; ++i) {
        const float current = sum(i);
        const bool crossing = (previous <= 0.0f && current >= 0.0f) || (previous >= 0.0f && current <= 0.0f);
        if (crossing) {
            const qint64 candidate = first + (magnitude(i) < magnitude(i - 1) ? i : i - 1);
            const qint64 distance = std::abs(candidate - frame);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = candidate;
            } else if (candidate > frame) {
                break;   // only farther crossings from here on
            }
        }
        previous = current;
    }

    return (best >= 0 && bestDistance <= maxDistance) ? best : -1;
}

TWaveformColumns::Source TWaveformColumns::query(const TPeakLevels& levels,
                                                 TWaveformSampleSource* samples,
                                                 const TTimeRef& start,
                                                 const TTimeRef& columnWidth,
                                                 int count,
                                                 TWaveformColumn* out)
{
    Q_ASSERT(columnWidth > TTimeRef());
    Q_ASSERT(count >= 0);

    std::fill(out, out + qsizetype(count) * levels.channel_count(), EmptyColumn);

    switch (source_for(levels, columnWidth)) {
    case Source::Samples:
        if (samples) {
            return from_samples(levels, samples, start, columnWidth, count, out);
        }
        // No samples to read (a recording in progress): the pyramid's
        // level-0 blocks are the finest there is
        return levels.level_count() > 0 ? from_pyramid(levels, start, columnWidth, count, out) : Source::None;
    case Source::Pyramid:
        return from_pyramid(levels, start, columnWidth, count, out);
    case Source::None:
        break;
    }
    return Source::None;
}

TWaveformColumns::Source TWaveformColumns::device_columns(const TPeakLevels& levels,
                                                          TWaveformSampleSource* samples,
                                                          const TTimeRef& start,
                                                          const TTimeRef& columnWidth,
                                                          int count,
                                                          const TTimeRef& clipStart,
                                                          const TTimeRef& clipEnd,
                                                          TWaveformColumn* out)
{
    const uint rate = levels.rate();
    const TWaveformGrid grid = rate > 0 && count > 0 ? TWaveformGrid::power_of_two(TTimeRef::to_frames(columnWidth, rate), rate)
                                                     : TWaveformGrid{};
    if (!grid.is_valid()) {
        return query(levels, samples, start, columnWidth, count, out);
    }

    // The grid column of every pixel: the one that holds its centre. A time
    // is at or past the start of grid column k when it is at or past
    // start_of(k) (the first universal frame of frame k << shift), so the
    // centres step through the columns' starts without a division.
    TTimeRef centre = start + TTimeRef(columnWidth.universal_frame() / 2);
    qint64 column = grid.column_of(std::max<qint64>(TTimeRef::frame_at_or_before(centre, rate), 0));
    TTimeRef next = grid.start_of(column + 1);
    const TTimeRef gridWidth = grid.column_width();
    std::vector<qint64> pixelColumn(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        while (centre >= next) {
            ++column;
            next += gridWidth;
        }
        pixelColumn[size_t(i)] = column;
        centre += columnWidth;
    }

    // The pixels whose grid column lies inside the clip: [inLeft, inRight).
    // Past the end of the source there is no audio to keep out: a clip that
    // reaches it (a whole file, a recording) has no edge there.
    const qint64 clipFirst = TTimeRef::frame_at_or_after(clipStart, rate);
    qint64 clipLast = TTimeRef::frame_at_or_after(clipEnd, rate);
    if (clipLast >= levels.frame_count()) {
        clipLast = std::numeric_limits<qint64>::max();
    }
    auto inside = [&](qint64 column) { return grid.first_frame(column) >= clipFirst && grid.first_frame(column + 1) <= clipLast; };
    int inLeft = 0;
    while (inLeft < count && !inside(pixelColumn[size_t(inLeft)])) {
        ++inLeft;
    }
    int inRight = count;
    while (inRight > inLeft && !inside(pixelColumn[size_t(inRight - 1)])) {
        --inRight;
    }
    if (inLeft == inRight) {
        return query(levels, samples, start, columnWidth, count, out);
    }

    // One grid column per pixel and no edge of the clip in view (a zoom of
    // exactly 2^s frames per pixel): the grid's columns straight into out
    const int channels = levels.channel_count();
    const qint64 firstColumn = pixelColumn[size_t(inLeft)];
    const int columns = int(pixelColumn[size_t(inRight - 1)] - firstColumn + 1);
    if (inLeft == 0 && inRight == count && columns == count) {
        if (walk_grid(levels, grid, firstColumn, count, out)) {
            return Source::Pyramid;
        }
        return query(levels, samples, grid.start_of(firstColumn), gridWidth, count, out);
    }
    std::vector<TWaveformColumn> gridColumns(size_t(columns) * size_t(channels));
    const Source source = walk_grid(levels, grid, firstColumn, columns, gridColumns.data())
        ? Source::Pyramid
        : query(levels, samples, grid.start_of(firstColumn), grid.column_width(), columns, gridColumns.data());
    for (int channel = 0; channel < channels; ++channel) {
        for (int i = inLeft; i < inRight; ++i) {
            out[qsizetype(channel) * count + i] = gridColumns[size_t(channel) * size_t(columns) + size_t(pixelColumn[size_t(i)] - firstColumn)];
        }
    }

    // The pixels at the clip's edges (at most two each), and those outside
    // it, exactly: the boundaries of query() over all pixels
    std::vector<TWaveformColumn> exact;
    auto exactly = [&](int from, int to) {
        if (from >= to) {
            return;
        }
        exact.resize(size_t(to - from) * size_t(channels));
        query(levels, samples, start + columnWidth * double(from), columnWidth, to - from, exact.data());
        for (int channel = 0; channel < channels; ++channel) {
            std::copy_n(exact.data() + size_t(channel) * size_t(to - from), to - from, out + qsizetype(channel) * count + from);
        }
    };
    exactly(0, inLeft);
    exactly(inRight, count);
    return source;
}

bool TWaveformColumns::walk_grid(const TPeakLevels& levels, const TWaveformGrid& grid, qint64 firstColumn, int count, TWaveformColumn* out)
{
    const int level = grid.shift - TPeakLevels::BlockFramesShift;
    const qint64 blocks = levels.block_count(0);
    if (level < 0 || level >= levels.level_count() || blocks > levels.pyramid_end()) {
        return false;
    }

    // Every channel from one record; the stored RMS is the column's (a
    // parent's RMS is that of its frames), decoded without squares or root
    const int channels = levels.channel_count();
    const qint64 records = levels.block_count(level);
    for (int i = 0; i < count; ++i) {
        const qint64 index = firstColumn + i;
        if (index < 0 || index >= records) {
            for (int channel = 0; channel < channels; ++channel) {
                out[qsizetype(channel) * count + i] = EmptyColumn;   // past the end of the source
            }
            continue;
        }
        const qint16* codes = levels.codes(level, index);
        for (int channel = 0; channel < channels; ++channel) {
            const int rms = std::max(int(codes[2 * channels + channel]), 0);
            out[qsizetype(channel) * count + i] = {TPeakLevels::to_value(codes[2 * channel]), TPeakLevels::to_value(codes[2 * channel + 1]),
                                                   TPeakLevels::magnitude_value(rms)};
        }
    }
    return true;
}

TWaveformColumns::Source TWaveformColumns::from_pyramid(const TPeakLevels& levels,
                                                        const TTimeRef& start, const TTimeRef& columnWidth, int count,
                                                        TWaveformColumn* out)
{
    const uint rate = levels.rate();
    const qint64 frames = levels.frame_count();
    const qint64 blocks = levels.block_count(0);
    const int channels = levels.channel_count();

    // The first level-0 block that starts at or after edge
    auto first_block_at = [&](const TTimeRef& edge) {
        const qint64 frame = std::clamp<qint64>(TTimeRef::frame_at_or_after(edge, rate), 0, frames);
        const qint64 block = (frame + TPeakLevels::BlockFrames - 1) >> TPeakLevels::BlockFramesShift;
        return std::min(block, blocks);
    };

    qint64 first = first_block_at(start);
    for (int i = 0; i < count; ++i) {
        const qint64 last = first_block_at(start + columnWidth * double(i + 1));
        if (last > first) {
            for (int channel = 0; channel < channels; ++channel) {
                double meanSquare = 0.0;
                const TPeakBlock block = levels.range(channel, first, last, meanSquare);
                out[qsizetype(channel) * count + i] = {TPeakLevels::to_value(block.min), TPeakLevels::to_value(block.max),
                                                       float(std::sqrt(meanSquare))};
            }
        }
        first = last;
    }

    return Source::Pyramid;
}

TWaveformColumns::Source TWaveformColumns::from_samples(const TPeakLevels& levels, TWaveformSampleSource* samples,
                                                        const TTimeRef& start, const TTimeRef& columnWidth, int count,
                                                        TWaveformColumn* out)
{
    const uint rate = levels.rate();
    const qint64 lastFrame = levels.frame_count() - 1;
    const int channels = levels.channel_count();

    // Every column edge: the frame at or before it and how far past it the edge is
    std::vector<qint64> edgeFrame(size_t(count) + 1);
    std::vector<double> edgeFraction(size_t(count) + 1);
    for (int i = 0; i <= count; ++i) {
        edgeFrame[size_t(i)] = TTimeRef::frame_at_or_before(start + columnWidth * double(i), rate, &edgeFraction[size_t(i)]);
    }

    // The frames the columns need, one neighbour outside included
    const qint64 first = std::max<qint64>(edgeFrame.front(), 0);
    const qint64 last = std::min(edgeFrame.back() + 1, lastFrame);
    if (first > last) {
        return Source::Samples;
    }

    const qint64 wanted = last - first + 1;
    std::vector<float> buffer(size_t(wanted) * size_t(channels));
    std::vector<float*> channelBuffers(static_cast<size_t>(channels));
    for (int channel = 0; channel < channels; ++channel) {
        channelBuffers[size_t(channel)] = buffer.data() + qsizetype(channel) * wanted;
    }

    const qint64 read = samples->read(first, wanted, channelBuffers.data());
    if (read <= 0) {
        return Source::None;
    }
    const qint64 available = first + std::min(read, wanted) - 1;   // last frame with a sample

    for (int channel = 0; channel < channels; ++channel) {
        const float* samplesOfChannel = channelBuffers[size_t(channel)];
        auto sample = [&](qint64 frame) { return samplesOfChannel[frame - first]; };
        TWaveformColumn* columns = out + qsizetype(channel) * count;

        // The value of the line through the samples at edge i, if the line is there
        auto edge_value = [&](int i, float& value) {
            const qint64 frame = edgeFrame[size_t(i)];
            const double fraction = edgeFraction[size_t(i)];
            if (frame < first || frame > available || (fraction > 0.0 && frame + 1 > available)) {
                return false;
            }
            value = fraction > 0.0
                ? float(sample(frame) + fraction * (sample(frame + 1) - sample(frame)))
                : sample(frame);
            return true;
        };

        float entry = 0.0f;
        bool hasEntry = edge_value(0, entry);

        for (int i = 0; i < count; ++i) {
            TWaveformColumn column = EmptyColumn;
            auto add = [&column](float value) {
                column.min = std::min(column.min, value);
                column.max = std::max(column.max, value);
            };

            float exit = 0.0f;
            const bool hasExit = edge_value(i + 1, exit);
            if (hasEntry) {
                add(entry);
            }
            if (hasExit) {
                add(exit);
            }

            // The samples inside the column: the first frame at or after its
            // left edge up to the first frame at or after its right edge
            const qint64 from = std::max(edgeFrame[size_t(i)] + (edgeFraction[size_t(i)] > 0.0 ? 1 : 0), first);
            const qint64 to = std::min(edgeFrame[size_t(i) + 1] + (edgeFraction[size_t(i) + 1] > 0.0 ? 1 : 0) - 1, available);
            double squares = 0.0;
            for (qint64 frame = from; frame <= to; ++frame) {
                add(sample(frame));
                squares += double(sample(frame)) * sample(frame);
            }
            if (to >= from) {
                column.rms = float(std::sqrt(squares / double(to - from + 1)));
            } else if (hasEntry || hasExit) {
                // Between two samples: the line's values at the edges
                const float a = hasEntry ? entry : exit;
                const float b = hasExit ? exit : entry;
                column.rms = float(std::sqrt((double(a) * a + double(b) * b) / 2.0));
            }

            columns[i] = column;
            entry = exit;
            hasEntry = hasExit;
        }
    }

    return Source::Samples;
}
