/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TCommandController.h"
#include "TContextItem.h"
#include <QUndoStack>

QUndoStack* TCommandController::get_context_item_undo_stack() const
{
    return m_contextItem ? m_contextItem->resolve_undo_stack() : nullptr;
}
