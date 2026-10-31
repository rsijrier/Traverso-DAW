// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

#include <algorithm>

// Reads of one kind: how many, their bytes and their time
struct TReadTotals
{
    uint64_t reads{0};
    uint64_t bytes{0};
    uint64_t nanoseconds{0};
    uint64_t worstNanoseconds{0};
    // Of the reads: the ones that met an error (with or without bytes);
    // every read's time counts, also one that gave nothing: a stalling NAS
    // shows there
    uint64_t failed{0};

    void add(uint64_t readBytes, uint64_t readNanoseconds)
    {
        ++reads;
        bytes += readBytes;
        nanoseconds += readNanoseconds;
        worstNanoseconds = std::max(worstNanoseconds, readNanoseconds);
    }

    void add(const TReadTotals& other)
    {
        reads += other.reads;
        bytes += other.bytes;
        nanoseconds += other.nanoseconds;
        worstNanoseconds = std::max(worstNanoseconds, other.worstNanoseconds);
        failed += other.failed;
    }
};

/**
 * @brief What a TByteSource measured of its reads: bytes and time, nothing
 * of what the bytes are.
 *
 * A read from the page cache shows only the copy of the memory, not the
 * medium, so the two are kept apart: cached is what came without waiting,
 * medium what had to wait for the disk or the network. Where the platform or
 * the file system cannot tell them apart the bytes are unclassified.
 */
struct TReadsByOrigin
{
    TReadTotals cached;
    TReadTotals medium;
    TReadTotals unclassified;

    uint64_t read_nanoseconds() const { return cached.nanoseconds + medium.nanoseconds + unclassified.nanoseconds; }

    void add(const TReadsByOrigin& other)
    {
        cached.add(other.cached);
        medium.add(other.medium);
        unclassified.add(other.unclassified);
    }
};
