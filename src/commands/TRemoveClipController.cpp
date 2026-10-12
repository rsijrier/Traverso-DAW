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

int TRemoveClipController::prepare_actions()
{
    PENTER;
    Q_ASSERT(m_sheet);

    TAudioClipManager* clipManager = m_sheet->get_audioclip_manager();
    Q_ASSERT(clipManager);

    m_clipsToRemove.clear();

    if (m_targetClip->is_selected()) {
        clipManager->get_selected_clips(m_clipsToRemove);
    } else {
        m_clipsToRemove.append(m_targetClip);
    }

    if (m_clipsToRemove.isEmpty()) {
        return -1;
    }

    return 1;
}

QUndoCommand* TRemoveClipController::get_undo_command()
{
    Q_ASSERT(m_sheet);

    return m_sheet->create_command_mutate_clips(
        {},
        m_clipsToRemove,
        m_targetClip->is_selected() ? tr("Remove Selected Clips") : tr("Remove Clip")
        );
}
