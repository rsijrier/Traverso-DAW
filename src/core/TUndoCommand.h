/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include <QUndoCommand>
#include <functional>
#include <QString>
#include <QtGlobal>

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
        m_redoAction();
    }

    void undo() final
    {
        m_undoAction();
    }

private:
    std::function<void()> m_redoAction;
    std::function<void()> m_undoAction;
};
