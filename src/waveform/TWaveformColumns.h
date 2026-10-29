// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakPyramid.h"
#include "TTimeRef.h"

// One drawn column of a waveform: the lowest and highest value of the signal
// inside the column and its RMS, 1.0 = full scale. A column without signal
// (before the start or after the end of the source) has min > max.
struct TWaveformColumn
{
    float min;
    float max;
    float rms;

    bool is_empty() const { return min > max; }
};

// Columns of 2^shift source frames, aligned in source time: column k covers
// the frames [k << shift, (k + 1) << shift). On such columns the query
// visits about one record per column (an aligned range has no odd edges
// below the top level), a fraction of the cost of columns of any width.
// Built from frames; a time is only derived from frames.
struct TWaveformGrid
{
    // From a level-0 block per column on: below, a query reads the samples,
    // and that costs the same on columns of any width
    static constexpr int MinShift = TPeakLevels::BlockFramesShift;
    static constexpr int MaxShift = 30;

    uint rate{0};
    int shift{-1};      // -1: no grid, the exact query

    // The smallest 2^s at or above framesPerPixel at rate. No grid for s
    // below MinShift (32 frames per pixel or fewer), nor where 2^s frames are
    // not a whole number of universal frames (a rate that does not divide
    // UNIVERSAL_SAMPLE_RATE): the grid's edges would not be exact.
    static TWaveformGrid power_of_two(double framesPerPixel, uint rate);
    bool is_valid() const { return shift >= 0; }

    qint64 column_of(qint64 frame) const { return frame >> shift; }
    qint64 first_frame(qint64 column) const { return column << shift; }
    // Exact: built from frames
    TTimeRef start_of(qint64 column) const { return TTimeRef(uint64_t(first_frame(column)), rate); }
    TTimeRef column_width() const { return TTimeRef(uint64_t(1) << shift, rate); }
};

// Gives the samples of a source to TWaveformColumns
class TWaveformSampleSource
{
public:
    virtual ~TWaveformSampleSource() = default;

    // Reads count frames from firstFrame (>= 0) into channels[c][0 .. count)
    // for every channel. Returns the number of frames read.
    virtual qint64 read(qint64 firstFrame, qint64 count, float* const* channels) = 0;
};

/**
 * @brief Waveform columns for a view: the model decides where they come from.
 *
 * Column i covers the source time [start + i * width, start + (i + 1) * width).
 * The frames of the source that are inside it belong to it, so the columns
 * partition the frames. The source of the data is chosen for the whole
 * request, so a view shows one kind of data at a time:
 *
 * - Samples, when a column holds fewer than SourceThreshold frames: the signal
 *   is the line through the samples, a column holds the samples inside it and
 *   the line's values at both its edges. The columns of a line touch, and a
 *   column between two samples holds the part of the line that crosses it.
 * - Pyramid otherwise: a level-0 block belongs to the column that holds its
 *   first frame, the column combines its blocks from the coarsest levels that
 *   fit (O(log) per column at any zoom). Values are rounded outwards by at
 *   most one code step (TPeakLevels, 0.0059 dB). The RMS is the root of the
 *   blocks' mean squares weighted by their frames, within a step.
 *
 * From the samples a column's RMS is that of the samples inside it (between
 * two samples: of the line's values at its edges).
 *
 * No interpolation between pyramid levels: every column shows values that are
 * in the audio, so spikes do not change size while zooming.
 */
class TWaveformColumns
{
public:
    enum class Source
    {
        None,       // nothing to show, or the pyramid is needed but missing
        Samples,
        Pyramid
    };

    // Frames per column below which the samples are read
    static constexpr double SourceThreshold = TPeakLevels::BlockFrames;

    // Which source a request with this column width uses
    static Source source_for(const TPeakLevels& levels, const TTimeRef& columnWidth);

    // The highest |sample| of all channels in the frames [first, last): the
    // partial blocks at both ends from the samples, the blocks between from
    // the pyramid in O(log n). Never below the real maximum and at most one
    // code step (0.0059 dB) above it; below the smallest code (about
    // -168 dB) it is that code's value. Only blocks beyond the code range
    // (+24 dB, saturated) are read from the samples. 0 for an empty range.
    static float max_amplitude(const TPeakLevels& levels, TWaveformSampleSource* samples, qint64 first, qint64 last);

    // The frame of the zero crossing nearest to frame, at most maxDistance
    // frames away, or -1. A crossing is between two frames where the signal
    // changes sign (or touches zero); the frame returned is the one nearer
    // to zero of the two. With several channels the crossing is where the sum
    // of the channels' |values| is smallest. Reads a window of the samples.
    static qint64 nearest_zero_crossing(const TPeakLevels& levels, TWaveformSampleSource* samples, qint64 frame, qint64 maxDistance);

    // Fills out[channel * count + i] for every channel of levels.
    // samples may be nullptr if the request never needs them.
    static Source query(const TPeakLevels& levels,
                        TWaveformSampleSource* samples,
                        const TTimeRef& start,
                        const TTimeRef& columnWidth,
                        int count,
                        TWaveformColumn* out);

    // One column per device pixel, for a raster view: pixel i covers
    // [start + i * columnWidth, start + (i + 1) * columnWidth), as in query().
    // From more than 32 frames per pixel on, a pixel shows the column of the
    // grid (TWaveformGrid::power_of_two()) that holds the pixel's centre: a grid
    // column is 1 to 2 pixels wide, every grid column in view shows in at
    // least one pixel, so no peak is lost, and a peak is at most a grid
    // column (2 pixels) from its place. A grid column that reaches outside
    // [clipStart, clipEnd) would show audio from outside the clip: its
    // pixels come from query() (a clip that reaches the end of the source has
    // no edge there). Without a grid, query(). The source is the one the
    // grid's columns came from.
    static Source device_columns(const TPeakLevels& levels,
                                 TWaveformSampleSource* samples,
                                 const TTimeRef& start,
                                 const TTimeRef& columnWidth,
                                 int count,
                                 const TTimeRef& clipStart,
                                 const TTimeRef& clipEnd,
                                 TWaveformColumn* out);

private:
    // The grid columns [firstColumn, firstColumn + count) of a finished file,
    // straight from the records of level shift - 6: an aligned grid column is
    // one record there. False when that does not hold (a file being written,
    // a level above the chunk): then nothing is written.
    // Temporary: goes together with the QPainter view's query
    // (from_pyramid()) once the GPU view is there (doc/design/waveform-grid.md
    // section 7).
    static bool walk_grid(const TPeakLevels& levels, const TWaveformGrid& grid, qint64 firstColumn, int count, TWaveformColumn* out);
    static Source from_samples(const TPeakLevels& levels, TWaveformSampleSource* samples,
                               const TTimeRef& start, const TTimeRef& columnWidth, int count,
                               TWaveformColumn* out);
    static Source from_pyramid(const TPeakLevels& levels,
                               const TTimeRef& start, const TTimeRef& columnWidth, int count,
                               TWaveformColumn* out);
};
