// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TRealTime.h"
#include <chrono>
#include <cstdint>

struct TClock
{
    // A monotonic clock for measuring durations, not a time of day, any thread
    static uint64_t now_nanoseconds() T_REALTIME_SAFE
    {
        // The steady clock reads the vDSO: real-time safe by contract
        T_REALTIME_SAFE_BY_CONTRACT(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();)
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    }
};
