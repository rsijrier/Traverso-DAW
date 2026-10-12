/*
Copyright (C) 2006-2008 Nicola Doebelin, Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#pragma once

#include <QList>
#include <QMultiHash>
#include <QFlags>
#include <QPointer>
#include "TTimeRef.h"

class TSession;

struct TRichSnapResult {
    TTimeRef        snappedLocation;
    bool            didSnap{false};
    QList<QObject*> snappedToTargets;
};

class TSnapList
{
public:
    enum SnapType {
        NoSnap      = 0x00,
        AudioClips  = 0x01,
        Markers     = 0x02,
        WorkCursor  = 0x04,
        SnapAll     = 0xFF
    };
    Q_DECLARE_FLAGS(SnapFilters, SnapType)

    explicit TSnapList(TSession* session);
    ~TSnapList() = default;

    TRichSnapResult get_snap_context(const TTimeRef& mouseLocation, qint64 scaleFactor, SnapFilters activeFilters);

    TTimeRef get_snap_value(const TTimeRef& location);
    TTimeRef next_snap_pos(const TTimeRef& location);
    TTimeRef prev_snap_pos(const TTimeRef& location);

    void mark_dirty();
    bool was_dirty();

    void set_range(const TTimeRef& start, const TTimeRef& end, qint64 scalefactor) {
        Q_UNUSED(start); Q_UNUSED(end); Q_UNUSED(scalefactor);
    }

private:
    TSession*                             m_session{nullptr};
    QList<TTimeRef>                       m_uniqueSnapPositions;
    QMultiHash<qint64, QPointer<QObject>> m_locationRegistry;
    bool                                  m_isDirty{true};
    bool                                  m_wasDirty{false};

    void update_snaplist_algorithm();
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TSnapList::SnapFilters)
