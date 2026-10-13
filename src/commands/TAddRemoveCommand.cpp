/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TAddRemoveCommand.h"
#include "TAudioThreadMessageQueue.h"
#include "TSession.h"

TAddRemoveCommand::TAddRemoveCommand(
    TContextItem* parent,
    bool historable,
    std::function<void()> doMethod,
    std::function<void()> undoMethod,
    const QString& des)
    : TCommand(parent, des)
    , m_doMethod(std::move(doMethod))
    , m_undoMethod(std::move(undoMethod))
    , m_doSignal(nullptr)
    , m_undoSignal(nullptr)
{
    if (!historable) {
        set_do_not_push_to_historystack();
    }
}

TAddRemoveCommand::TAddRemoveCommand(
    TContextItem* parent,
    void* arg,
    bool historable,
    TSession* sheet,
    std::function<void()> doMethod,   std::function<void()> doSignal,
    std::function<void()> undoMethod, std::function<void()> undoSignal,
    const QString& des)
    : TCommand(parent, des)
    , m_sheet(sheet)
    , m_doMethod(std::move(doMethod))
    , m_undoMethod(std::move(undoMethod))
    , m_doSignal(std::move(doSignal))
    , m_undoSignal(std::move(undoSignal))
{
    Q_UNUSED(arg);
    if (!historable) {
        set_do_not_push_to_historystack();
    }
}

TAddRemoveCommand::TAddRemoveCommand(
    TContextItem* parent,
    TContextItem* item,
    bool historable,
    TSession* sheet,
    std::function<void()> doMethod,   std::function<void()> doSignal,
    std::function<void()> undoMethod, std::function<void()> undoSignal,
    const QString& des)
    : TCommand(parent, des)
    , m_parentItem(item)
    , m_sheet(sheet)
    , m_doMethod(std::move(doMethod))
    , m_undoMethod(std::move(undoMethod))
    , m_doSignal(std::move(doSignal))
    , m_undoSignal(std::move(undoSignal))
{
    if (!historable) {
        set_do_not_push_to_historystack();
    }
}

int TAddRemoveCommand::do_action()
{
    if (m_doSignal || m_undoSignal) {
        return un_redo_action(m_doMethod, m_doSignal);
    }
    if (m_doMethod) {
        m_doMethod();
        return 1;
    }
    return -1;
}

int TAddRemoveCommand::undo_action()
{
    if (m_doSignal || m_undoSignal) {
        return un_redo_action(m_undoMethod, m_undoSignal);
    }
    if (m_undoMethod) {
        m_undoMethod();
        return 1;
    }
    return -1;
}

void TAddRemoveCommand::set_instantanious(bool instant)
{
    m_instantanious = instant;
}

int TAddRemoveCommand::un_redo_action(const std::function<void()>& method, const std::function<void()>& signal)
{
    // The methods change lists that the audio thread iterates. While the transport
    // is rolling they must run in the audio thread, unless the caller asked for
    // instant execution.
    bool executeSynchrone = m_instantanious;

    if (m_sheet && !m_sheet->is_transport_rolling()) {
        executeSynchrone = true;
    }

    if (executeSynchrone) {
        if (method) method();
        if (signal) signal();
        return 1;
    }

    // Non historable commands are deleted right after do_action(), possibly before
    // the audio thread runs the task, so the task gets its own copy of the methods.
    // The copy is deleted in the GUI thread, after the signal method ran.
    struct Job {
        std::function<void()> method;
        std::function<void()> signal;
    };
    auto* job = new Job{method, signal};

    tsmp().post_rt_task(
        [job]() { if (job->method) job->method(); },
        [job]() { if (job->signal) job->signal(); delete job; });

    return 1;
}
