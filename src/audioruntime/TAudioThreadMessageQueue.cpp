/*
Copyright (C) 2006-2026 Remon Sijrier

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

#include "TAudioThreadMessageQueue.h"
#include "Debugger.h"
#include "TRtTransaction.h"

/**
 * @brief Continuous background execution loop for processing thread-safe events.
 *        Monitors the active state lifecycle flag to gracefully break execution.
 */
void TAudioThreadMessageQueueThread::run()
{
    TLog::name_thread("mq");
    while (tsmp().m_running) {
        tsmp().process_processed_events_by_rt_thread_queue();
    }
}

/**
 * @brief Thread-safe Meyer's Singleton accessor for global queue coordination.
 */
// The queue, made by the first call
TAudioThreadMessageQueue& TAudioThreadMessageQueue::make_instance()
{
    static TAudioThreadMessageQueue ThreadSaveAddRemove;
    return ThreadSaveAddRemove;
}

TAudioThreadMessageQueue& tsmp() T_REALTIME_SAFE
{
    // The first call makes the queue, in the GUI thread at start-up (main()
    // in Traverso.cpp), before any audio thread runs; later calls only find
    // it made. Made once: a deliberate exception (realtime-audio-path.md §5)
    TAudioThreadMessageQueue* queue = nullptr;
    T_BLOCKING_BY_DESIGN(queue = &TAudioThreadMessageQueue::make_instance();)
    return *queue;
}

/**
 * @brief Constructor managing lock-free circular buffer initialization
 *        and critical cross-thread error signaling pathways.
 */
TAudioThreadMessageQueue::TAudioThreadMessageQueue()
{
    m_running = true;
    m_droppedRtEvents = 0; // Initialize the atomic overflow tracker

    // Allocate fixed-capacity lock-free queues via automatic memory wrappers.
    // These allocations occur strictly during initialization, ensuring zero heap activity at runtime.
    m_postedFromGuiThreadQueue = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>>(1024);
    m_postedFromRTThreadQueue = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>>(4096);
    m_processedByRTThreadQueue = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>>(5120);

    // Asynchronously notify the system when the lock-free queues encounter pressure
    connect(this, &TAudioThreadMessageQueue::queueOverflowDetected, this, [](int count) {
        TLOG_E("mq", "a queue from the audio thread was full: {} events dropped, their callbacks did not run "
                     "(what they would free stays allocated)", count);
    }, Qt::QueuedConnection);

    // Bootstrapping the companion background thread for GUI signaling tasks
    m_TAudioThreadMessageQueueThread = new TAudioThreadMessageQueueThread;
    m_TAudioThreadMessageQueueThread->start();
    m_TAudioThreadMessageQueueThread->moveToThread(m_TAudioThreadMessageQueueThread);
}

/**
 * @brief Destructor orchestrating deadlock-free lifecycle termination
 *        by awaking the blocking thread with a dummy payload.
 */
TAudioThreadMessageQueue::~TAudioThreadMessageQueue()
{
    m_running = false;

    // Dispatch a hollow dummy payload to immediately break the blocking wait_dequeue state
    TAudioThreadMessageQueueEvent dummyEvent{};
    m_postedFromRTThreadQueue->try_enqueue(std::move(dummyEvent));

    // Wait until the thread breaks its loop execution flow and release handles safely
    m_TAudioThreadMessageQueueThread->quit();
    m_TAudioThreadMessageQueueThread->wait();
    delete m_TAudioThreadMessageQueueThread;

    // The worker has ended: what it reported plus what it had not yet
    TLOG_I("mq", "dropped real time events in this session: {}",
           m_droppedRtEventsTotal.load() + m_droppedRtEvents.load());
}

/**
 * @brief Enqueues a structured transaction payload originating from the primary GUI thread context.
 *        Safe to block since it executes inside the non-real-time user interface context.
 */
void TAudioThreadMessageQueue::post_gui_event(TAudioThreadMessageQueueEvent &&event)
{
    Q_ASSERT_X(this->thread() == QThread::currentThread(), "TSMP::post_gui_event", "Adding event from other than GUI thread!!");

    if (!m_postedFromGuiThreadQueue->try_enqueue(std::move(event))) {
        // Producers should never wait: a full queue means too many events are
        // posted, which should be made visible and optimized
        PWARN(QString("TAudioThreadMessageQueue: the queue to the audio thread is full (%1 of %2 events), "
                      "the GUI thread waits for the audio thread")
                  .arg(m_postedFromGuiThreadQueue->size_approx())
                  .arg(m_postedFromGuiThreadQueue->max_capacity()).toLatin1().data());
        m_postedFromGuiThreadQueue->wait_enqueue(std::move(event));
    }
}

