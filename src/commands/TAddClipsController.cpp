/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TAddClipsController.h"
#include "TSheet.h"
#include "Debugger.h"
#include <QUndoCommand>

TAddClipsController::TAddClipsController(TContextItem* contextItem, TSheet* sheet, CreateClips createClips, QString description)
    : TCommandController(contextItem)
    , m_sheet(sheet)
    , m_createClips(std::move(createClips))
    , m_description(std::move(description))
{
    Q_ASSERT(m_sheet);
    Q_ASSERT(m_createClips);
}

TCommitResult TAddClipsController::commit()
{
    PENTER;

    QString error;
    QList<TAudioClip*> clips = m_createClips(&error);

    if (clips.isEmpty()) {
        return error.isEmpty() ? TCommitResult::nothing_to_do() : TCommitResult::failed(error);
    }

    return TCommitResult::committed(std::unique_ptr<QUndoCommand>(
        m_sheet->create_command_mutate_clips(clips, {}, m_description)));
}
