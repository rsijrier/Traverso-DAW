// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QtTypes>

#include <algorithm>

// Reads of one kind: how many, their bytes and their time
struct TByteTransfer
{
    quint64 reads{0};
    quint64 bytes{0};
    quint64 nanoseconds{0};
    quint64 worstNanoseconds{0};
    // Of the reads: the ones that met an error (with or without bytes);
    // every read's time counts, also one that gave nothing: a stalling NAS
    // shows there
    quint64 failed{0};

    void add(quint64 readBytes, quint64 readNanoseconds)
    {
        ++reads;
        bytes += readBytes;
        nanoseconds += readNanoseconds;
        worstNanoseconds = std::max(worstNanoseconds, readNanoseconds);
    }

    void add(const TByteTransfer& other)
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
 * the file system cannot tell them apart the bytes are untold.
 */
struct TByteStats
{
    TByteTransfer cached;
    TByteTransfer medium;
    TByteTransfer untold;

    quint64 read_nanoseconds() const { return cached.nanoseconds + medium.nanoseconds + untold.nanoseconds; }

    void add(const TByteStats& other)
    {
        cached.add(other.cached);
        medium.add(other.medium);
        untold.add(other.untold);
    }
};
