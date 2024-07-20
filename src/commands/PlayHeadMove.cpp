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

#include "PlayHeadMove.h"

#include "SheetView.h"
#include "ClipsViewPort.h"
#include "Cursors.h"
#include "SnapList.h"
#include "TSession.h"
#include "TConfig.h"
#include "TInputEventDispatcher.h"

#include <Debugger.h>

PlayHeadMove::PlayHeadMove(SheetView* sv)
    : TMoveCommand(sv, nullptr, "Play Cursor Move")
    , m_session(sv->get_sheet())
{
    m_playhead = d->sv->get_play_cursor();

    m_resync = config().get_property("AudioClip", "SyncDuringDrag", false).toBool();
    m_newTransportLocation = m_session->get_transport_location();
}

int PlayHeadMove::finish_hold()
{
    // When SyncDuringDrag is true, don't seek in finish_hold()
    // since that causes another audio glitch.
    if (!(m_resync && m_session->is_transport_rolling())) {
        // if the sheet is transporting, the seek action will cause
        // the playcursor to be moved to the correct location.
        // Until then hide it, it will be shown again when the seek is finished!
        if (m_session->is_transport_rolling()) {
            m_playhead->hide();
        }

        m_session->set_transport_location(m_newTransportLocation);
    }
    return -1;
}

int PlayHeadMove::begin_hold()
{
    m_playhead->set_active(false);
    m_origXPos = m_newXPos = int(m_session->get_transport_location() / d->sv->timeref_scalefactor);
    m_holdCursorSceneY = cpointer().scene_y();

    ClipsViewPort* port = d->sv->get_clips_viewport();
    cpointer().set_canvas_cursor_pos(QPointF(m_playhead->scenePos().x(), cpointer().mouse_viewport_y()));
    int x = port->mapFromScene(m_playhead->scenePos()).x();

    if (x < 0 || x > port->width()) {
        d->sv->center_in_view(m_playhead, Qt::AlignHCenter);
    }

    return 1;
}

void PlayHeadMove::cancel_action()
{
    m_playhead->set_active(m_session->is_transport_rolling());
    if (!m_resync) {
        m_playhead->setPos(m_origXPos, 0);
    }
}


void PlayHeadMove::set_cursor_shape(int useX, int useY)
{
    Q_UNUSED(useX);
    Q_UNUSED(useY);

    cpointer().set_canvas_cursor_shape(":/cursorHoldLr");
}

int PlayHeadMove::jog()
{
    int x = cpointer().scene_x();
    int y = cpointer().scene_y();
    if (x < 0) {
        x = 0;
    }
    if (x == m_newXPos && y == m_newYPos) {
        return 0;
    }

    if (x != m_newXPos) {
        m_playhead->setPos(x, 0);

        m_newTransportLocation = TTimeRef(x * d->sv->timeref_scalefactor);

        if (m_resync && m_session->is_transport_rolling()) {
            m_session->set_transport_location(m_newTransportLocation);
        }

        cpointer().set_canvas_cursor_text(TTimeRef::timeref_to_text(m_newTransportLocation, d->sv->timeref_scalefactor));
    }

    cpointer().set_canvas_cursor_pos(QPointF(x, y));

    m_newXPos = x;
    m_newYPos = y;

    return 1;
}

void PlayHeadMove::move_left()
{

    if (d->doSnap) {
        return prev_snap_pos();
    }

    do_keyboard_move(m_newTransportLocation - (d->sv->timeref_scalefactor * d->speed));
}


void PlayHeadMove::move_right()
{

    if (d->doSnap) {
        return next_snap_pos();
    }
    do_keyboard_move(m_newTransportLocation + (d->sv->timeref_scalefactor * d->speed));
}


void PlayHeadMove::next_snap_pos()
{

    do_keyboard_move(m_session->get_snap_list()->next_snap_pos(m_newTransportLocation), true);
}

void PlayHeadMove::prev_snap_pos()
{

    do_keyboard_move(m_session->get_snap_list()->prev_snap_pos(m_newTransportLocation), true);
}

void PlayHeadMove::do_keyboard_move(TTimeRef newLocation, bool centerInView)
{
    ied().bypass_jog_until_mouse_movements_exceeded_manhattenlength();

    if (newLocation < TTimeRef()) {
        newLocation = TTimeRef();
    }

    m_newTransportLocation = newLocation;

    if (m_resync && m_session->is_transport_rolling()) {
        m_session->set_transport_location(m_newTransportLocation);
    } else {
        m_playhead->setPos(newLocation / d->sv->timeref_scalefactor, 0);

        int x = d->sv->get_clips_viewport()->mapFromScene(m_playhead->scenePos()).x();


        int canvasWidth = d->sv->get_clips_viewport()->width();
        int nearBorderMargin = 50;
        if (nearBorderMargin > (canvasWidth / 4))
        {
            nearBorderMargin = 0;
        }

        if (x < (0 + nearBorderMargin) || x > (canvasWidth - nearBorderMargin)) {
            d->sv->center_in_view(m_playhead, Qt::AlignHCenter);
        }
    }


    cpointer().set_canvas_cursor_text(TTimeRef::timeref_to_text(m_newTransportLocation, d->sv->timeref_scalefactor));
    d->sv->set_canvas_cursor_pos(QPointF(m_playhead->scenePos().x(), m_holdCursorSceneY), AbstractViewPort::CursorMoveReason::KEYBOARD_NAVIGATION);
}

void PlayHeadMove::move_to_work_cursor()
{
    do_keyboard_move(m_session->get_work_location());
}

void PlayHeadMove::move_to_start()
{
    do_keyboard_move(TTimeRef());
}
