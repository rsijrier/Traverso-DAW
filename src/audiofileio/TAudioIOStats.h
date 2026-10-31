// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TByteSource.h"
#include "TByteStats.h"

#include <QtTypes>

#include <vector>

/**
 * @brief What one kind of decoder work cost: its calls and time, and the bytes
 * that went through its TByteSource during it.
 */
struct TAudioPhaseStats
{
    quint64 calls{0};
    quint64 frames{0};
    quint64 nanoseconds{0};
    TByteStats bytes;

    quint64 read_nanoseconds() const { return bytes.read_nanoseconds(); }
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
 * set_phase() points its sources at the bytes of the phase, so none can be
 * forgotten (WavPack has two, the .wv and its .wvc).
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
    // The sources that count into this; they outlive it or leave with it
    std::vector<TByteSource*> sources;

    TAudioPhaseStats& cost_of(Phase of)
    {
        switch (of) {
        case Phase::Open:
            return open;
        case Phase::Seek:
            return seek;
        case Phase::Stream:
            break;
        }
        return stream;
    }

    TAudioPhaseStats& current() { return cost_of(phase); }

    void set_phase(Phase to)
    {
        phase = to;
        for (TByteSource* source : sources) {
            source->set_stats(&current().bytes);
        }
    }
};
