/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include <QUndoCommand>
#include <functional>
#include <QString>
#include <QtGlobal>

#include "TRtTransaction.h"

// Runs redo and undo in a TRtTransaction, which the actions get to change
// real-time state with, so all changes of one undo step reach the audio
// thread as a single event.
class TUndoCommand : public QUndoCommand
{
public:
    using Action = std::function<void(TRtTransaction& transaction)>;

    explicit TUndoCommand(const QString& description,
                          const Action& redoAction,
                          const Action& undoAction)
        : QUndoCommand(description)
        , m_redoAction(redoAction)
        , m_undoAction(undoAction)
    {
        Q_ASSERT(m_redoAction != nullptr);
        Q_ASSERT(m_undoAction != nullptr);
    }

    ~TUndoCommand() override = default;

    TUndoCommand(const TUndoCommand&) = delete;
    TUndoCommand& operator=(const TUndoCommand&) = delete;
    TUndoCommand(TUndoCommand&&) = delete;
    TUndoCommand& operator=(TUndoCommand&&) = delete;

    void redo() final
    {
        TRtTransaction transaction;
        m_redoAction(transaction);
    }

    void undo() final
    {
        TRtTransaction transaction;
        m_undoAction(transaction);
    }

private:
    Action m_redoAction;
    Action m_undoAction;
};
