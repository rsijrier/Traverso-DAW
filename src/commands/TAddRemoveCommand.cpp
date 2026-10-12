/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TAddRemoveCommand.h"

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
    , m_doMethod(std::move(doMethod))
    , m_undoMethod(std::move(undoMethod))
    , m_doSignal(std::move(doSignal))
    , m_undoSignal(std::move(undoSignal))
{
    Q_UNUSED(arg);
    Q_UNUSED(sheet);
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
    , m_doMethod(std::move(doMethod))
    , m_undoMethod(std::move(undoMethod))
    , m_doSignal(std::move(doSignal))
    , m_undoSignal(std::move(undoSignal))
{
    Q_UNUSED(sheet);
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
    if (method) method();
    if (signal) signal();
    return 1;
}
