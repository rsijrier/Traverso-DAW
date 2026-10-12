/*
Copyright (C) 2007 Ben Levitt

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

$Id: TLocation.cpp,v 1.2 2008/02/21 20:00:48 r_sijrier Exp $
*/

#include "TLocation.h"
#include <Debugger.h>

TLocation::TLocation(QObject *parent)
    : QObject(parent)
{
}

void TLocation::set_snappable(bool snap)
{
    if (m_isSnappable == snap) return;
    m_isSnappable = snap;
    emit locationChanged();
}

void TLocation::set_location(const TTimeRef &start, const TTimeRef &end)
{
    if (m_start == start && m_end == end) return;
    m_start = start;
    m_end = end;
    emit locationChanged();
}

void TLocation::set_start(const TTimeRef &start)
{
    if (m_start == start) return;
    m_start = start;
    emit locationChanged();
}

void TLocation::set_end(const TTimeRef &end)
{
    if (m_end == end) return;
    m_end = end;
    emit locationChanged();
}

void TLocation::set_moving(bool moving)
{
    if (m_isMoving == moving) return;
    m_isMoving = moving;
    emit locationChanged();
}

/* EOF */
