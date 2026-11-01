// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TAudioDecoder.h"
#include "TClock.h"

/**
 * @brief Measures one seek, read or open: when it ends, also by an early return
 * or an error, the time and the call go to the cost. For a decoder it sets the
 * activity at the start, so its sources count there, and puts the one before it
 * back. An open has no decoder yet: its sources were pointed at the cost once.
 */
class TAudioIOScope
{
public:
    explicit TAudioIOScope(TAudioIOCost& cost)
        : m_cost(cost)
        , m_start(TClock::now_nanoseconds())
    {
    }

    TAudioIOScope(TAudioDecoder& decoder, TAudioIOStats::Activity activity)
        : m_cost(decoder.m_stats.cost_of(activity))
        , m_decoder(&decoder)
        , m_previous(decoder.m_stats.activity)
        , m_start(TClock::now_nanoseconds())
    {
        decoder.set_activity(activity);
    }

    ~TAudioIOScope()
    {
        m_cost.nanoseconds += TClock::now_nanoseconds() - m_start;
        ++m_cost.calls;
        if (m_decoder) {
            m_decoder->set_activity(m_previous);
        }
    }

    TAudioIOScope(const TAudioIOScope&) = delete;
    TAudioIOScope& operator=(const TAudioIOScope&) = delete;

private:
    TAudioIOCost& m_cost;
    TAudioDecoder* m_decoder{nullptr};
    TAudioIOStats::Activity m_previous{};
    uint64_t m_start;
};
