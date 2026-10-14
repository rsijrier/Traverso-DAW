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

    auto* targetItem = dynamic_cast<TContextItem*>(m_target);
    const bool removingSelection = m_selection && targetItem && m_selection->is_selected(targetItem->get_id());

    QList<TTimeLineEntity*> entities;

    if (removingSelection) {
        // Only entities of the target's type, the selection may contain others
        for (TTimeLineEntity* entity : m_selection->get_selected_entities()) {
            if (entity->get_timeline_entity_type() == m_target->get_timeline_entity_type()) {
                entities.append(entity);
            }
        }
    } else {
        entities.append(m_target);
    }

    if (entities.isEmpty()) {
        return TCommitResult::nothing_to_do();
    }

    return TCommitResult::committed(m_createRemoveCommand(entities, removingSelection));
}
