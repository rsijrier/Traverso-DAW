/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include <QObject>

class QUndoStack;
class TContextItem;

class TCommandController : public QObject
{
    Q_OBJECT

public:
    explicit TCommandController(TContextItem* item)
        : QObject(nullptr)
        , m_contextItem(item) {}

    ~TCommandController() override = default;

    inline bool is_valid() const { return m_isValid; }
    inline void set_valid(bool valid) { m_isValid = valid; }

    QUndoStack* get_context_item_undo_stack() const;

protected:
    TContextItem* m_contextItem{nullptr};
    bool          m_isValid{false};
};
