// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QtGlobal>

#include <algorithm>
#include <array>

// The lowest and highest sample value of a block of frames as value codes
// (TPeakLevels::to_value()), rounded outwards, so the stored span always
// contains the real one.
struct TPeakBlock
{
    qint16 min;
    qint16 max;
};

// 2^(i / 1024) for the compiler, without std::exp2 (not constexpr): the
// binary digits of i / 1024 pick the roots 2^(1/2), 2^(1/4) .. 2^(1/1024),
// each a square root of the one before (Newton's method)
struct TPeakCodeMath
{
    static constexpr double square_root(double x)   // x in [1, 2]
    {
        double root = x;
        for (int i = 0; i < 32; ++i) {
            const double next = 0.5 * (root + x / root);
            if (next == root) {
                break;
            }
            root = next;
        }
        return root;
    }

    // 2^(1/2), 2^(1/4) .. 2^(1/1024)
    static constexpr std::array<double, 10> octave_roots()
    {
        std::array<double, 10> roots{};
        double root = 2.0;
        for (double& r : roots) {
            root = square_root(root);
            r = root;
        }
        return roots;
    }

    // i in [0, 1024), from the roots (computed once for a table)
    static constexpr double octave_fraction(int i, const std::array<double, 10>& roots)
    {
        double value = 1.0;
        for (int bit = 9; bit >= 0; --bit) {
            if (i & (1 << bit)) {
                value *= roots[size_t(9 - bit)];
            }
        }
        return value;
    }
    static constexpr double octave_fraction(int i) { return octave_fraction(i, octave_roots()); }
};

// The decoding tables of the value codes (TPeakLevels), built by the
// compiler. Code c > 0 is Octaves[c >> 10] * Mantissas[c & 1023]: octave
// c / 1024 and step c % 1024 within it. Code 0 is silence, -c is the value
// of c negated. Multiplying by a power of two is exact, so a value is as
// exact as its mantissa.
struct TPeakCodeTables
{
    static constexpr std::array<double, 10> Roots = TPeakCodeMath::octave_roots();

    static constexpr std::array<float, 1024> Mantissas = [] {
        std::array<float, 1024> mantissas{};
        for (int i = 0; i < 1024; ++i) {
            mantissas[size_t(i)] = float(TPeakCodeMath::octave_fraction(i, Roots));
        }
        return mantissas;
    }();

    // 2^(o - 28) for o = 0 .. 31
    static constexpr std::array<float, 32> Octaves = [] {
        std::array<float, 32> octaves{};
        float value = 1.0f;
        for (int o = 0; o < 28; ++o) {
            value *= 0.5f;
        }
        for (float& octave : octaves) {
            octave = value;
            value *= 2.0f;
        }
        return octaves;
    }();
};

/**
 * @brief Read-only view on a min/max and RMS pyramid of one audio source.
 *
 * Level 0 holds one block per BlockFrames frames of the source, at the
 * source's own sample rate. Every block has its min/max (TPeakBlock) and the
 * RMS of its frames as a code (nearest; a level-0 block with a NaN or an inf
 * sample gets its peak instead); a parent's RMS is the root of its children's
 * mean squares weighted by their frames. Block k of level L covers the frames
 * [k * BlockFrames * 2^L, (k + 1) * BlockFrames * 2^L), and is the
 * combination of blocks 2k and 2k + 1 of level L - 1. The last block of a
 * level may cover fewer frames; a level has half the blocks of the one below,
 * rounded up. There are levels 0 to ChunkLevel.
 *
 * A record per block: the min and max code of every channel, then the RMS
 * code of every channel (record_codes()). The records are kept in chunks of
 * 2^ChunkLevel level-0 blocks (TRAVPEAK version 4, doc/design/peak-records.md):
 * a chunk holds levels 0 to ChunkLevel of its blocks, each level an array of
 * records, level 0 first, and every chunk has the same size (the last one is
 * filled up with zeros). A record is found by index arithmetic, without a
 * table; pairs never cross a chunk, so a chunk's levels are the pyramid's,
 * cut to the chunk.
 *
 * The view does not own the records: TPeakFile (a mapped file) and
 * TPeakStream (a file being written, mapped for one read) do. A file being
 * written has its complete chunks, then level 0 of the chunk being written
 * (the tail): the levels above level 0 end with the complete chunks.
 */
class TPeakLevels
{
public:
    static constexpr int BlockFrames = 64;
    static constexpr int BlockFramesShift = 6;

    // Values (1.0 = full scale) are stored as int16 codes on a log scale:
    // code 0 is silence, code c > 0 is 2^((c - OneCode) / CodesPerOctave),
    // -c the same value negated. A step is 1/1024 octave (0.0059 dB) at any
    // level, from about -168 dB (code 1) to +24 dB (MaxCode); larger values
    // saturate. Codes compare like their values, so min and max work on
    // codes.
    static constexpr int CodesPerOctave = 1024;
    static constexpr int OneCode = 28 * CodesPerOctave;
    static constexpr int MaxCode = 32767;

