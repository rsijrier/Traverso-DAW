// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The loops of the buffer operations, for TBufferOperations.cpp and
// TBufferOperationsAVX2.cpp: one source, compiled once per file with that
// file's target flags (T20, doc/design/t20-buffer-operations-measurement.md).
//
// Every function here is static: each file gets its own copy, built for its
// own target. No C++ library templates (std::max and the like) either: their
// copies are shared between files, so the linker could hand the AVX2 copy
// to the portable path, which then fails on a CPU without AVX2.

#include "TTypes.h"
#include "TRealTime.h"

#include <cmath>
#if defined(__AVX__)
#include <immintrin.h>
#elif defined(__SSE__)
#include <xmmintrin.h>
#endif

#if defined(__GNUC__)
// The vector types of the peak, per number of float lanes. Specialised with
// fixed sizes: GCC drops a vector_size that depends on a template parameter.
// Load: at any float address, no alignment, may alias the samples. A
// typedef, as the compilers' own __m128_u: Clang drops the aligned(4) of the
// same attributes in a using alias (an aligned load, a crash)
template <int Lanes> struct TFloatLanes;
template <> struct TFloatLanes<4> {
    typedef float Vector __attribute__((vector_size(16)));
    typedef float Load __attribute__((vector_size(16), aligned(4), may_alias));
};
template <> struct TFloatLanes<8> {
    typedef float Vector __attribute__((vector_size(32)));
    typedef float Load __attribute__((vector_size(32), aligned(4), may_alias));
};
#endif

// The peak of buf, or current when that is higher. Lanes: the floats per
// vector, 4 for the baseline (128 bit), 8 for AVX2 (256 bit)
template <int Lanes>
static inline float buffer_loop_update_peak(const audio_sample_t* buf, nframes_t nframes, float current) T_REALTIME_SAFE
{
    nframes_t i = 0;
#if defined(__GNUC__)
    // GCC and Clang (MinGW too): the maximum in four vectors of Lanes lanes.
    // A maximum in one chain is never vectorised: that would reorder it, which
    // the compiler may not do. Written in vectors, it is vectorised at -O2,
    // whatever the vectoriser decides. Four vectors: a max waits about four
    // cycles for the one before it, four chains keep the unit busy.
    using FloatN = typename TFloatLanes<Lanes>::Vector;
    using FloatNLoad = typename TFloatLanes<Lanes>::Load;
    static_assert(sizeof(FloatN) == Lanes * sizeof(float) && sizeof(FloatNLoad) == Lanes * sizeof(float));
    const auto absN = [](FloatN x) T_REALTIME_SAFE {
        for (int lane = 0; lane < Lanes; ++lane) {
            x[lane] = fabsf(x[lane]);
        }
        return x;
    };
    // a > b ? a : b per lane. On x86 as maxps: GCC makes the vector ternary
    // a compare and three logic operations in a chain (the old SSE assembly
    // was faster on short buffers for it). maxps is the same, NaN and the
    // signed zeros included, so the result is bit for bit the same
    const auto maxN = [](FloatN a, FloatN b) T_REALTIME_SAFE {
#if defined(__AVX__)
        if constexpr (Lanes == 8) {
            return FloatN(_mm256_max_ps(__m256(a), __m256(b)));
        }
#endif
#if defined(__SSE__)
        if constexpr (Lanes == 4) {
            return FloatN(_mm_max_ps(__m128(a), __m128(b)));
        }
#endif
        return a > b ? a : b;
    };

    FloatN start;
    for (int lane = 0; lane < Lanes; ++lane) {
        start[lane] = current;
    }
    FloatN m0 = start, m1 = start, m2 = start, m3 = start;
    for (; i + 4 * Lanes <= nframes; i += 4 * Lanes) {
        const FloatNLoad* p = reinterpret_cast<const FloatNLoad*>(buf + i);
        m0 = maxN(m0, absN(p[0]));
        m1 = maxN(m1, absN(p[1]));
        m2 = maxN(m2, absN(p[2]));
        m3 = maxN(m3, absN(p[3]));
    }
    const FloatN m = maxN(maxN(m0, m1), maxN(m2, m3));
    for (int lane = 0; lane < Lanes; ++lane) {
        current = m[lane] > current ? m[lane] : current;
    }
#endif
    for (; i < nframes; ++i) {
        const float sample = fabsf(buf[i]);
        current = sample > current ? sample : current;
    }
    return current;
}

static inline void buffer_loop_apply_gain(audio_sample_t* buf, nframes_t nframes, float gain) T_REALTIME_SAFE
{
    for (nframes_t i = 0; i < nframes; ++i) {
        buf[i] *= gain;
    }
}

// dst and src never overlap (TChannelBuffer::mix_buffers_*(), T64): with
// __restrict the compiler needs no overlap check, which GCC's -O2 cost
// model refuses, and then would not vectorise the loop
static inline void buffer_loop_mix_with_gain(audio_sample_t* __restrict dst, const audio_sample_t* __restrict src, nframes_t nframes, float gain) T_REALTIME_SAFE
{
    for (nframes_t i = 0; i < nframes; ++i) {
        dst[i] += src[i] * gain;
    }
}

static inline void buffer_loop_mix(audio_sample_t* __restrict dst, const audio_sample_t* __restrict src, nframes_t nframes) T_REALTIME_SAFE
{
    for (nframes_t i = 0; i < nframes; ++i) {
        dst[i] += src[i];
    }
}
