// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TReadsByOrigin.h"

#include <cstdint>

/**
 * @brief What one kind of decoder work cost: its calls and time, and the bytes
 * that went through its TByteSource during it.
 */
struct TAudioIOCost
{
    uint64_t calls{0};
    uint64_t frames{0};
    uint64_t nanoseconds{0};
    TReadsByOrigin reads;

    uint64_t read_nanoseconds() const { return reads.read_nanoseconds(); }
    // The rest of the calls' time: the library's decoding and the copies
    uint64_t decode_nanoseconds() const
    {
        const uint64_t read = read_nanoseconds();
        return nanoseconds > read ? nanoseconds - read : 0;
    }
};

/**
 * @brief What a decoder read and decoded, per kind of work, since it opened.
 *
 * Three kinds, because they answer different questions: open (the format
 * probes and the decoder's own open, which can read the whole file), seek (a
 * seek's reads are not sequential, and a seek can decode, so it stays out of
 * the medium's latency) and stream (the reads a scheduler can plan).
 *
 * Plain counters of one decoder, for the thread that uses the decoder. The
 * decoder points its sources at the reads of the current activity.
 */
struct TAudioIOStats
{
    enum class Activity {
        Open,
        Seek,
        Stream
    };

    Activity activity{Activity::Open};
    TAudioIOCost open;
    TAudioIOCost seek;
    TAudioIOCost stream;
    // The medium the file is on (st_dev), 0 where unknown: files on the same
    // medium wait for each other
    uint64_t mediumId{0};

    TAudioIOCost& cost_of(Activity of)
    {
        switch (of) {
        case Activity::Open:
            return open;
        case Activity::Seek:
            return seek;
        case Activity::Stream:
            break;
        }
        return stream;
    }

    TAudioIOCost& current_cost() { return cost_of(activity); }
};
