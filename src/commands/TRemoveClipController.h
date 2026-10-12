/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommandController.h"
#include "TInstantInteraction.h"
#include "TTransactional.h"
#include <QList>

class TAudioClip;
class TSheet;

class TRemoveClipController final : public TCommandController, public TInstantInteraction, public TTransactional
{
    Q_OBJECT
    Q_INTERFACES(TInstantInteraction TTransactional)

public:
    explicit TRemoveClipController(TAudioClip* targetClip);
    ~TRemoveClipController() override = default;

    int prepare_actions() override;
    QUndoCommand* get_undo_command() override;

private:
    TAudioClip*        m_targetClip{nullptr};
    TSheet*            m_sheet{nullptr};
    QList<TAudioClip*> m_clipsToRemove;
};
