/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommandController.h"
#include <QList>
#include <functional>
#include <memory>

class TTimeLineEntity;
class TSelectionService;

/**
 * @brief Removes a timeline entity, or the selection it is part of.
 *
 * If the target is selected, all selected entities of the same type are
 * removed, otherwise only the target. The type specific part, creating the
 * undo command that removes the entities from their owner, is passed in.
 */
class TRemoveEntitiesController final : public TCommandController
{
    Q_OBJECT

public:
    // removingSelection is true when the entities are the selection, e.g. for the undo text
    using CreateRemoveCommand = std::function<std::unique_ptr<QUndoCommand>(const QList<TTimeLineEntity*>& entities,
                                                                            bool removingSelection)>;

    TRemoveEntitiesController(TTimeLineEntity* target, TSelectionService* selection, CreateRemoveCommand createRemoveCommand);
    ~TRemoveEntitiesController() override = default;

    TCommitResult commit() override;

private:
    TTimeLineEntity*    m_target{nullptr};
    TSelectionService*  m_selection{nullptr};
    CreateRemoveCommand m_createRemoveCommand;
};
