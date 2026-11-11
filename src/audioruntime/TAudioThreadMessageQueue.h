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

#pragma once

#include <QObject>
#include <QThread>
#include <cstdint>
#include <memory>
#include <type_traits>
#include "cameron/readerwritercircularbuffer.h"
#include "TRealTime.h"
#include "TRealTimeClosure.h"

/**
 * @brief An event passed between the GUI thread, the audio thread and the
 *        message queue worker thread (TAudioThreadMessageQueueThread).
 *
 * rtTask runs in the audio thread. guiCallback, despite its name, does NOT run
 * in the GUI thread: it runs in the message queue worker thread, after the
 * audio thread processed rtTask (or directly, for events posted by the audio
 * thread via post_to_gui_thread()).
 *
 * guiCallback may therefore only:
 * - free plain memory that nobody else references any more (this keeps freeing
 *   out of both the audio thread and the GUI thread)
 * - emit signals; Qt delivers them queued to receivers living in the GUI thread
 *
 * It must not call GUI or model functions directly, and must not delete
 * QObjects that live in the GUI thread. Such objects have to be deleted in the
 * GUI thread, e.g. by calling deleteLater() on them.
 */
struct TAudioThreadMessageQueueEvent {
    TRealTimeClosure rtTask;
    TGuiClosure      guiCallback;
};

class TAudioThreadMessageQueueThread : public QThread
{
protected:
    void run() override;
};

class TAudioThreadMessageQueue : public QObject
{
    Q_OBJECT

public:
    /**
     * rtTask runs in the audio thread: a TRealTimeClosure, whose type checks
     * at compile time that it is real-time safe. guiSignal becomes a
     * TGuiClosure (it passes through the audio thread, which may drop it) and
     * runs afterwards in the message queue worker thread, not in the GUI
     * thread. See
     * TAudioThreadMessageQueueEvent for what guiSignal may and may not do.
     */
    template<typename RTTask, typename GuiSignal = std::nullptr_t>
    void post_rt_task(RTTask&& rtTask, GuiSignal&& guiSignal = nullptr)
    {
        TAudioThreadMessageQueueEvent event{};
        event.rtTask = TRealTimeClosure(std::forward<RTTask>(rtTask));
        event.guiCallback = TGuiClosure(std::forward<GuiSignal>(guiSignal));
        post_gui_event(std::move(event));
    }

    void post_gui_event(TAudioThreadMessageQueueEvent &&event);
    // Called from the audio thread. Despite the name, event.guiCallback runs in the
    // message queue worker thread, see TAudioThreadMessageQueueEvent.
    void post_to_gui_thread(TAudioThreadMessageQueueEvent &&event) T_REALTIME_SAFE;

private:
    TAudioThreadMessageQueue();
    ~TAudioThreadMessageQueue() override;
    TAudioThreadMessageQueue(const TAudioThreadMessageQueue&) = delete;

    friend TAudioThreadMessageQueue& tsmp() T_REALTIME_SAFE;
    // The one queue, made by the first call (tsmp())
    static TAudioThreadMessageQueue& make_instance();

    friend class TAudioDevice;
    friend class TAudioThreadMessageQueueThread;

    void process_posted_gui_events() T_REALTIME_SAFE;
    void process_processed_events_by_rt_thread_queue();

    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>> m_postedFromGuiThreadQueue;
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>> m_postedFromRTThreadQueue;
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>> m_processedByRTThreadQueue;

    TAudioThreadMessageQueueThread* m_TAudioThreadMessageQueueThread;
    // Dropped since the worker last reported them (the audio thread counts)
    std::atomic<int>  m_droppedRtEvents{0};
    // Dropped in this session, reported or not: the worker adds what it
    // reports, the destructor reads it after the worker ended
    std::atomic<int64_t> m_droppedRtEventsTotal{0};
    std::atomic<bool> m_running{true};

public slots:
    void trigger_batch_start() { emit batchTransactionStarted(); }
    void trigger_batch_finish() { emit batchTransactionFinished(); }

signals:
    void batchTransactionStarted();
    void batchTransactionFinished();
    // From the worker thread: count events were dropped since the last one
    void queueOverflowDetected(int count);
};

TAudioThreadMessageQueue& tsmp() T_REALTIME_SAFE;
