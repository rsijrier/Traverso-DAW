// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TRealTime.h"

#include <cmath>

/**
 * @brief Compares floats without the ambiguity of ==: on the audio path a difference
 * below the margin is no difference. A float's margin is 1e-6, -120 dB; a double's
 * 1e-13.
 */
class TFloatCompare
{
public:
    static constexpr float MarginFloat = 0.000001f;
    static constexpr double MarginDouble = 0.0000000000001;

    static bool compare(float a, float b) T_REALTIME_SAFE { return std::fabs(a - b) <= MarginFloat; }
    static bool compare(double a, double b) T_REALTIME_SAFE { return std::fabs(a - b) <= MarginDouble; }
    static bool equals_1(float a) T_REALTIME_SAFE { return std::fabs(a - 1.0f) <= MarginFloat; }
    static bool equals_1(double a) T_REALTIME_SAFE { return std::fabs(a - 1.0) <= MarginDouble; }
    static bool equals_0(float a) T_REALTIME_SAFE { return std::fabs(a) <= MarginFloat; }
    static bool equals_0(double a) T_REALTIME_SAFE { return std::fabs(a) <= MarginDouble; }
};