    // The codes of a record: min and max of every channel, then the RMS of
    // every channel
    static constexpr int record_codes(int channelCount) { return 3 * channelCount; }

    // A chunk holds 2^ChunkLevel level-0 blocks and levels 0 to ChunkLevel
    // of them: 65536 frames, 1.4 s at 48 kHz. What this version writes; a
    // file says its own (TPeakFile).
    static constexpr int ChunkLevel = 10;
    // The records of a chunk, and where a level starts in it
    static constexpr qint64 chunk_records(int chunkLevel) { return (qint64(2) << chunkLevel) - 1; }
    static constexpr qint64 level_offset(int chunkLevel, int level)
    {
        return chunk_records(chunkLevel) - ((qint64(2) << (chunkLevel - level)) - 1);
    }

    TPeakLevels() = default;
    // chunks: the records of chunkCount complete chunks; tail: level 0 of the
    // blocks after them, up to frameCount (a file being written), or nullptr
    TPeakLevels(uint rate, int channelCount, qint64 frameCount, int chunkLevel, const qint16* chunks, qint64 chunkCount,
                const qint16* tail = nullptr);

    uint rate() const { return m_rate; }
    int channel_count() const { return m_channelCount; }
    qint64 frame_count() const { return m_frameCount; }

    // 0 when there are no blocks (yet); frame_count() and rate() still tell
    // about the source then
    int level_count() const { return m_frameCount > 0 ? m_chunkLevel + 1 : 0; }
    // The level's blocks. Above level 0, of a file being written: those of
    // its complete chunks.
    qint64 block_count(int level) const
    {
        const qint64 blocks = level == 0 ? level0_blocks() : std::min(level0_blocks(), pyramid_end());
        return (blocks + (qint64(1) << level) - 1) >> level;
    }
    // The level-0 blocks the levels above level 0 cover: those of the
    // complete chunks
    qint64 pyramid_end() const { return m_chunkCount << m_chunkLevel; }
    TPeakBlock block(int level, int channel, qint64 index) const
    {
        const qint16* codes = record(level, index);
        return {codes[2 * channel], codes[2 * channel + 1]};
    }
    qint16 rms(int level, int channel, qint64 index) const { return record(level, index)[2 * m_channelCount + channel]; }
    // The codes of one block, every channel at once: the min and max code of
    // every channel, then the RMS code of every channel (record_codes())
    const qint16* codes(int level, qint64 index) const { return record(level, index); }
    // The frames block index of level covers (the source's last one fewer)
    qint64 block_frames(int level, qint64 index) const
    {
        const qint64 start = index << (BlockFramesShift + level);
        return std::min(start + (qint64(BlockFrames) << level), m_frameCount) - start;
    }

    // The combined block of the level-0 blocks [first, last) of channel,
    // from the coarsest levels that fit. first < last <= block_count(0).
    // Reads min/max only.
    TPeakBlock range(int channel, qint64 first, qint64 last) const;
    // The same blocks' min/max and mean square (their RMS squared, weighted
    // by their frames)
    TPeakBlock range(int channel, qint64 first, qint64 last, double& meanSquare) const;

    // The value of a code's magnitude, 0 .. MaxCode
    static float magnitude_value(int magnitude)
    {
        return magnitude == 0 ? 0.0f
            : TPeakCodeTables::Octaves[size_t(magnitude >> 10)] * TPeakCodeTables::Mantissas[size_t(magnitude & 1023)];
    }
    static float to_value(qint16 code)
    {
        // Clamped: -32768 is never written, but a file may hold anything
        const float value = magnitude_value(std::min(code < 0 ? -int(code) : int(code), MaxCode));
        return code < 0 ? -value : value;
    }
    // The codes on both sides of value: the smallest code whose value is >=
    // value, the largest whose value is <= value (MaxCode or -MaxCode when
    // value is beyond the range: saturated). NaN gives 0.
    static qint16 code_at_or_above(float value);
    static qint16 code_at_or_below(float value);
    // The code whose value is nearest (RMS: no side to keep)
    static qint16 code_nearest(float value);
    static float max_value() { return to_value(MaxCode); }

private:
    qint64 level0_blocks() const { return (m_frameCount + BlockFrames - 1) >> BlockFramesShift; }

    const qint16* record(int level, qint64 index) const
    {
        Q_ASSERT(index >= 0 && index < block_count(level));
        const qint64 codes = record_codes(m_channelCount);
        if (level == 0 && index >= pyramid_end()) {
            return m_tail + (index - pyramid_end()) * codes;
        }
        const int shift = m_chunkLevel - level;
        const qint64 chunk = index >> shift;
        const qint64 local = index & ((qint64(1) << shift) - 1);
        return m_chunks + (chunk * chunk_records(m_chunkLevel) + level_offset(m_chunkLevel, level) + local) * codes;
    }

    uint m_rate{0};
    int m_channelCount{0};
    qint64 m_frameCount{0};
    int m_chunkLevel{ChunkLevel};
    const qint16* m_chunks{nullptr};
    qint64 m_chunkCount{0};
    const qint16* m_tail{nullptr};
};
