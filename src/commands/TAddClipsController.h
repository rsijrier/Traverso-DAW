/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommandController.h"
#include <QList>
#include <QString>
#include <functional>

class TAudioClip;
class TSheet;

/**
 * @brief Adds new audio clips as one undo step.
 *
 * The clips are created by the function passed in, e.g. from a file the user
 * picks. It returns the created clips, or no clips and an error message when
 * creating them failed, or no clips and no message when there is nothing to
 * add, e.g. because the user cancelled a dialog.
 */
class TAddClipsController final : public TCommandController
{
    Q_OBJECT

public:
    using CreateClips = std::function<QList<TAudioClip*>(QString* error)>;

    TAddClipsController(TContextItem* contextItem, TSheet* sheet, CreateClips createClips, QString description);
    ~TAddClipsController() override = default;

    TCommitResult commit() override;

private:
    TSheet*     m_sheet{nullptr};
    CreateClips m_createClips;
    QString     m_description;
};
