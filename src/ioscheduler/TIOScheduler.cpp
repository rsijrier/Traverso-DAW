/*
Copyright (C) 2006-2024 Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#include "TIOScheduler.h"

#include "TIOStream.h"
#include "Debugger.h"
#include "TLog.h"

#include <QFile>
#include <QFileInfo>
#include <QThreadPool>

#include <algorithm>
#include <climits>

#if defined (Q_OS_LINUX)
// idle_limit_for(): the block device of a directory
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#endif

std::atomic<quint64> TIOScheduler::s_handOverFence{0};

TIOScheduler::TIOScheduler(int threads)
    : m_lastStepLoadTime(TTimeRef::get_nanoseconds_since_epoch())
{
    m_pool.setMaxThreadCount(std::max(threads, 2));
}

TIOScheduler::~TIOScheduler()
{
    PENTERDES;

    // No new work: the scheduler thread wakes and ends, the steps that run
    // end (they use the streams and tasks), then the tasks that are left go
    // back to their owners
    m_stopRequested.store(true);
    if (m_thread) {
        m_wake.signal();
        m_thread->wait();
    }
    m_pool.waitForDone();
    Q_ASSERT_X(m_streams.empty() && m_streamsToAdd.empty(), "TIOScheduler", "streams are still registered");
    Q_ASSERT_X(m_groups.empty() && m_groupsToAdd.empty(), "TIOScheduler", "groups are still registered");
    for (const std::unique_ptr<TIOIdleTask>& task : m_tasksToAdd) {
        task->retire();
    }
    for (const std::unique_ptr<TaskWork>& task : m_idleTasks) {
        task->task->retire();
    }
}

void TIOScheduler::start()
{
    Q_ASSERT(!m_thread);
    m_thread.reset(QThread::create([this] { run(); }));
    m_thread->start();
}

bool TIOScheduler::on_scheduler_thread() const
{
    return m_thread ? QThread::currentThread() == m_thread.get() : QThread::currentThread() == thread();
}

void TIOScheduler::run()
{
    TLog::name_thread("diskio");

    while (true) {
        // The wake: the audio thread's tick every cycle (also with the
        // transport stopped), a request, or a step that is done; the wakes
        // that came meanwhile are taken with it
        m_wake.wait();
        while (m_wake.tryWait()) {
        }
        if (m_stopRequested.load()) {
            break;
        }
        dispatch();
    }

    TLOG_I("diskio", "stopped");
}

bool TIOScheduler::dispatch()
{
    take_requests();
    take_reports();
    const bool idle = start_steps();
    if (!m_thread) {
        // No scheduler thread (a test): the steps are done when it returns
        m_pool.waitForDone();
        take_reports();
    }
    return idle;
}

void TIOScheduler::retire_stream(StreamWork* work)
{
    if (work->seek) {
        for (GroupWork& group : m_groups) {
            if (group.group == work->group) {
                --group.seeksLeft;
            }
        }
    }
    // The ring buffers go here, where the audio thread does not use them;
    // the owner deletes the stream once remove_stream() returns
    work->stream->retire();
    std::erase_if(m_streams, [work](const std::unique_ptr<StreamWork>& w) { return w.get() == work; });
}

void TIOScheduler::take_requests()
{
    Q_ASSERT_X(on_scheduler_thread(), "TIOScheduler::take_requests", "not on the scheduler thread");

    // Under the lock only the swap: a request never waits for retire()
    std::vector<TIOGroup*> groups;
    std::vector<StreamWork> added;
    std::vector<Removal*> removals;
    std::vector<std::unique_ptr<TIOIdleTask>> tasks;
    {
        std::lock_guard locker(m_requestsMutex);
        groups.swap(m_groupsToAdd);
        added.swap(m_streamsToAdd);
        removals.swap(m_removals);
        tasks.swap(m_tasksToAdd);
    }

    for (TIOGroup* group : groups) {
        m_groups.push_back(GroupWork{group});
    }
    for (const StreamWork& work : added) {
        Q_ASSERT(std::none_of(m_streams.begin(), m_streams.end(),
                              [&work](const std::unique_ptr<StreamWork>& w) { return w->stream == work.stream; }));
        m_streams.push_back(std::make_unique<StreamWork>(work));
    }
    for (std::unique_ptr<TIOIdleTask>& task : tasks) {
        m_idleTasks.push_back(std::make_unique<TaskWork>(TaskWork{std::move(task)}));
    }

    // A stream whose step runs goes once the step is done (take_reports())
    std::vector<Removal*> done;
    for (Removal* removal : removals) {
        if (removal->group) {
            Q_ASSERT_X(std::none_of(m_streams.begin(), m_streams.end(),
                                    [removal](const std::unique_ptr<StreamWork>& w) { return w->group == removal->group; }),
                       "TIOScheduler::remove_group", "the group still has streams");
            std::erase_if(m_groups, [removal](const GroupWork& w) { return w.group == removal->group; });
            done.push_back(removal);
            continue;
        }
        const auto it = std::find_if(m_streams.begin(), m_streams.end(),
                                     [removal](const std::unique_ptr<StreamWork>& w) { return w->stream == removal->stream; });
        if (it == m_streams.end()) {
            // Never added (no channels)
            done.push_back(removal);
        } else if ((*it)->inFlight) {
            (*it)->removal = removal;
        } else {
            retire_stream(it->get());
            done.push_back(removal);
        }
    }

    // remove_stream() and remove_group() return now
    if (!done.empty()) {
        {
            std::lock_guard locker(m_requestsMutex);
            for (Removal* removal : done) {
                removal->done = true;
            }
        }
        m_removalDone.notify_all();
    }
}

void TIOScheduler::report(const Report& report)
{
    {
        std::lock_guard locker(m_reportsMutex);
        m_reports.push_back(report);
    }
    m_wake.signal();
}

void TIOScheduler::take_reports()
{
    Q_ASSERT_X(on_scheduler_thread(), "TIOScheduler::take_reports", "not on the scheduler thread");

    // The lock orders what the steps wrote before what this thread does next
    std::vector<Report> reports;
    {
        std::lock_guard locker(m_reportsMutex);
        reports.swap(m_reports);
    }
    if (reports.empty()) {
        return;
    }

    trav_time_t stepTime = 0;
    int lowest = 100;
    std::vector<Removal*> done;
    for (const Report& report : reports) {
        --m_running;
        if (report.idle) {
            --m_runningIdle;
        }
        stepTime += report.nanoseconds;

        if (report.task) {
            report.task->inFlight = false;
            // An idle task that is over goes (a build reports its result then)
            if (report.over) {
                report.task->task->retire();
                std::erase_if(m_idleTasks, [&report](const std::unique_ptr<TaskWork>& t) { return t.get() == report.task; });
            }
            continue;
        }

        StreamWork* work = report.stream;
        work->inFlight = false;
        // For the GUI: the emptiest stream in sync since it last asked (a
        // reading it takes meanwhile may miss a low: it is a meter)
        if (report.fill >= 0) {
            lowest = std::min(lowest, report.fill);
        }
        // A seek done: the stream reads at the rate and with the rings of the
        // seek, and its song has one stream less to seek. A fill of -1 counts
        // too: mostly a stream out of its window (nothing to read there); a
        // seek that really failed the stream tells itself, and it seeks to
        // the transport again with its next step, one a tick.
        if (report.info.seek) {
            work->seek = false;
            work->rate = report.info.outputRate;
            work->bufferSize = report.info.bufferSize;
            for (GroupWork& group : m_groups) {
                if (group.group == work->group) {
                    --group.seeksLeft;
                }
            }
        }
        if (work->resamplerPending) {
            work->resamplerPending = false;
            work->stream->set_output_rate_and_resampler(int(work->rate), get_resampler());
        }
        if (work->removal) {
            done.push_back(work->removal);
            retire_stream(work);
        }
    }

    m_stepTime.fetch_add(stepTime, std::memory_order_relaxed);
    if (lowest < m_lowestBufferFill.load(std::memory_order_relaxed)) {
        m_lowestBufferFill.store(lowest, std::memory_order_relaxed);
    }

    finish_seeks();

    if (!done.empty()) {
        {
            std::lock_guard locker(m_requestsMutex);
            for (Removal* removal : done) {
                removal->done = true;
            }
        }
        m_removalDone.notify_all();
    }
}

void TIOScheduler::finish_seeks()
{
    for (GroupWork& group : m_groups) {
        if (group.seeking && group.seeksLeft <= 0) {
            group.seeking = false;
            group.group->m_seeking.store(false, std::memory_order_relaxed);
            emit seekFinished(group.group);
        }
    }
}

TIOStream::StepInfo TIOScheduler::step_info(const StreamWork& work, uint outputRate, nframes_t bufferSize,
                                           const TIOStream::Resampler& resampler) const
{
    TIOStream::StepInfo info;
    // A writer has no song: no transport, no seek
    if (work.group) {
        info.transportLocation = TTimeRef(work.group->m_transportLocation.load(std::memory_order_relaxed));
    }
    info.seek = work.seek;
    info.generation = work.stream->generation();
    info.seekLocation = info.transportLocation;
    // A new rate or buffer size goes with the stream's next seek: its rings
    // are made again there, where the audio thread does not use them
    info.sampleRateChanged = work.seek && (work.rate != outputRate || work.bufferSize != bufferSize);
    info.outputRate = outputRate;
    info.resampler = resampler;
    info.bufferSize = work.seek ? bufferSize : 0;
    return info;
}

bool TIOScheduler::start_steps()
{
    Q_ASSERT_X(on_scheduler_thread(), "TIOScheduler::start_steps", "not on the scheduler thread");

    const uint outputRate = get_output_rate();
    const nframes_t bufferSize = m_bufferSize.load(std::memory_order_relaxed);
    const TIOStream::Resampler resampler = get_resampler();
    const quint64 tick = m_ticks.load(std::memory_order_relaxed);

    // A new resampler: every stream at the rate it reads at now (a new rate
    // comes with the stream's next seek), a stream whose step runs after it
    if (m_resamplerChanged.exchange(false)) {
        for (const std::unique_ptr<StreamWork>& work : m_streams) {
            if (work->inFlight) {
                work->resamplerPending = true;
            } else {
                work->stream->set_output_rate_and_resampler(int(work->rate), resampler);
            }
        }
    }

    // A song's seek (the location is written before the flag): its streams
    // seek to the location, which is where its transport is from now on (the
    // audio thread does not move it while the song seeks). A stream whose
    // step runs seeks after it.
    for (GroupWork& group : m_groups) {
        if (!group.group->m_seekRequested.exchange(false, std::memory_order_acquire)) {
            continue;
        }
        group.group->m_transportLocation.store(group.group->m_seekLocation.load(std::memory_order_relaxed),
                                               std::memory_order_relaxed);
        group.seeking = true;
        group.seeksLeft = 0;
        for (const std::unique_ptr<StreamWork>& work : m_streams) {
            if (work->group == group.group) {
                work->seek = true;
                work->stream->next_generation();
                ++group.seeksLeft;
            }
        }
        TLOG_D("diskio", "seek requested, {} streams seek", group.seeksLeft);
    }
    // A song without streams is done at once
    finish_seeks();

    // What may start: the streams and idle tasks that run no step now. A
    // stream takes one refill step per tick: one that cannot make progress
    // (the end of its file, out of its window, an error) does not spin.
    bool idleAllowed = true;
    bool playingSeek = false;
    int idleLimit = m_groups.empty() ? m_idleLimit.load(std::memory_order_relaxed) : INT_MAX;
    for (const GroupWork& group : m_groups) {
        idleAllowed = idleAllowed && !group.group->m_recording.load(std::memory_order_relaxed);
        idleLimit = std::min(idleLimit, group.group->idle_limit());
        playingSeek = playingSeek || (group.seeking && group.group->m_playing.load(std::memory_order_relaxed));
    }
    std::vector<TIOStream::State> states;
    std::vector<StreamWork*> streams;
    for (const std::unique_ptr<StreamWork>& work : m_streams) {
        if (work->inFlight) {
            continue;
        }
        const bool playing = work->group && work->group->m_playing.load(std::memory_order_relaxed);
        if (work->seek) {
            states.push_back(TIOStream::State{false, false, 100, true, playing});
        } else {
            const TTimeRef transport = work->group ? TTimeRef(work->group->m_transportLocation.load(std::memory_order_relaxed)) : TTimeRef();
            TIOStream::State state = work->stream->disk_state(transport);
            state.playing = playing;
            state.needsWork = state.needsWork && work->stepTick != tick;
            states.push_back(state);
        }
        streams.push_back(work.get());
    }
    std::vector<TaskWork*> tasks;
    std::vector<qint64> weights;
    qint64 heaviestRunning = -1;
    for (const std::unique_ptr<TaskWork>& task : m_idleTasks) {
        if (!task->inFlight) {
            tasks.push_back(task.get());
            weights.push_back(task->task->weight());
        } else {
            heaviestRunning = std::max(heaviestRunning, task->weight);
        }
    }

    // Idle work never takes the last thread: a refill finds one at once
    const int threads = m_pool.maxThreadCount();
    const int idleBudget = std::min(std::max(idleLimit, 1), threads - 1) - m_runningIdle;
    const TIOPlanner::Plan plan = m_planner.plan(states, playingSeek, weights, heaviestRunning, idleAllowed, threads - m_running,
                                                 idleBudget);

    for (const int index : plan.streams) {
        StreamWork* work = streams[size_t(index)];
        start_stream_step(work, false, states[size_t(index)].fill, step_info(*work, outputRate, bufferSize, resampler), tick);
    }
    for (const int index : plan.idleSeeks) {
        StreamWork* work = streams[size_t(index)];
        start_stream_step(work, true, states[size_t(index)].fill, step_info(*work, outputRate, bufferSize, resampler), tick);
    }
    for (const int index : plan.idleTasks) {
        start_task_step(tasks[size_t(index)]);
    }

    return !plan.idleSeeks.empty() || !plan.idleTasks.empty();
}

void TIOScheduler::start_stream_step(StreamWork* work, bool idle, int plannedFill, const TIOStream::StepInfo& info, quint64 tick)
{
    work->inFlight = true;
    if (!info.seek) {
        work->stepTick = tick;
    }
    ++m_running;
    if (idle) {
        ++m_runningIdle;
    }
    TIOStream* stream = work->stream;
    QRunnable* step = QRunnable::create([this, work, stream, info, idle, plannedFill] {
        s_handOverFence.load(std::memory_order_acquire);
        const trav_time_t start = TTimeRef::get_nanoseconds_since_epoch();
        // The fill to show: after a seek what the seek filled, otherwise
        // what the stream had when the planner looked at it (its low point,
        // before this step refilled or flushed it); none out of sync
        const int fill = stream->step(info);
        Report done;
        done.stream = work;
        done.info = info;
        done.fill = fill < 0 ? -1 : (info.seek ? fill : plannedFill);
        done.idle = idle;
        done.nanoseconds = TTimeRef::get_nanoseconds_since_epoch() - start;
        report(done);
    });
    // After the step is made, before it goes to the pool
    s_handOverFence.fetch_add(1, std::memory_order_release);
    m_pool.start(step);
}

void TIOScheduler::start_task_step(TaskWork* task)
{
    task->inFlight = true;
    task->weight = task->task->weight();
    ++m_running;
    ++m_runningIdle;
    TIOIdleTask* idleTask = task->task.get();
    QRunnable* step = QRunnable::create([this, task, idleTask] {
        s_handOverFence.load(std::memory_order_acquire);
        const trav_time_t start = TTimeRef::get_nanoseconds_since_epoch();
        Report done;
        done.task = task;
        done.over = !idleTask->step();
        done.idle = true;
        done.nanoseconds = TTimeRef::get_nanoseconds_since_epoch() - start;
        report(done);
    });
    s_handOverFence.fetch_add(1, std::memory_order_release);
    m_pool.start(step);
}

int TIOScheduler::idle_limit_for(const QString& directory)
{
#if defined(Q_OS_LINUX)
    // The block device of the directory's file system (of the nearest
    // existing directory above it: a new project's directories may not be
    // there yet); a partition has no queue of its own, its disk is the
    // directory above it
    QString existing = QFileInfo(directory).absoluteFilePath();
    while (!QFileInfo::exists(existing)) {
        const QString parent = QFileInfo(existing).absolutePath();
        if (parent == existing) {
            break;
        }
        existing = parent;
    }
    struct stat info;
    if (stat(QFile::encodeName(existing).constData(), &info) == 0) {
        const QString device = QStringLiteral("/sys/dev/block/%1:%2").arg(major(info.st_dev)).arg(minor(info.st_dev));
        for (const QString& queue : {device + QStringLiteral("/queue/rotational"), device + QStringLiteral("/../queue/rotational")}) {
            QFile file(queue);
            if (file.open(QIODevice::ReadOnly)) {
                const QByteArray rotational = file.readAll().trimmed();
                if (rotational == "0") {
                    return QThread::idealThreadCount();
                }
                if (rotational == "1") {
                    return 1;
                }
            }
        }
    }
    // Unknown (a file system without a block device of its own, like a
    // btrfs subvolume or a network share): as if it seeks
    return 1;
#else
    // Windows (seek penalty, IOCTL_STORAGE_QUERY_PROPERTY) and macOS (IOKit
    // "Medium Type") could tell; not done yet, so as if it seeks
    Q_UNUSED(directory);
    return 1;
#endif
}

void TIOScheduler::add_idle_task(std::unique_ptr<TIOIdleTask> task)
{
    {
        std::lock_guard locker(m_requestsMutex);
        m_tasksToAdd.push_back(std::move(task));
    }
    // Also without a driver: the task does not wait for a tick
    m_wake.signal();
}

void TIOScheduler::add_group(TIOGroup* group)
{
    Q_ASSERT(group);
    {
        std::lock_guard locker(m_requestsMutex);
        m_groupsToAdd.push_back(group);
    }
    m_wake.signal();
}

void TIOScheduler::remove_group(TIOGroup* group)
{
    Removal removal{nullptr, group};
    std::unique_lock locker(m_requestsMutex);
    m_removals.push_back(&removal);
    if (!m_thread) {
        // No scheduler thread: this thread is the only one
        locker.unlock();
        take_requests();
        return;
    }
    m_wake.signal();
    m_removalDone.wait(locker, [&removal] { return removal.done; });
}

void TIOScheduler::add_stream(TIOStream* stream, TIOGroup* group)
{
    PENTER2;

    Q_ASSERT(stream);
    if (stream->get_channel_count() == 0) {
        PMESG("TIOScheduler::add_stream: the stream has no channels, not adding it");
        return;
    }

    const uint rate = get_output_rate();
    const nframes_t bufferSize = m_bufferSize.load(std::memory_order_relaxed);
    stream->set_output_rate_and_resampler(int(rate), get_resampler());
    stream->prepare_ring_buffers(bufferSize);

    {
        std::lock_guard locker(m_requestsMutex);
        m_streamsToAdd.push_back(StreamWork{stream, group, rate, bufferSize});
    }
    m_wake.signal();
}

void TIOScheduler::remove_stream(TIOStream* stream)
{
    PENTER2;

    Removal removal{stream};
    std::unique_lock locker(m_requestsMutex);
    m_removals.push_back(&removal);
    if (!m_thread) {
        // No scheduler thread: this thread is the only one (and no step runs
        // between two dispatch() calls)
        locker.unlock();
        take_requests();
        return;
    }
    // Until the scheduler thread let go of it: at once, or once its step
    // that runs is done; no step of it runs after that
    m_wake.signal();
    m_removalDone.wait(locker, [&removal] { return removal.done; });
}

/**
 * The time the steps took since the last call, in percent of the time that
 * passed (percent of one core: above 100 with more than one thread busy).
 * The time a step waits for the disk counts too.
 */
bool TIOScheduler::get_step_load(float &time)
{
    trav_time_t currentTime = TTimeRef::get_nanoseconds_since_epoch();

    // exchange: what the scheduler thread adds meanwhile counts next time
    time = (m_stepTime.exchange(0) / float(currentTime - m_lastStepLoadTime)) * 100;

    m_lastStepLoadTime = currentTime;

    return true;
}


/**
 * The fill of the emptiest stream in sync since the last call, in percent
 * (a writer's fill is its free part), 100 when no step reported meanwhile.
 */
int TIOScheduler::lowest_buffer_fill( )
{
    return m_lowestBufferFill.exchange(100);
}

void TIOScheduler::set_resampler(const TIOStream::Resampler& resampler)
{
    m_resampler.store(resampler);
    m_resamplerChanged.store(true);
}

void TIOScheduler::set_output_sample_rate(uint outputSampleRate)
{
    TLOG_I("diskio", "output sample rate {}", outputSampleRate);
    m_outputRate.store(outputSampleRate);
}
