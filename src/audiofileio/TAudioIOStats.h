// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QtTypes>

#include <algorithm>

// Reads of one kind: how many, their bytes and their time
struct TAudioTransfer
{
    quint64 reads{0};
    quint64 bytes{0};
    quint64 nanoseconds{0};
    quint64 worstNanoseconds{0};

    void add(quint64 readBytes, quint64 readNanoseconds)
    {
        ++reads;
        bytes += readBytes;
        nanoseconds += readNanoseconds;
        worstNanoseconds = std::max(worstNanoseconds, readNanoseconds);
    }

    void add(const TAudioTransfer& other)
    {
        reads += other.reads;
        bytes += other.bytes;
        nanoseconds += other.nanoseconds;
        worstNanoseconds = std::max(worstNanoseconds, other.worstNanoseconds);
    }
};

/**
 * @brief What one kind of decoder work cost: its calls and time, and the bytes
 * that went through TAudioByteSource during it.
 *
 * A read from the page cache shows only the copy of the memory, not the
 * medium, so the two are kept apart: cached is what came without waiting,
 * medium what had to wait for the disk or the network. Where the platform or
 * the file system cannot tell them apart the bytes are untold.
 */
struct TAudioPhaseStats
{
    quint64 calls{0};
    quint64 frames{0};
    quint64 nanoseconds{0};
    TAudioTransfer cached;
    TAudioTransfer medium;
    TAudioTransfer untold;

    quint64 read_nanoseconds() const { return cached.nanoseconds + medium.nanoseconds + untold.nanoseconds; }
    // The rest of the calls' time: the library's decoding and the copies
    quint64 decode_nanoseconds() const
    {
        const quint64 read = read_nanoseconds();
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
 * Plain counters of one decoder, for the thread that uses the decoder.
 */
struct TAudioIOStats
{
    enum class Phase {
        Open,
        Seek,
        Stream
    };

    Phase phase{Phase::Open};
    TAudioPhaseStats open;
    TAudioPhaseStats seek;
    TAudioPhaseStats stream;
    // The medium the file is on (st_dev), 0 where unknown: files on the same
    // medium wait for each other
    quint64 mediumId{0};

    TAudioPhaseStats& current()
    {
        switch (phase) {
        case Phase::Open:
            return open;
        case Phase::Seek:
            return seek;
        case Phase::Stream:
            break;
        }
        return stream;
    }
};
