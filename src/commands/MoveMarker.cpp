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

#include "MoveMarker.h"

#include "Marker.h"
#include "MarkerView.h"
#include "SnapList.h"
#include "Sheet.h"
#include "SheetView.h"
#include "TimeLine.h"
#include "TInputEventDispatcher.h"


#include "Debugger.h"

MoveMarker::MoveMarker(MarkerView* mview, qint64 scalefactor, const QString& des)
    : TMoveCommand(mview->get_sheetview(), mview->get_marker(), des)
{
    mmd = new MoveMarkerData;
    mmd->view = mview;
    m_marker= mmd->view->get_marker();
    mmd->scalefactor = scalefactor;
}

int MoveMarker::prepare_actions()
{
	return 1;
}

int MoveMarker::begin_hold()
{
	m_origWhen = m_newWhen = m_marker->get_when();
    m_marker->set_snappable(false);
    mmd->view->set_dragging(true);
	return 1;
}

int MoveMarker::finish_hold()
{
    mmd->view->set_dragging(false);
    delete mmd;
    mmd = nullptr;

	return 1;
}

int MoveMarker::do_action()
{
	m_marker->set_when(m_newWhen);
	m_marker->set_snappable(true);
	return 1;
}

int MoveMarker::undo_action()
{
	PENTER;
	m_marker->set_when(m_origWhen);
	m_marker->set_snappable(true);
	return 1;
}

void MoveMarker::cancel_action()
{
	finish_hold();
	undo_action();
}

void MoveMarker::move_left()
{
    if (d->doSnap) {
        return prev_snap_pos();
	}

	ied().bypass_jog_until_mouse_movements_exceeded_manhattenlength();
	// Move 1 pixel to the left
    TimeRef newpos = TimeRef(m_newWhen - (mmd->scalefactor * d->speed));
	if (newpos < TimeRef()) {
		newpos = TimeRef();
	}
	m_newWhen = newpos;
	m_marker->set_when(m_newWhen);
}

void MoveMarker::move_right()
{
    if (d->doSnap) {
        return next_snap_pos();
	}

	ied().bypass_jog_until_mouse_movements_exceeded_manhattenlength();
	// Move 1 pixel to the right
    m_newWhen = m_newWhen + (mmd->scalefactor * d->speed);
	m_marker->set_when(m_newWhen);
}

void MoveMarker::next_snap_pos()
{
	
	ied().bypass_jog_until_mouse_movements_exceeded_manhattenlength();
	SnapList* slist = m_marker->get_timeline()->get_sheet()->get_snap_list();
	m_newWhen = slist->next_snap_pos(m_newWhen);
	m_marker->set_when(m_newWhen);
}

void MoveMarker::prev_snap_pos()
{
	
	ied().bypass_jog_until_mouse_movements_exceeded_manhattenlength();
	SnapList* slist = m_marker->get_timeline()->get_sheet()->get_snap_list();
	m_newWhen = slist->prev_snap_pos(m_newWhen);
	m_marker->set_when(m_newWhen);
}


int MoveMarker::jog()
{
    TimeRef newpos = TimeRef(cpointer().scene_x() * mmd->scalefactor);

    bool didSnap = false;

    if (d->doSnap) {
		SnapList* slist = m_marker->get_timeline()->get_sheet()->get_snap_list();
        newpos = slist->get_snap_value(newpos, didSnap);
	}

	if (newpos < TimeRef()) {
		newpos = TimeRef();
	}

	m_newWhen = newpos;
	m_marker->set_when(m_newWhen);
    if (didSnap) {
        QPointF scenePos = cpointer().scene_pos();
        scenePos.setX(mmd->view->x());
        cpointer().set_canvas_cursor_pos(scenePos);
    } else {
        cpointer().set_canvas_cursor_pos(cpointer().scene_pos());
    }

//	d->view->set_position(int(m_newWhen / d->scalefactor));
	return 1;
}
