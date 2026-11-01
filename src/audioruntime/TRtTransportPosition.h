// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QtGlobal>
#include <atomic>

/**
 * @brief Where the transport was in the last processed buffer.
 *
 * universalFrame is the first frame of the buffer the audio thread processed
 * in that cycle, frames its length at sampleRate. runStartUniversalFrame is
 * where the transport started rolling for the current run (a seek starts a
 * new run). cycleTimeNanoseconds is when the cycle started, on the clock of
 * TClock::now_nanoseconds().
 */
struct TTransportPosition
{
    qint64  universalFrame{0};
    qint64  runStartUniversalFrame{0};
    uint64_t cycleTimeNanoseconds{0};
    uint32_t frames{0};
    uint32_t sampleRate{0};
};

/**
 * @brief A TTransportPosition the audio thread publishes once per cycle and
 *        any thread reads, without locks.
 *
 * One writer. A reader copies the fields and reads again when the writer was
 * busy meanwhile (sequence lock), so it never sees fields of two different
 * cycles. The writer never waits: a publish is a few relaxed stores.
 */
class TRtTransportPosition
{
public:
    static_assert(std::atomic<int64_t>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free,
                  "the transport position must be published without locks");

    // Audio thread only
    void publish(const TTransportPosition& position)
    {
        const uint64_t sequence = m_sequence.load(std::memory_order_relaxed);

        // Odd: a write is in progress. The fence keeps the field stores
        // after it, so a reader that sees a new field also sees the odd
        // sequence and reads again
        m_sequence.store(sequence + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);

        m_universalFrame.store(position.universalFrame, std::memory_order_relaxed);
        m_runStartUniversalFrame.store(position.runStartUniversalFrame, std::memory_order_relaxed);
        m_cycleTimeNanoseconds.store(position.cycleTimeNanoseconds, std::memory_order_relaxed);
        m_frames.store(position.frames, std::memory_order_relaxed);
        m_sampleRate.store(position.sampleRate, std::memory_order_relaxed);

        // Even again: a reader that sees this sequence sees all the fields
        m_sequence.store(sequence + 2, std::memory_order_release);
    }

    // Any thread
    TTransportPosition read() const
    {
        TTransportPosition position;

        for (;;) {
            const uint64_t before = m_sequence.load(std::memory_order_acquire);
            if (before & 1) {
                continue;
            }

            position.universalFrame = m_universalFrame.load(std::memory_order_relaxed);
            position.runStartUniversalFrame = m_runStartUniversalFrame.load(std::memory_order_relaxed);
            position.cycleTimeNanoseconds = m_cycleTimeNanoseconds.load(std::memory_order_relaxed);
            position.frames = m_frames.load(std::memory_order_relaxed);
            position.sampleRate = m_sampleRate.load(std::memory_order_relaxed);

            // Keeps the field loads before the sequence check
            std::atomic_thread_fence(std::memory_order_acquire);

            if (m_sequence.load(std::memory_order_relaxed) == before) {
                return position;
            }
        }
    }

private:
    std::atomic<uint64_t> m_sequence{0};
    std::atomic<qint64>   m_universalFrame{0};
    std::atomic<qint64>   m_runStartUniversalFrame{0};
    std::atomic<uint64_t> m_cycleTimeNanoseconds{0};
    std::atomic<uint32_t> m_frames{0};
    std::atomic<uint32_t> m_sampleRate{0};
};
