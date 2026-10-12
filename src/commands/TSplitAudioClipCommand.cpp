/*
Copyright (C) 2005-2024 Remon Sijrier

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

#include "TSplitAudioClipCommand.h"

#include "TAudioClip.h"
#include "TAudioTrack.h"
#include "TProjectManager.h"
#include "TResourcesManager.h"
#include "TSheet.h"
#include "TSheetView.h"
#include "TAudioClipView.h"
#include "TLineView.h"
#include "TSnapList.h"
#include "TLocation.h"
#include "TViewItem.h"
#include "TContextPointer.h"
#include "TThemer.h"



#include "Debugger.h"


TSplitAudioClipCommand::TSplitAudioClipCommand(TAudioClipView* view)
    : TMoveCommand(view->get_sheetview(), view->get_clip(), tr("Split Clip"))
{
    m_canvasCursorFollowsMouseCursor = true;
    m_clip = view->get_clip();
    m_session = d->sv->get_sheet();
    m_cv = view;
    m_track = m_clip->get_track();
    Q_ASSERT(m_clip->get_sheet());
}


int TSplitAudioClipCommand::prepare_actions()
{
    if (m_splitLocation == TTimeRef()) {
        m_splitLocation = TTimeRef(m_contextPointer->scene_x() * d->sv->timeref_scalefactor);
    }

    if (m_splitLocation <= m_clip->get_location_start() || m_splitLocation >= m_clip->get_location_start() + m_clip->get_length()) {
        return -1;
    }

    leftClip = resources_manager()->get_clip(m_clip->get_id());
    rightClip = resources_manager()->get_clip(m_clip->get_id());

    leftClip->set_sheet(m_clip->get_sheet());
    leftClip->move_to_location(m_clip->get_location_start());
    leftClip->set_right_edge(m_splitLocation);
    leftClip->reset_fade_out();

    rightClip->set_sheet(m_clip->get_sheet());
    rightClip->set_left_edge(m_splitLocation);
    rightClip->move_to_location(m_splitLocation);
    rightClip->reset_fade_in();

    return 1;
}


int TSplitAudioClipCommand::do_action()
{
    PENTER;
    TAudioClipAddRemoveSpec spec;
    spec.set_is_historable(false);
    spec.set_is_move(false);

    spec.set_clip(leftClip);
    TCommand::process_command(m_track->add_clip(spec));
    spec.set_clip(rightClip);
    TCommand::process_command(m_track->add_clip(spec));

    spec.set_clip(m_clip);
    TCommand::process_command(m_track->remove_clip(spec));

    return 1;
}

int TSplitAudioClipCommand::undo_action()
{
    PENTER;

    TAudioClipAddRemoveSpec spec;
    spec.set_is_historable(false);
    spec.set_is_move(false);

    spec.set_clip(m_clip);
    TCommand::process_command(m_track->add_clip(spec));

    spec.set_clip(leftClip);
    TCommand::process_command(m_track->remove_clip(spec));
    spec.set_clip(rightClip);
    TCommand::process_command(m_track->remove_clip(spec));

    return 1;
}

int TSplitAudioClipCommand::begin_hold()
{
    m_splitcursor = new TLineView(m_cv);
    m_splitcursor->set_color(themer()->get_color("AudioClip:contour"));
    // fake mouse move to update splitcursor position
    jog();

    return 1;
}

int TSplitAudioClipCommand::finish_hold()
{
    delete m_splitcursor;
    m_splitcursor = nullptr;
    m_cv->update();
    return 1;
}

void TSplitAudioClipCommand::cancel_action()
{
    finish_hold();
}

int TSplitAudioClipCommand::jog()
{
    int x = m_contextPointer->scene_x();

    if (x < 0) {
        x = 0;
    }

    m_splitLocation = x * d->sv->timeref_scalefactor;

    if (m_clip->get_sheet()->is_snap_on()) {
        TSnapList* slist = m_clip->get_sheet()->get_snap_list();
        m_splitLocation = slist->get_snap_value(m_splitLocation);
    }

    QPointF point = m_cv->mapFromScene(m_splitLocation / d->sv->timeref_scalefactor, m_contextPointer->mouse_viewport_y());
    int xpos = (int) point.x();
    if (xpos < 0) {
        xpos = 0;
    }
    if (xpos > m_cv->boundingRect().width()) {
        xpos = (int)m_cv->boundingRect().width();
    }
    m_splitcursor->setPos(xpos, 0);

    update_canvas_cursor_text();

    return 1;
}


void TSplitAudioClipCommand::move_left()
{

    if (d->doSnap) {
        return prev_snap_pos();
    }
    do_keyboard_move(m_splitLocation - (d->sv->timeref_scalefactor * d->speed));
}


void TSplitAudioClipCommand::move_right()
{

    if (d->doSnap) {
        return next_snap_pos();
    }
    do_keyboard_move(m_splitLocation + (d->sv->timeref_scalefactor * d->speed));
}


void TSplitAudioClipCommand::next_snap_pos()
{

    do_keyboard_move(m_session->get_snap_list()->next_snap_pos(m_splitLocation));
}

void TSplitAudioClipCommand::prev_snap_pos()
{

    do_keyboard_move(m_session->get_snap_list()->prev_snap_pos(m_splitLocation));
}

void TSplitAudioClipCommand::do_keyboard_move(const TTimeRef &location)
{
    m_splitLocation = location;

    if (m_splitLocation < m_clip->get_location_start()) {
        m_splitLocation = m_clip->get_location_start();
    }
    if (m_splitLocation > m_clip->get_location_end()) {
        m_splitLocation = m_clip->get_location_end();
    }

    QPointF pos = m_cv->mapFromScene(m_splitLocation / d->sv->timeref_scalefactor, m_splitcursor->scenePos().y());
    m_splitcursor->setPos(pos);

    update_canvas_cursor_text();
}

void TSplitAudioClipCommand::update_canvas_cursor_text() const
{
    m_contextPointer->set_canvas_cursor_text(TTimeRef::timeref_to_text(m_splitLocation, d->sv->timeref_scalefactor));
}

// eof

