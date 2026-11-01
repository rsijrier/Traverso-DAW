// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless tests of the published transport position: the lock-free
// publish/read pair never hands out a mix of two cycles, and the estimate of
// the heard position follows the audio clock within its clamps.

#include "TRtTransportPosition.h"
#include "TTransportPositionEstimator.h"
#include "TTimeRef.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

static const uint32_t rate = 48000;
static const uint32_t frames = 512;
static const uint64_t period = 1000000000ULL * frames / rate;   // nanoseconds

static TTransportPosition cycle(int64_t bufferStartFrame, int64_t runStartFrame, uint64_t cycleTime)
{
    return TTransportPosition{
        .universalFrame = TTimeRef(nframes_t(bufferStartFrame), rate).universal_frame(),
        .runStartUniversalFrame = TTimeRef(nframes_t(runStartFrame), rate).universal_frame(),
        .cycleTimeNanoseconds = cycleTime,
        .frames = frames,
        .sampleRate = rate
    };
}

static int64_t frame_of(const TTimeRef& position)
{
    return TTimeRef::frame_nearest(position, rate);
}

static void test_publish_read()
{
    TRtTransportPosition published;

    check(published.read().sampleRate == 0, "nothing published yet");

    published.publish(cycle(1000, 0, 123456789));
    const TTransportPosition read = published.read();
    check(read.universalFrame == TTimeRef(nframes_t(1000), rate).universal_frame()
          && read.runStartUniversalFrame == 0 && read.cycleTimeNanoseconds == 123456789
          && read.frames == frames && read.sampleRate == rate, "a published position is read back");

    // A writer publishing as fast as it can, a reader checking that all
    // fields of a read belong to the same publish. Starts from the pattern
    // of i = 0, so the first reads are consistent too
    published.publish(TTransportPosition{.universalFrame = 0, .runStartUniversalFrame = 0,
                                         .cycleTimeNanoseconds = 0, .frames = 0, .sampleRate = 1});
    std::atomic<bool> stop{false};
    std::atomic<bool> mixed{false};
    std::atomic<int64_t> reads{0};

    std::thread writer([&]() {
        for (int64_t i = 1; !stop.load(); ++i) {
            published.publish(TTransportPosition{
                .universalFrame = i,
                .runStartUniversalFrame = i * 2,
                .cycleTimeNanoseconds = uint64_t(i * 3),
                .frames = uint32_t(i % 1000),
                .sampleRate = uint32_t(i % 7 + 1)
            });
        }
    });

    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < end) {
        const TTransportPosition p = published.read();
        const int64_t i = p.universalFrame;
        if (p.runStartUniversalFrame != i * 2 || p.cycleTimeNanoseconds != i * 3
            || p.frames != uint32_t(i % 1000) || p.sampleRate != uint32_t(i % 7 + 1)) {
            mixed.store(true);
        }
        reads.fetch_add(1);
    }
    stop.store(true);
    writer.join();

    check(!mixed.load(), "a concurrent reader never sees fields of two publishes");
    check(reads.load() > 1000, "the reader is not starved by the writer");
}

static void test_estimator()
{
    const TTimeRef latency(nframes_t(2 * frames), rate);   // two buffers to the output

    // Steady playback from frame 0: the heard position is the published
    // buffer plus the elapsed time, minus the output latency
    {
        TTransportPositionEstimator estimator;
        // Cycle 10 started at t = 10 periods, so its buffer (frames 5120..5631)
        // is heard from t = 12 periods on
        const TTransportPosition p = cycle(10 * frames, 0, 10 * period);
        const TTimeRef atCycleStart = estimator.heard_position(p, 10 * period, latency);
        check(frame_of(atCycleStart) == 8 * frames, "at the cycle start the buffer of two cycles ago is heard");
        const TTimeRef halfway = estimator.heard_position(p, 10 * period + period / 2, latency);
        check(frame_of(halfway) == 8 * frames + frames / 2, "half a period later the position moved half a buffer");
    }

    // Start of a run: nothing before the start location is heard while the
    // first sound is in flight
    {
        TTransportPositionEstimator estimator;
        const TTransportPosition first = cycle(1000, 1000, 0);
        check(frame_of(estimator.heard_position(first, 0, latency)) == 1000, "at the first cycle the run start is shown");
        check(frame_of(estimator.heard_position(first, period, latency)) == 1000, "one period later still the run start");
        const TTransportPosition third = cycle(1000 + 2 * frames, 1000, 2 * period);
        check(frame_of(estimator.heard_position(third, 2 * period + period / 4, latency)) == 1000 + frames / 4,
              "after the latency the position moves on from the run start");
    }

    // Never backwards within a run: a GUI tick whose time is earlier than the
    // previous one returns the previous position
    {
        TTransportPositionEstimator estimator;
        const TTransportPosition p = cycle(10 * frames, 0, 10 * period);
        const TTimeRef later = estimator.heard_position(p, 10 * period + period / 2, latency);
        const TTimeRef earlier = estimator.heard_position(p, 10 * period + period / 4, latency);
        check(earlier == later, "an earlier tick time does not move the position backwards");
    }

    // A frame time before a cycle just published: a negative elapsed time, no
    // jump
    {
        TTransportPositionEstimator estimator;
        const TTransportPosition p = cycle(10 * frames, 0, 10 * period);
        const TTimeRef early = estimator.heard_position(p, 10 * period - period / 2, latency);
        check(frame_of(early) == 8 * frames - frames / 2, "a frame time before the cycle start gives half a buffer earlier");
    }

    // A late audio thread: the position stops at the end of the published buffer
    {
        TTransportPositionEstimator estimator;
        const TTransportPosition p = cycle(10 * frames, 0, 10 * period);
        const TTimeRef stalled = estimator.heard_position(p, 10 * period + 5 * period, latency);
        check(frame_of(stalled) == 11 * frames, "a stalled audio thread stops the position at the end of its buffer");
    }

    // A seek backwards starts a new run: the position may go back
    {
        TTransportPositionEstimator estimator;
        estimator.heard_position(cycle(100 * frames, 0, 100 * period), 100 * period, latency);
        const TTransportPosition afterSeek = cycle(20 * frames, 20 * frames, 101 * period);
        check(frame_of(estimator.heard_position(afterSeek, 101 * period, latency)) == 20 * frames,
              "a seek backwards shows the new run start");
    }

    // A seek forwards starts a new run too: no sound of the new run is heard
    // before its start location
    {
        TTransportPositionEstimator estimator;
        estimator.heard_position(cycle(20 * frames, 0, 20 * period), 20 * period + period / 2, latency);
        const TTransportPosition afterSeek = cycle(200 * frames, 200 * frames, 21 * period);
        check(frame_of(estimator.heard_position(afterSeek, 21 * period, latency)) == 200 * frames,
              "a seek forwards shows the new run start");
    }

    // Stop and start again from the same location: reset() starts the run over
    {
        TTransportPositionEstimator estimator;
        estimator.heard_position(cycle(100 * frames, 0, 100 * period), 100 * period + period / 2, latency);
        estimator.reset();
        check(frame_of(estimator.heard_position(cycle(0, 0, 200 * period), 200 * period, latency)) == 0,
              "after a stop the next run starts at its own start location");
    }

    // Nothing published yet: the position is what was published (zero)
    {
        TTransportPositionEstimator estimator;
        check(estimator.heard_position(TTransportPosition{}, 12345, latency) == TTimeRef(),
              "without a published position the estimate is zero");
    }
}

int main()
{
    test_publish_read();
    test_estimator();

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all transport position tests passed\n");
    return 0;
}
