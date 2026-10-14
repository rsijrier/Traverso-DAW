/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommandController.h"
#include <QList>

class TAudioClip;
class TSheet;

class TRemoveClipController final : public TCommandController
{
    Q_OBJECT

public:
    explicit TRemoveClipController(TAudioClip* targetClip);
    ~TRemoveClipController() override = default;

    TCommitResult commit() override;

private:
    TAudioClip*        m_targetClip{nullptr};
    TSheet*            m_sheet{nullptr};
};
