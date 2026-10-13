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

// Runs redo and undo in a TRtTransaction, so all real-time changes of one
// undo step reach the audio thread as a single event.
class TUndoCommand : public QUndoCommand
{
public:
    explicit TUndoCommand(const QString& description,
                          const std::function<void()>& redoAction,
                          const std::function<void()>& undoAction)
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
        m_redoAction();
    }

    void undo() final
    {
        TRtTransaction transaction;
        m_undoAction();
    }

private:
    std::function<void()> m_redoAction;
    std::function<void()> m_undoAction;
};
