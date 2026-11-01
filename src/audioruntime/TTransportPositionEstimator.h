// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TRtTransportPosition.h"
#include "TTimeRef.h"

/**
 * @brief Estimates the position the listener hears now from the position the
 *        audio thread published last.
 *
 * The published buffer starts at universalFrame; its first frame leaves the
 * output outputLatency after its cycle started. So at time now the listener
 * hears universalFrame + (now - cycleTime) - outputLatency. Clamped:
 * - never before the start of the run: the first sound is still in flight;
 * - never backwards within a run: the GUI and the audio thread do not read
 *   the clock at the same moments;
 * - never past the end of the published buffer: what the audio thread has
 *   not processed cannot be heard, e.g. when it is late.
 * A new run (a transport start, or a seek: the run start changes or the
 * published position jumps backwards) starts the clamps over.
 *
 * Times come from the caller, so the estimate can be tested without threads
 * or clocks.
 */
class TTransportPositionEstimator
{
public:
    // GUI thread. outputLatency: from the start of a cycle to the first
    // frame of its buffer leaving the output
    TTimeRef heard_position(const TTransportPosition& published, uint64_t nowNanoseconds, const TTimeRef& outputLatency)
    {
        if (published.sampleRate == 0) {
            // Nothing published yet
            return TTimeRef(published.universalFrame);
        }

        if (!m_inRun
            || published.runStartUniversalFrame != m_runStartUniversalFrame
            || published.universalFrame < m_lastPublishedUniversalFrame) {
            m_inRun = true;
            m_runStartUniversalFrame = published.runStartUniversalFrame;
            m_floor = TTimeRef(published.runStartUniversalFrame);
        }
        m_lastPublishedUniversalFrame = published.universalFrame;

        const TTimeRef bufferStart(published.universalFrame);
        const TTimeRef bufferEnd = bufferStart + TTimeRef(nframes_t(published.frames), uint(published.sampleRate));
        // Negative when the frame time lies before a cycle just published
        const TTimeRef elapsed = TTimeRef::from_nanoseconds(int64_t(nowNanoseconds - published.cycleTimeNanoseconds));

        TTimeRef heard = bufferStart + elapsed - outputLatency;

        if (heard > bufferEnd) {
            heard = bufferEnd;
        }
        if (heard < m_floor) {
            heard = m_floor;
        }

        m_floor = heard;
        return heard;
    }

    // The transport stopped: the next position starts a new run
    void reset()
    {
        m_inRun = false;
    }

private:
    bool     m_inRun{false};
    qint64   m_runStartUniversalFrame{0};
    qint64   m_lastPublishedUniversalFrame{0};
    TTimeRef m_floor;
};
