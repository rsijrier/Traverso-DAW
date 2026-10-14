/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TRemoveClipController.h"
#include "TAudioClip.h"
#include "TAudioClipManager.h"
#include "TSheet.h"
#include "Debugger.h"
#include <QUndoCommand>

TRemoveClipController::TRemoveClipController(TAudioClip* targetClip)
    : TCommandController(targetClip)
    , m_targetClip(targetClip)
{
    m_sheet = m_targetClip->get_sheet();
    Q_ASSERT(m_sheet);
}

TCommitResult TRemoveClipController::commit()
{
    PENTER;
    Q_ASSERT(m_sheet);

    TAudioClipManager* clipManager = m_sheet->get_audioclip_manager();
    Q_ASSERT(clipManager);

    QList<TAudioClip*> clipsToRemove;

    if (m_targetClip->is_selected()) {
        clipManager->get_selected_clips(clipsToRemove);
    } else {
        clipsToRemove.append(m_targetClip);
    }

    if (clipsToRemove.isEmpty()) {
        return TCommitResult::nothing_to_do();
    }

    return TCommitResult::committed(std::unique_ptr<QUndoCommand>(m_sheet->create_command_mutate_clips(
        {},
        clipsToRemove,
        m_targetClip->is_selected() ? tr("Remove Selected Clips") : tr("Remove Clip"))));
}
