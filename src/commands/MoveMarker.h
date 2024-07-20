/*
    Copyright (C) 2010 Remon Sijrier

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

#ifndef MOVEMARKER_H
#define MOVEMARKER_H

#include "TMoveCommand.h"
#include "TTimeRef.h"

class Marker;
class MarkerView;

class MoveMarker : public TMoveCommand
{
        Q_OBJECT

public:
        MoveMarker(MarkerView* mview, qint64 scalefactor, const QString& des);

        int prepare_actions();
        int do_action();
        int undo_action();
        int finish_hold();
        int begin_hold();
        void cancel_action();
        int jog();

private :
        Marker*		m_marker;
        TTimeRef		m_origLocation;
        TTimeRef		m_newLocation;
        struct MoveMarkerData {
                MarkerView*	view;
                qint64 		scalefactor;
        };
        MoveMarkerData* mmd;

public slots:
        void move_left();
        void move_right();
        void next_snap_pos();
        void prev_snap_pos();
};

#endif // MOVEMARKER_H
