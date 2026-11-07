/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TRemoveEntitiesController.h"
#include "TContextItem.h"
#include "TSelectionService.h"
#include "TTimeLineEntity.h"
#include "Debugger.h"
#include <QUndoCommand>

TRemoveEntitiesController::TRemoveEntitiesController(TTimeLineEntity* target,
                                                     TSelectionService* selection,
                                                     CreateRemoveCommand createRemoveCommand)
    : TCommandController(dynamic_cast<TContextItem*>(target))
    , m_target(target)
    , m_selection(selection)
    , m_createRemoveCommand(std::move(createRemoveCommand))
{
    Q_ASSERT(m_target);
    Q_ASSERT(m_createRemoveCommand);
}

TCommitResult TRemoveEntitiesController::commit()
{
    PENTER;

    const bool removingSelection = m_selection && m_selection->is_selected(m_target);

    QList<TTimeLineEntity*> entities;

    if (removingSelection) {
        entities = m_selection->get_selected_entities();
    } else {
        entities.append(m_target);
    }

    if (entities.isEmpty()) {
        return TCommitResult::nothing_to_do();
    }

    return TCommitResult::committed(m_createRemoveCommand(entities, removingSelection));
}
