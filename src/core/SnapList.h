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

#ifndef SNAPLIST_H
#define SNAPLIST_H

#include <QList>

#include "TTimeRef.h"

class TSession;

class SnapList
{

public:
    SnapList(TSession* sheet);
    ~SnapList() {}

    TTimeRef get_snap_value(const TTimeRef& location);
    TTimeRef get_snap_value(const TTimeRef& location, bool& didSnap);
    qint64 get_snap_diff(const TTimeRef& location);
    TTimeRef next_snap_pos(const TTimeRef& location);
    TTimeRef prev_snap_pos(const TTimeRef& location);

    TTimeRef calculate_snap_diff(TTimeRef leftlocation, TTimeRef rightlocation);

    void set_range(const TTimeRef& start, const TTimeRef& end, qint64 scalefactor);
    void mark_dirty();
    bool was_dirty();

private:
    TSession*	m_sheet;
    QList<TTimeRef> 	m_xposList;
    QList<TTimeRef> 	m_xposLut;
    QList<bool> 	m_xposBool;
    bool		m_isDirty;
    bool		m_wasDirty{};
    TTimeRef		m_rangeStart;
    TTimeRef		m_rangeEnd;
    qint64		m_scalefactor;

    void update_snaplist();
    bool is_snap_value(const TTimeRef& location);
};

#endif

/* EOF */
