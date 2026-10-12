/*
Copyright (C) 2024 Remon Sijrier

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

#include "TTimeRef.h"
#include <QObject>

enum SnapType {
    SnapAudioClips = 0x01,
    SnapMarkers    = 0x02,
    SnapWorkCursor = 0x04
};

class TLocation : public QObject
{
    Q_OBJECT

public:
    explicit TLocation(QObject* parent = nullptr);
    virtual ~TLocation() = default;

    void set_snappable(bool snap);
    bool is_snappable() const { return m_isSnappable; }
    bool is_moving() const { return m_isMoving; }

    inline TTimeRef get_start() const { return m_start; }
    inline TTimeRef get_end() const { return m_end; }
    TTimeRef get_length() const { return m_end - m_start; }

    void set_location(const TTimeRef& start, const TTimeRef& end = TTimeRef());
    void set_start(const TTimeRef& start);
    void set_end(const TTimeRef& end);
    void set_moving(bool moving);

private:
    TTimeRef m_start;
    TTimeRef m_end;
    bool     m_isSnappable{true};
    bool     m_isMoving{false};

signals:
    void locationChanged();
};
