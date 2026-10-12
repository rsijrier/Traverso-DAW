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
#include <memory>
#include <functional>
#include <type_traits>
#include "cameron/readerwritercircularbuffer.h"

struct TAudioThreadMessageQueueEvent {
    std::move_only_function<void()> rtTask{nullptr};
    std::move_only_function<void()> guiCallback{nullptr};
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
     * @brief Pure C++23 Task Dispatcher.
     *        Statically vefifies that lambda captures fit within real-time limits (64 bytes)
     *        and are trivially copyable to prevent heap allocation inside the audio thread.
     */
    template<typename RTTask, typename GuiSignal = std::nullptr_t>
    void post_rt_task(RTTask&& rtTask, GuiSignal&& guiSignal = nullptr)
    {
        using DecayedRTTask = std::decay_t<RTTask>;
        using DecayedGuiSignal = std::decay_t<GuiSignal>;
        using TargetType = std::move_only_function<void()>;

        // 1. Real-Time Security Assertions for the Audio Thread
        if constexpr (!std::is_same_v<DecayedRTTask, TargetType>) {
            static_assert(sizeof(DecayedRTTask) <= 64,
                          "CRITICAL C++23 ERROR: Lambda closure exceeds the 64-byte real-time stack limit!");
            static_assert(std::is_trivially_copyable_v<DecayedRTTask>,
                          "CRITICAL C++23 ERROR: Lambda contains non-trivial captures which violates lock-free safety!");
        }

        // 2. Security Assertions for the GUI Return Signal
        if constexpr (!std::is_same_v<DecayedGuiSignal, TargetType> && !std::is_same_v<DecayedGuiSignal, std::nullptr_t>) {
            static_assert(sizeof(DecayedGuiSignal) <= 64,
                          "CRITICAL C++23 ERROR: GUI Signal lambda exceeds 64 bytes!");
            static_assert(std::is_trivially_copyable_v<DecayedGuiSignal>,
                          "CRITICAL C++23 ERROR: GUI Signal lambda contains non-trivial captures!");
        }

        TAudioThreadMessageQueueEvent event{};
        event.rtTask = std::forward<RTTask>(rtTask);
        event.guiCallback = std::forward<GuiSignal>(guiSignal);
        post_gui_event(std::move(event));
    }

    void post_gui_event(TAudioThreadMessageQueueEvent &&event);
    void post_to_gui_thread(TAudioThreadMessageQueueEvent &&event);

private:
    TAudioThreadMessageQueue();
    ~TAudioThreadMessageQueue() override;
    TAudioThreadMessageQueue(const TAudioThreadMessageQueue&) = delete;

    friend TAudioThreadMessageQueue& tsmp();

    friend class TAudioDevice;
    friend class TAudioThreadMessageQueueThread;

    void process_posted_gui_events();
    void process_processed_events_by_rt_thread_queue();

    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>> m_postedFromGuiThreadQueue;
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>> m_postedFromRTThreadQueue;
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<TAudioThreadMessageQueueEvent>> m_processedByRTThreadQueue;

    TAudioThreadMessageQueueThread* m_TAudioThreadMessageQueueThread;
    int             m_eventCounter;
    int             m_retryCount;
    std::atomic<int>  m_droppedRtEvents{0};
    std::atomic<bool> m_running{true};

public slots:
    void trigger_batch_start() { emit batchTransactionStarted(); }
    void trigger_batch_finish() { emit batchTransactionFinished(); }

signals:
    void batchTransactionStarted();
    void batchTransactionFinished();
    void audioDriverStalled();
    void audioDriverFatal();
    void queueOverflowDetected();
};

TAudioThreadMessageQueue& tsmp();
