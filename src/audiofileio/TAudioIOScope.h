// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TAudioIOStats.h"
#include "TClock.h"

/**
 * @brief Measures one seek, read or open of a decoder: from here on the sources
 * count into phase; when it ends, also by an early return or an error, the time
 * and the call go to phase's cost and the phase before it comes back.
 */
class TAudioIOScope
{
public:
    TAudioIOScope(TAudioIOStats& stats, TAudioIOStats::Phase phase)
        : m_stats(&stats)
        , m_phase(phase)
        , m_previous(stats.phase)
        , m_start(now_nanoseconds())
    {
        stats.set_phase(phase);
    }

    ~TAudioIOScope()
    {
        TAudioPhaseStats& cost = m_stats->cost_of(m_phase);
        cost.nanoseconds += now_nanoseconds() - m_start;
        ++cost.calls;
        m_stats->set_phase(m_previous);
    }

    TAudioIOScope(const TAudioIOScope&) = delete;
    TAudioIOScope& operator=(const TAudioIOScope&) = delete;

    // An open starts before there is a decoder: its measurement ends on the
    // decoder's stats, which go on from where they are
    void move_to(TAudioIOStats& stats)
    {
        m_stats = &stats;
        m_previous = stats.phase;
    }

private:
    TAudioIOStats* m_stats;
    TAudioIOStats::Phase m_phase;
    TAudioIOStats::Phase m_previous;
    uint64_t m_start;
};
