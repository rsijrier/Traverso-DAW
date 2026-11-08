// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// A workspace: a child session of a song or of the project that shows some
// of its parent's tracks

#include "TSession.h"

class TWorkspace : public TSession
{
    Q_OBJECT

public:
    explicit TWorkspace(TSession* parentSession);

    // Given by TObjectFactory
    using IdTag = WorkspaceTag;
    WorkspaceId object_id() const
    {
        Q_ASSERT(m_objectId.is_valid());
        return m_objectId;
    }

    QDomNode get_state(QDomDocument doc);
    int set_state(const QDomNode& node);

private:
    friend class TObjectFactory;
    WorkspaceId m_objectId;
};
