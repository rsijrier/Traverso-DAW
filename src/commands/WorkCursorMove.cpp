/*
    Copyright (C) 2007 Remon Sijrier

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

#include "WorkCursorMove.h"

#include "TContextPointer.h"
#include "TContextItem.h"
#include "TInputEventDispatcher.h"
#include "TClipsViewPort.h"
#include "TLocation.h"
#include "TTimeLineMarker.h"
#include "TSheet.h"
#include "TSnapList.h"
#include "TSheetView.h"
#include "TTimeLineRulerViewPort.h"
#include "TTimeLineRulerView.h"
#include "TTimeLineMarkerView.h"
#include "TTimeLineRuler.h"
#include "TWorkCursorView.h"

#include <Debugger.h>

WorkCursorMove::WorkCursorMove(TSheetView* sv)
    : TMoveCommand(sv, nullptr, "Work Cursor Move")
	, m_session(sv->get_sheet())
	, m_browseMarkers(false)
{
    m_workCursor = d->sv->get_work_cursor();
    m_playCursor = d->sv->get_play_cursor();
}

int WorkCursorMove::finish_hold()
{
	m_session->get_work_snap()->set_snappable(true);
	return -1;
}


int WorkCursorMove::begin_hold()
{
	if (m_session->is_transport_rolling()) {
		m_playCursor->disable_follow();
	}

	m_session->get_work_snap()->set_snappable(false);
	m_contextPointer->set_canvas_cursor_type(TContextPointer::CursorType::HoldLeftRight);
	m_origPos = m_session->get_work_location();
    m_holdCursorSceneY = m_contextPointer->scene_y();
	
	// Jog once, to update the work cursor position to the current mouse position
	jog();
	
	return 1;
}

void WorkCursorMove::cancel_action()
{
	m_session->set_work_at(m_origPos);
	finish_hold();
}

void WorkCursorMove::set_cursor_shape(int useX, int useY)
{
	Q_UNUSED(useX);
	Q_UNUSED(useY);

//        do_keyboard_move(m_session->get_work_location());
}

int WorkCursorMove::jog()
{
	PENTER;
	int x = m_contextPointer->scene_x();

	if (x < 0) {
		x = 0;
	}

    TTimeRef newLocation(x * d->sv->timeref_scalefactor);

	if (newLocation == m_session->get_work_location()) {
		return 1;
	}

    if (m_session->is_snap_on() || d->doSnap) {
		TSnapList* slist = m_session->get_snap_list();
		newLocation = slist->get_snap_value(newLocation);
	}

	m_session->set_work_at(newLocation);

    m_contextPointer->set_canvas_cursor_text(TTimeRef::timeref_to_text(newLocation, d->sv->timeref_scalefactor));
	m_contextPointer->set_canvas_cursor_pos(QPointF(m_workCursor->scenePos().x(), m_holdCursorSceneY));

	return 1;
}

void WorkCursorMove::move_left()
{
	

	if (m_browseMarkers) {
		return browse_to_previous_marker();
	}

	// FIXME this should be done automatically when moving
	// the WC by means of setting the active items under the
	// edit point!!!
	remove_markers_from_active_context();

    if (d->doSnap) {
        return prev_snap_pos();
	}
    do_keyboard_move(m_session->get_work_location() - (d->sv->timeref_scalefactor * d->speed));
}


void WorkCursorMove::move_right()
{
	

	if (m_browseMarkers) {
		return browse_to_next_marker();
	}

	// FIXME this should be done automatically when moving
	// the WC by means of setting the active items under the
	// edit point!!!
	remove_markers_from_active_context();

    if (d->doSnap) {
        return next_snap_pos();
	}
    do_keyboard_move(m_session->get_work_location() + (d->sv->timeref_scalefactor * d->speed));
}


void WorkCursorMove::next_snap_pos()
{
	
	do_keyboard_move(m_session->get_snap_list()->next_snap_pos(m_session->get_work_location()));
}

void WorkCursorMove::prev_snap_pos()
{
	
	do_keyboard_move(m_session->get_snap_list()->prev_snap_pos(m_session->get_work_location()));
}

void WorkCursorMove::do_keyboard_move(TTimeRef newLocation)
{
	ied().bypass_jog_until_mouse_movements_exceeded_manhattenlength();

    d->sv->set_canvas_cursor_location(newLocation, m_holdCursorSceneY);
}

void WorkCursorMove::toggle_snap_on_off()
{
	m_browseMarkers = false;
    TMoveCommand::toggle_snap_on_off();
}

void WorkCursorMove::browse_to_next_marker()
{
	QList<TTimeLineMarker*> markers = m_session->get_timeline_ruler()->get_markers();
	QList<TContextItem*> contexts = m_contextPointer->get_active_context_items();
	TTimeLineMarkerView* view;
	foreach(TContextItem* item, contexts) {
		view = qobject_cast<TTimeLineMarkerView*>(item);
		if (view) {
			m_contextPointer->remove_from_active_context_list(item);
			contexts.removeAll(item);
		}
	}

	TTimeLineMarker* next = nullptr;
	foreach(TTimeLineMarker* marker, markers) {
        if (marker->get_location()->get_start() > m_session->get_work_location()) {
			next = marker;
			break;
		}
	}

	if (next) {
        QList<TTimeLineMarkerView*> markerViews = d->sv->get_timeline_viewport()->get_timeline_view()->get_marker_views();
		foreach(TTimeLineMarkerView* view, markerViews) {
			if (view->get_marker() == next) {
				contexts.prepend(view);
				break;
			}
		}
        do_keyboard_move(next->get_location()->get_start());
	}

	m_contextPointer->set_active_context_items_by_keyboard_input(contexts);
}

void WorkCursorMove::browse_to_previous_marker()
{
	QList<TTimeLineMarker*> markers = m_session->get_timeline_ruler()->get_markers();
	QList<TContextItem*> contexts = m_contextPointer->get_active_context_items();
	TTimeLineMarkerView* view;
	foreach(TContextItem* item, contexts) {
		view = qobject_cast<TTimeLineMarkerView*>(item);
		if (view) {
			m_contextPointer->remove_from_active_context_list(item);
			contexts.removeAll(item);
		}
	}

	TTimeLineMarker* prev = nullptr;
	for (int i=markers.size() - 1; i>= 0; --i) {
		TTimeLineMarker* marker = markers.at(i);
        if (marker->get_location()->get_start() < m_session->get_work_location()) {
			prev = marker;
			break;
		}
	}

	if (prev) {
        QList<TTimeLineMarkerView*> markerViews = d->sv->get_timeline_viewport()->get_timeline_view()->get_marker_views();
		foreach(TTimeLineMarkerView* view, markerViews) {
			if (view->get_marker() == prev) {
				contexts.prepend(view);
				break;
			}
		}

        do_keyboard_move(prev->get_location()->get_start());
	}

	m_contextPointer->set_active_context_items_by_keyboard_input(contexts);
}

void WorkCursorMove::remove_markers_from_active_context()
{
	QList<TContextItem*> contexts = m_contextPointer->get_active_context_items();
	foreach(TContextItem* item, contexts) {
		if (item->inherits("MarkerView")) {
			m_contextPointer->remove_from_active_context_list(item);
		}
	}
}

void WorkCursorMove::move_to_play_cursor()
{
	do_keyboard_move(m_session->get_transport_location());
}

void WorkCursorMove::move_to_start()
{
    do_keyboard_move(TTimeRef());
}
