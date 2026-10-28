// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TAudioDevice with the Dummy driver, the safety net for its
// races (T53) and the singleton removal (T58): start,
// add a client, cycles, free-wheeling and back, the CPU time, remove the
// client (no call after), switch the driver to another buffer size, shut
// down. Run it under ThreadSanitizer too (TRAVERSO_TSAN). The xrun storm
// (T54) needs a driver that makes xruns: the test audio driver of the
// headless scenario work (step 8).

#include "TAudioDevice.h"
#include "TAudioDeviceClient.h"
#include "TAudioDeviceSetup.h"
#include "TProcessContext.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <atomic>
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

// The client: what the cycles it saw were like
struct Probe
{
    std::atomic<int> cycles{0};
    std::atomic<nframes_t> nframes{0};
    std::atomic<bool> realTime{false};

    int process(TProcessContext& context) T_REALTIME_SAFE
    {
        nframes.store(context.get_nframes_to_process());
        realTime.store(context.get_is_real_time());
        cycles.fetch_add(1);
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
        QThread::msleep(2);
    }
    return done();
}

static TAudioDeviceSetup dummy_setup(nframes_t bufferSize)
{
    TAudioDeviceSetup setup;
    setup.set_driver_type("Dummy");
    setup.set_sample_rate(48000);
    setup.set_buffer_size(bufferSize);
    return setup;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TAudioDevice device;

    // Start
    device.set_parameters(dummy_setup(256));
    check(device.get_driver_type() == "Dummy" && device.get_buffer_size() == 256, "the Dummy driver starts at 256 frames");

    // A client and its cycles
    Probe probe;
    TAudioDeviceClient client(QStringLiteral("test client"));
    client.set_process_callback(TProcessCallBack::from_method<Probe, &Probe::process>(&probe));
    device.add_client(&client);
    check(wait_for(app, [&] { return probe.cycles.load() >= 5; }), "the client gets cycles");
    check(client.is_connected(), "the client is connected");
    check(probe.nframes.load() == 256, "a cycle is the buffer size");
    check(probe.realTime.load(), "a cycle runs in real time");

    // Free-wheeling and back: the cycles see the mode
    device.set_free_wheeling(true);
    int from = probe.cycles.load();
    check(wait_for(app, [&] { return probe.cycles.load() >= from + 5 && !probe.realTime.load(); }), "free-wheeling: the cycles see it");
    check(!device.running_real_time(), "free-wheeling: the device says so");
    device.set_free_wheeling(false);
    from = probe.cycles.load();
    check(wait_for(app, [&] { return probe.cycles.load() >= from + 5 && probe.realTime.load(); }), "back to real time: the cycles see it");

    // The CPU time: a share of the time since the last read, never negative
    device.get_cpu_time();
    from = probe.cycles.load();
    wait_for(app, [&] { return probe.cycles.load() >= from + 20; });
    const float cpu = device.get_cpu_time();
    check(cpu >= 0.0f && cpu <= 100.0f, "the CPU time is a share between 0 and 100");

    // Remove: no call after it returns
    device.remove_client(&client);
    check(!client.is_connected(), "the client is disconnected once remove_client() returns");
    const int afterRemove = probe.cycles.load();
    QElapsedTimer quiet;
    quiet.start();
    while (quiet.elapsed() < 100) {
        app.processEvents();
        QThread::msleep(2);
    }
    check(probe.cycles.load() == afterRemove, "no cycle reaches a removed client");

    // Switch the driver: another buffer size, the client again
    device.set_parameters(dummy_setup(512));
    check(device.get_buffer_size() == 512, "the driver restarts at 512 frames");
    device.add_client(&client);
    from = probe.cycles.load();
    check(wait_for(app, [&] { return probe.cycles.load() >= from + 5 && probe.nframes.load() == 512; }), "the client gets cycles of 512 frames");
    device.remove_client(&client);

    // Shut down
    check(device.shutdown() != 0, "the device shuts down");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all audio device tests passed\n");
    return 0;
}
