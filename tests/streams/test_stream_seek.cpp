// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the seeks of a stream reader, the real stream on the real
// scheduler, without the rest of core: the frames of a scenario file are in
// its samples, so every slot in the ring shows what it holds. A refill made
// before a seek is thrown away. Seeks one after the other while refills run:
// after each, every slot holds the frames of its own location, from the seek
// on, and none of an earlier seek.

#include "TAudioBuffer.h"
#include "TAudioIOStats.h"
#include "TBufferedAudioStreamReader.h"
#include "TIOGroup.h"
#include "TIOScheduler.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TQueueBufferSlot.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <random>
#include <semaphore>
#include <thread>
#include <vector>

// What the stream needs of core: no project
TProjectManager& pm()
{
    alignas(TProjectManager) static char nothing[sizeof(TProjectManager)]{};
    return *reinterpret_cast<TProjectManager*>(nothing);
}
TProject* TProjectManager::get_project()
{
    return nullptr;
}
QString TProject::get_root_dir() const
{
    return QString();
}
uint TProject::get_rate() const
{
    return 48000;
}

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

static constexpr uint Rate = 48000;
static constexpr nframes_t Frames = 120 * Rate;
static constexpr nframes_t BufferSize = 1024;
// 2^24: the scenario's ramp is the frame / 2^24
static constexpr float RampScale = 16777216.0f;

// The stream's friend: a stream takes part in a project through it
class TResourcesManager
{
public:
    static void use(TBufferedAudioStreamReader& stream) { stream.ref(); }
};

class Stream final : public TBufferedAudioStreamReader
{
public:
    Stream(const QString& dir, const QString& name) : TBufferedAudioStreamReader(dir, name) {}

    // The slots in the ring, as the audio thread takes them: back to the free
    // queue. Each one holds the frames of its own location, none before
    // first; the count of slots, -1 when a slot is wrong.
    int take_slots(nframes_t first)
    {
        TChannelBuffer channel(BufferSize);
        TQueueBufferSlot* slot = nullptr;
        int slotCount = 0;
        bool right = true;
        while (m_rtBufferSlotsQueue->try_dequeue(slot)) {
            const nframes_t location = TTimeRef::to_nframes(TTimeRef::frame_nearest(slot->get_file_location(), Rate));
            slot->read_buffer(channel, 0, BufferSize);
            const float* samples = channel.get_data(BufferSize);
            right = right && location >= first;
            for (nframes_t frame = 0; frame < BufferSize; frame += 97) {
                right = right && samples[frame] == float(location + frame) / RampScale;
            }
            m_freeBufferSlotsQueue->try_enqueue(slot);
            ++slotCount;
        }
        return right ? slotCount : -1;
    }
};

static TTimeRef at_frame(nframes_t frame)
{
    return TTimeRef(frame, Rate);
}

// A refill made before a seek arrives after it: it adds nothing
static void test_stale_refill(Stream& stream)
{
    TIOStream& io = stream;
    TIOStream::StepInfo seek;
    seek.seek = true;
    seek.seekLocation = seek.transportLocation = at_frame(10 * BufferSize);
    seek.generation = io.next_generation();
    check(io.step(seek) >= 0, "a seek step");
    check(stream.take_slots(10 * BufferSize) > 0, "the seek fills the ring with its own frames");

    // A seek to the very frame the reader is at: a full reset all the same
    TAudioBuffer buffer;
    (void)stream.file_read(buffer, at_frame(19 * BufferSize), BufferSize);
    const quint64 seeks = stream.io_stats().seek.calls;
    seek.seekLocation = seek.transportLocation = at_frame(20 * BufferSize);
    seek.generation = io.next_generation();
    check(io.step(seek) >= 0, "a seek step to where the reader is");
    check(stream.io_stats().seek.calls == seeks + 1, "it resets the decoder");
    check(stream.take_slots(20 * BufferSize) > 0, "and fills the ring");

    TIOStream::StepInfo refill;
    refill.generation = io.generation();
    io.next_generation();
    check(io.step(refill) == -1, "a refill of an older generation says nothing");
    check(stream.take_slots(0) == 0, "and puts nothing in the ring");

    refill.generation = io.generation();
    check(io.step(refill) >= 0 && stream.take_slots(0) > 0, "a refill of this generation reads");
}

// Seeks one after the other on the scheduler, the audio thread's side taking
// slots between them so refills run while the next seek comes
static void test_seeks_while_refilling(Stream& stream)
{
    TIOScheduler scheduler;
    scheduler.set_output_sample_rate(Rate);
    scheduler.set_buffer_size(BufferSize);
    TIOGroup group;
    group.set_playing(true);
    std::binary_semaphore done{0};
    QObject::connect(&scheduler, &TIOScheduler::seekFinished, &scheduler, [&done](TIOGroup*) { done.release(); }, Qt::DirectConnection);
    scheduler.start();
    scheduler.add_group(&group);
    scheduler.add_stream(&stream, &group);

    std::atomic<bool> stop{false};
    std::thread ticks([&] {
        while (!stop.load()) {
            scheduler.tick();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    std::mt19937 random(2026);
    std::uniform_int_distribution<nframes_t> place(0, (Frames - 2 * Rate) / BufferSize);
    int wrong = 0;
    int slotCount = 0;
    for (int seek = 0; seek < 500; ++seek) {
        const nframes_t first = place(random) * BufferSize;
        group.request_seek(at_frame(first));
        done.acquire();
        const int taken = stream.take_slots(first);
        wrong += taken < 0 ? 1 : 0;
        slotCount += std::max(taken, 0);
        // Time for a refill to start before the next seek, now and then
        if (seek % 3 == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        // What that refill added belongs to this seek still, and goes
        // with the next one
    }
    stop.store(true);
    ticks.join();
    scheduler.remove_stream(&stream);
    scheduler.remove_group(&group);

    std::printf("500 seeks, %d slots looked at\n", slotCount);
    check(slotCount > 0, "the seeks filled the ring");
    check(wrong == 0, "no slot of an earlier position after a seek");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }
    QFile file(dir.filePath(QStringLiteral("ramp.tscenario")));
    const QByteArray scenario = QStringLiteral(R"(# tscenario 1
{"rate": %1, "frames": %2, "channels": [{"signal": "ramp"}]})").arg(Rate).arg(Frames).toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(scenario) != scenario.size()) {
        std::printf("no scenario\n");
        return 1;
    }
    file.close();

    Stream stream(dir.path() + QLatin1Char('/'), QStringLiteral("ramp.tscenario"));
    TResourcesManager::use(stream);
    check(stream.init() == 1, "the stream opens its file");
    stream.set_stream_location_window(TTimeRef(), at_frame(Frames));
    stream.set_active(true);
    TIOStream& io = stream;
    io.prepare_ring_buffers(BufferSize);

    test_stale_refill(stream);
    test_seeks_while_refilling(stream);

    io.retire();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all stream seek tests passed\n");
    return 0;
}
