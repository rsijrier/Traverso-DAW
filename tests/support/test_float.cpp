// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TFloatCompare: what its comparisons treat as equal at
// the magnitudes Traverso uses them for (gains and pans around 1, zoom
// factors, lengths and positions in universal frames up to about 1e11).
// The tolerances are absolute: MarginDouble = 1e-13, MarginFloat = 1e-6.

#include "TFloatCompare.h"

#include <cmath>
#include <cstdio>
#include <limits>


static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

int main()
{
    // Doubles around 1 (curve values, normalised fade shapes): rounding
    // differences count as equal, real differences do not
    check(TFloatCompare::compare(1.0, 1.0), "equal doubles are equal");
    check(TFloatCompare::compare(0.1 + 0.2, 0.3), "0.1 + 0.2 equals 0.3");
    check(!TFloatCompare::compare(1.0, 1.0 + 1.0e-12), "1 and 1 + 1e-12 differ");
    check(TFloatCompare::equals_1(0.7 / 0.7), "0.7 / 0.7 equals 1");
    check(TFloatCompare::equals_0(0.3 - 0.1 - 0.2), "0.3 - 0.1 - 0.2 equals 0");

    // Floats around 1 (gains, pans): about eight representable steps of tolerance
    check(TFloatCompare::compare(1.0f, 1.0f + 5.0e-7f), "float gain 1 and 1 + 5e-7 are equal");
    check(!TFloatCompare::compare(1.0f, 1.0f + 1.0e-5f), "float gain 1 and 1 + 1e-5 differ");
    check(TFloatCompare::equals_0(1.0e-7f), "a float gain of 1e-7 (-140 dB) counts as 0");
    check(!TFloatCompare::equals_0(1.0e-5f), "a float gain of 1e-5 (-100 dB) does not count as 0");

    // Universal frames: one hour is about 1e11. Neighbouring doubles there
    // are about 1.5e-5 apart, far more than 1e-13, so compare() is an exact
    // comparison: only identical values are equal. Rounding differences of
    // computed positions count as different.
    const double hour = 101606400000.0;
    const double next = std::nextafter(hour, 2 * hour);
    check(!TFloatCompare::compare(hour, next), "neighbouring doubles at one hour differ (exact comparison)");
    check(TFloatCompare::compare(hour, hour), "identical positions are equal");
    check(TFloatCompare::compare(1.0e7, 1.0e7), "identical lengths are equal");

    // Tiny values: differences below 1e-13 vanish, relative size does not count
    check(TFloatCompare::compare(1.0e-15, 9.0e-15), "1e-15 and 9e-15 count as equal");

    // Not a number is never equal, also not to itself
    const double nan = std::numeric_limits<double>::quiet_NaN();
    check(!TFloatCompare::compare(nan, nan) && !TFloatCompare::equals_0(nan), "NaN is never equal");

    // Infinity minus infinity is NaN, so infinity is not equal to itself
    const double inf = std::numeric_limits<double>::infinity();
    check(!TFloatCompare::compare(inf, inf), "infinity is not equal to itself");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all Float tests passed\n");
    return 0;
}
