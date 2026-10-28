// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the denormal protection (task T51), in a process without
// Traverso, so nothing set the bits before: a cycle of the Dummy driver runs
// with flush-to-zero (and denormals-are-zero where the CPU has it) on the
// audio thread, and when something on that thread clears them (a plugin),
// the next cycle sets them again.

#include "TAudioDevice.h"
#include "TAudioDeviceClient.h"
#include "TAudioDeviceSetup.h"
#include "TDenormals.h"
#include "TProcessContext.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <atomic>
#include <cstdio>

#if defined(TRAVERSO_DENORMALS_MXCSR)
#include <xmmintrin.h>
#define TEST_MXCSR 1
#endif

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

// The client: what each cycle saw on the audio thread, and on request it
// clears the bits after looking, as a plugin could
struct Probe
{
    std::atomic<int> cycles{0};
    std::atomic<bool> active{false};
    std::atomic<bool> clearNow{false};
    std::atomic<int> clearedAt{-1};

    int process(TProcessContext&) T_REALTIME_SAFE
    {
        active.store(TDenormals::active_on_this_thread());
        const int cycle = cycles.fetch_add(1) + 1;
#if defined(TEST_MXCSR)
        if (clearNow.exchange(false)) {
            _mm_setcsr(_mm_getcsr() & ~TDenormals::mask());
            clearedAt.store(cycle);
        }
#endif
        return 0;
    }
};

// Lets the device and its message queue run until done() or 5 seconds
template <typename Done>
static bool wait_for(QCoreApplication& app, Done done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 5000) {
        app.processEvents();
        QThread::msleep(5);
    }
    return done();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TAudioDevice device;

    std::printf("denormals on the audio threads: %s\n", TDenormals::mode_name());
#if !defined(TEST_MXCSR)
    std::printf("no SSE: nothing to test\n");
    return 0;
#else
    if (TDenormals::mask() == 0) {
        std::printf("no bits to set (valgrind, or a CPU without FTZ): nothing to test\n");
        return 0;
    }
    check((TDenormals::mask() & 0x8000) != 0, "flush-to-zero is among the bits");

    // The bits clear here, so the audio thread does not inherit them
    _mm_setcsr(_mm_getcsr() & ~TDenormals::mask());
    check(!TDenormals::active_on_this_thread(), "the main thread starts without them");

    TAudioDeviceSetup setup;
    setup.set_driver_type("Dummy");
    device.set_parameters(setup);

    Probe probe;
    TAudioDeviceClient client(QStringLiteral("denormals probe"));
    client.set_process_callback(TProcessCallBack::from_method<Probe, &Probe::process>(&probe));
    device.add_client(&client);

    const bool ran = wait_for(app, [&] { return probe.cycles.load() >= 3; });
    check(ran, "the Dummy driver runs cycles");
    check(probe.active.load(), "a cycle runs with the bits set on the audio thread");

    probe.clearNow.store(true);
    const bool cleared = wait_for(app, [&] { return probe.clearedAt.load() > 0 && probe.cycles.load() >= probe.clearedAt.load() + 3; });
    check(cleared, "cycles after the bits were cleared on the audio thread");
    check(probe.active.load(), "the next cycles set them again");

    device.remove_client(&client);
    device.shutdown();

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all denormal tests passed\n");
    return 0;
#endif
}
