// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TResamplerChoice.h"
#include <atomic>
#include "TTimeRef.h"
#include "TRealTime.h"
#include "TTypes.h"

/**
 * @brief What TIOScheduler asks of a stream it reads or writes: its state
 * for the planner, one step, and its ring buffers. The readers and writers in
 * core implement it; the reading, decoding, encoding and writing are theirs.
 */
class TIOStream
{
public:
    // What a stream needs now, for the planner (one look at its buffers)
    struct State
    {
        bool needsWork{false};
        // A reader whose fill decides about idle work: not a writer, not a
        // reader out of its window (nothing to read there, it would keep the
        // idle work away for good). Only the readers of songs that play count.
        bool countsForFill{false};
        int fill{100};      // percent of the ring that holds data
        // Its seek step is to come (its song seeks)
        bool seekPending{false};
        // Its song plays, or plays once its seek is done; a writer has no song
        bool playing{false};
    };

    // The resampler of a stream that reads at another rate than its file's:
    // a TAudioResampler backend and one of its qualities, and whether to
    // resample at all, as the owner chose them in its thread; the scheduler
    // only hands them on. The zeroed choice (libsamplerate at its best,
    // most expensive quality) is no chosen default: std::atomic needs one,
    // and the owner sets the choice before any stream comes
    // (TIOScheduler::set_resampler()).
    // alignas(8): 8 bytes, so the scheduler's std::atomic of it is lock-free
    // (the static_assert below the class)
    struct alignas(8) Resampler
    {
        TResamplerChoice choice{};
        // false: the file is read at its own rate (and plays at another
        // pitch when the device's rate is not the file's)
        bool resample{true};
        bool operator==(const Resampler&) const = default;
    };

    // What a step needs to know: the scheduler makes a copy for every step,
    // the step only reads it
    struct StepInfo
    {
        bool seek{false};
        TTimeRef seekLocation;
        TTimeRef transportLocation;
        // The stream's generation when the step was made: a step of an older
        // one is not wanted any more
        quint64 generation{0};
        bool sampleRateChanged{false};
        uint outputRate{0};
        Resampler resampler;
        nframes_t bufferSize{0};
    };

    virtual ~TIOStream() = default;

    // Raised for every seek of the stream, not per song: a stream also seeks
    // on its own, to get back in sync. Work of an older generation is thrown
    // away where it arrives; it is not stopped. Any thread.
    quint64 next_generation() { return m_generation.fetch_add(1) + 1; }
    quint64 generation() const { return m_generation.load(); }


    // What the scheduler needs to know, the transport being at
    // transportLocation: whether it has work, whether its fill decides about
    // the idle work, its fill (one look at its buffers). On the scheduler
    // thread, never while a step of the stream runs.
    virtual TIOStream::State disk_state(const TTimeRef& transportLocation) = 0;
    // One step: a seek, back in sync, or read or write all there is room
    // for. The fill to show for it, -1 for nothing (out of sync). On a
    // worker thread, one step of a stream at a time.
    virtual int step(const StepInfo& info) = 0;
    // On the owner's thread before add_stream() returns, then on the
    // scheduler thread, never while a step of the stream runs
    virtual void set_output_rate_and_resampler(int outputRate, const Resampler& resampler) = 0;
    // Also asked in the audio thread
    virtual uint get_channel_count() const T_REALTIME_SAFE = 0;
    // The ring buffers are made and freed only where the audio thread does
    // not use them: the scheduler knows when that is
    virtual void prepare_ring_buffers(nframes_t bufferSize) = 0;
    // The scheduler hands the stream back when it takes it out, on its
    // thread, when no step of it runs: the ring buffers go. Its owner deletes
    // it once remove_stream() returned.
    virtual void retire() = 0;

private:
    std::atomic<quint64> m_generation{0};
};

// 8 bytes: the scheduler keeps it in a lock-free std::atomic (after the
// class: its default member initialisers are usable only once it is complete)
static_assert(sizeof(TIOStream::Resampler) == 8 && std::atomic<TIOStream::Resampler>::is_always_lock_free);