/**
 * @brief Enqueues feedback indicators or execution callbacks originating from the RT audio context.
 *        Guarantees strict non-blocking behavior to prevent priority inversion glitches.
 */
void TAudioThreadMessageQueue::post_to_gui_thread(TAudioThreadMessageQueueEvent &&event) T_REALTIME_SAFE
{
    // The queue lives in the GUI thread; not QThread::currentThread(), which
    // makes a QThread object in a thread Qt did not start
    Q_ASSERT_X(!TRtPublishable::is_gui_thread(), "TSMP::post_to_gui_thread", "Adding event from NON-RT Thread!!");

    // Real-time Safety: Never fall back to wait_enqueue. If the queue is entirely full,
    // drop the execution signal to maintain audio integrity and record the failure via an atomic counter.
    // Real-time safe by contract: the try_ call does not allocate or wait;
    // it moves the event (inline closures, no allocation) and wakes the
    // worker's semaphore when it sleeps
    bool enqueued = false;
    T_REALTIME_SAFE_BY_CONTRACT(enqueued = m_postedFromRTThreadQueue->try_enqueue(std::move(event));)
    if (!enqueued) {
        m_droppedRtEvents.fetch_add(1, std::memory_order_relaxed);
    }
}

/**
 * @brief Flushes tasks dispatched by the GUI layer. Executed entirely inside the RT processing context.
 *        Guarantees immediate zero-heap cleanup of task structures right after execution.
 */
void TAudioThreadMessageQueue::process_posted_gui_events() T_REALTIME_SAFE
{
    // Real-time safe by contract: the queues' try_ calls do not allocate or
    // wait, and moving or overwriting an event frees nothing: its rtTask is
    // trivially copyable, and so is its guiCallback, also when a full return
    // queue drops it here (TGuiClosure). The rtTask is real-time safe by its type
    // (TRealTimeClosure)
    T_REALTIME_SAFE_BY_CONTRACT(TAudioThreadMessageQueueEvent event;)

    while (true) {
        bool dequeued = false;
        T_REALTIME_SAFE_BY_CONTRACT(dequeued = m_postedFromGuiThreadQueue->try_dequeue(event);)
        if (!dequeued) {
            break;
        }
        if (event.rtTask) {
            event.rtTask();
        }

        if (event.guiCallback) {
            // Real-time Safety: Utilize strict non-blocking try-semantics. Dropping an overflowing
            // callback event prevents kernel locks from disrupting the audio streaming deadline.
            bool enqueued = false;
            T_REALTIME_SAFE_BY_CONTRACT(enqueued = m_processedByRTThreadQueue->try_enqueue(std::move(event));)
            if (!enqueued) {
                m_droppedRtEvents.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

/**
 * @brief Processes feedback and event loops inside the decoupled signaling background thread.
 *        Maintains optimal zero-CPU usage via blocking semantics, but respects immediate exit signals.
 */
void TAudioThreadMessageQueue::process_processed_events_by_rt_thread_queue()
{
    Q_ASSERT_X(m_TAudioThreadMessageQueueThread->thread() == QThread::currentThread(),
               "TSMP::process_processed_events_by_rt_thread_queue", "Runs in wrong thread");

    TAudioThreadMessageQueueEvent event;

    // 1. Process asynchronous processing signals finalized by the RT engine
    while (m_processedByRTThreadQueue->try_dequeue(event)) {
        if (event.guiCallback) {
            event.guiCallback();
        }
    }

    // 2. Clear out immediate state feedback metrics
    while (m_postedFromRTThreadQueue->try_dequeue(event)) {
        if (event.guiCallback) {
            event.guiCallback();
        }
    }

    // 3. Fall back to high-efficiency hardware sleep mode until awakened by another transaction cycle
    m_postedFromRTThreadQueue->wait_dequeue(event);

    // Asynchronously evaluate if the real-time loop registered any dropped transactions
    // Taken at once, so a drop between a read and a reset is not lost
    const int dropped = m_droppedRtEvents.exchange(0, std::memory_order_relaxed);
    if (dropped > 0) {
        m_droppedRtEventsTotal.fetch_add(dropped, std::memory_order_relaxed);
        emit queueOverflowDetected(dropped);
    }

    // Execute callback patterns strictly when operating active run states
    if (m_running && event.guiCallback) {
        event.guiCallback();
    }
}
