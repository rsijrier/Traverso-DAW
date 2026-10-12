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
#include "TAudioDevice.h"
#include "TAudioDeviceSetup.h"
#include <QMessageBox>
#include <QCoreApplication>

/**
 * @brief Continuous background execution loop for processing thread-safe events.
 *        Monitors the active state lifecycle flag to gracefully break execution.
 */
void TAudioThreadMessageQueueThread::run()
{
    while (tsmp().m_running) {
        tsmp().process_processed_events_by_rt_thread_queue();
    }
}

/**
 * @brief Thread-safe Meyer's Singleton accessor for global queue coordination.
 */
TAudioThreadMessageQueue& tsmp()
{
    static TAudioThreadMessageQueue ThreadSaveAddRemove;
    return ThreadSaveAddRemove;
}

/**
 * @brief Constructor managing lock-free circular buffer initialization
 *        and critical cross-thread error signaling pathways.
 */
TAudioThreadMessageQueue::TAudioThreadMessageQueue()
{
    m_running = true;
    m_eventCounter = 0;
    m_retryCount = 0;
    m_droppedRtEvents = 0; // Initialize the atomic overflow tracker

    // Allocate fixed-capacity lock-free queues via automatic memory wrappers.
    // These allocations occur strictly during initialization, ensuring zero heap activity at runtime.
    m_postedFromGuiThreadQueue = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>>(1024);
    m_postedFromRTThreadQueue = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>>(4096);
    m_processedByRTThreadQueue = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>>(5120);

    // Bind thread-safety notifications to GUI slots using standard QueuedConnections
    connect(this, &TAudioThreadMessageQueue::audioDriverStalled, this, []() {
        if (audiodevice().get_driver_type() != "Dummy") {
            QMessageBox::critical(nullptr, tr("Traverso - Malfunction!"),
                                  tr("The Audiodriver Thread seems to be stalled/stopped..."), QMessageBox::Ok);
            TAudioDeviceSetup audioDeviceSetup;
            audioDeviceSetup.set_driver_type("Dummy");
            audiodevice().set_parameters(audioDeviceSetup);
        }
    }, Qt::QueuedConnection);

    connect(this, &TAudioThreadMessageQueue::audioDriverFatal, this, []() {
        QMessageBox::critical(nullptr, tr("Traverso - Fatal!"),
                              tr("The Null AudioDriver stalled too, exiting application!"), QMessageBox::Ok);
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    // Asynchronously notify the system when the lock-free queues encounter pressure
    connect(this, &TAudioThreadMessageQueue::queueOverflowDetected, this, []() {
        qCritical("CRITICAL: Real-time message queue overflowed! Events dropped to preserve audio stream integrity.");
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
    printf("TAudioThreadMessageQueue dropped real time events: %d\n", m_droppedRtEvents.load());

    m_running = false;

    // Dispatch a hollow dummy payload to immediately break the blocking wait_dequeue state
    TAudioThreadMessageQueueEvent dummyEvent{};
    m_postedFromRTThreadQueue->try_enqueue(std::move(dummyEvent));

    // Wait until the thread breaks its loop execution flow and release handles safely
    m_TAudioThreadMessageQueueThread->quit();
    m_TAudioThreadMessageQueueThread->wait();
    delete m_TAudioThreadMessageQueueThread;
}

/**
 * @brief Enqueues a structured transaction payload originating from the primary GUI thread context.
 *        Safe to block since it executes inside the non-real-time user interface context.
 */
void TAudioThreadMessageQueue::post_gui_event(TAudioThreadMessageQueueEvent &&event)
{
    Q_ASSERT_X(this->thread() == QThread::currentThread(), "TSMP::post_gui_event", "Adding event from other than GUI thread!!");

    if (!m_postedFromGuiThreadQueue->try_enqueue(std::move(event))) {
        m_postedFromGuiThreadQueue->wait_enqueue(std::move(event));
    }
    m_eventCounter++;
}

/**
 * @brief Enqueues feedback indicators or execution callbacks originating from the RT audio context.
 *        Guarantees strict non-blocking behavior to prevent priority inversion glitches.
 */
void TAudioThreadMessageQueue::post_to_gui_thread(TAudioThreadMessageQueueEvent &&event)
{
    Q_ASSERT_X(this->thread() != QThread::currentThread(), "TSMP::post_rt_event", "Adding event from NON-RT Thread!!");

    // Real-time Safety: Never fall back to wait_enqueue. If the queue is entirely full,
    // drop the execution signal to maintain audio integrity and record the failure via an atomic counter.
    if (!m_postedFromRTThreadQueue->try_enqueue(std::move(event))) {
        m_droppedRtEvents.fetch_add(1, std::memory_order_relaxed);
    }
}

/**
 * @brief Flushes tasks dispatched by the GUI layer. Executed entirely inside the RT processing context.
 *        Guarantees immediate zero-heap cleanup of task structures right after execution.
 */
void TAudioThreadMessageQueue::process_posted_gui_events()
{
    TAudioThreadMessageQueueEvent event;

    while (m_postedFromGuiThreadQueue->try_dequeue(event)) {
        if (event.rtTask) {
            event.rtTask();
            // Real-time Safety: Erase closures immediately to clean up stack space inside the cycle loop
            event.rtTask = nullptr;
        }

        if (event.guiCallback) {
            // Real-time Safety: Utilize strict non-blocking try-semantics. Dropping an overflowing
            // callback event prevents kernel locks from disrupting the audio streaming deadline.
            if (!m_processedByRTThreadQueue->try_enqueue(std::move(event))) {
                m_droppedRtEvents.fetch_add(1, std::memory_order_relaxed);
                --m_eventCounter; // Compensate transaction counter to prevent structural watch-dog deadlocks
            }
        } else {
            --m_eventCounter;
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
    if (m_droppedRtEvents.load(std::memory_order_relaxed) > 0) {
        m_droppedRtEvents.store(0, std::memory_order_relaxed);
        emit queueOverflowDetected();
    }

    // Execute callback patterns strictly when operating active run states
    if (m_running && event.guiCallback) {
        event.guiCallback();
    }

    --m_eventCounter;
    m_retryCount++;

    // Integrated engine health checking watchdog mechanism
    if (m_retryCount > 200) {
        m_retryCount = 0;
        if (audiodevice().get_driver_type() != "Dummy") {
            emit audioDriverStalled();
        } else {
            emit audioDriverFatal();
        }
    }

    if (m_eventCounter <= 0) {
        m_retryCount = 0;
    }
}
