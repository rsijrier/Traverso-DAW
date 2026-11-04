/*
Copyright (C) 2005-2008 Remon Sijrier

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

#include "MoveTrack.h"

#include "TContextPointer.h"
#include "TSheet.h"
#include "TSheetView.h"
#include "TTrack.h"
#include "TTrackView.h"


#include "Debugger.h"

MoveTrack::MoveTrack(TTrackView* view)
    : TMoveCommand(view->get_sheetview(), view->get_related_context_item(), "")
    , m_trackView(view)
{
    m_sv = m_trackView->get_sheetview();
}

MoveTrack::~MoveTrack()
{
}

int MoveTrack::begin_hold()
{
    m_trackView->set_moving(true);

    return 1;
}


int MoveTrack::finish_hold()
{
    m_trackView->set_moving(false);
    return 1;
}


int MoveTrack::prepare_actions()
{

    return -1;
}


int MoveTrack::do_action()
{
    PENTER;

    return 1;
}


int MoveTrack::undo_action()
{
    PENTER;

    return 1;
}

void MoveTrack::cancel_action()
{
    finish_hold();
    undo_action();
}

int MoveTrack::jog()
{

    if (m_trackView->animatedMoveRunning()) {
        return 1;
    }

    if ((m_trackView->scenePos().y() + m_trackView->boundingRect().height()) < m_contextPointer->scene_y()) {
        m_sv->move_trackview_down(m_trackView);
    } else if ((m_trackView->scenePos().y()) > (m_contextPointer->scene_y())) {
        m_sv->move_trackview_up(m_trackView);
    }

    return 1;
}


void MoveTrack::move_up()
{
    m_sv->move_trackview_up(m_trackView);
    m_sv->browse_to_track(m_trackView->get_track());
}

void MoveTrack::move_down()
{
    m_sv->move_trackview_down(m_trackView);
    m_sv->browse_to_track(m_trackView->get_track());
}

void MoveTrack::set_cursor_shape(int /*useX*/, int useY)
{
    if (useY) {
        m_contextPointer->set_canvas_cursor_type(TContextPointer::CursorType::HoldUpDown);
    }
}

void MoveTrack::to_bottom()
{
    m_sv->to_bottom(m_trackView);
    m_sv->browse_to_track(m_trackView->get_track());
}

void MoveTrack::to_top()
{
    m_sv->to_top(m_trackView);
    m_sv->browse_to_track(m_trackView->get_track());
}


// horribly broken :D
