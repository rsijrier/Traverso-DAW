/*
Copyright (C) 2005-2019 Remon Sijrier

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

#include "SplitClip.h"

#include "AudioClip.h"
#include "AudioTrack.h"
#include "ProjectManager.h"
#include "ResourcesManager.h"
#include "Sheet.h"
#include "SheetView.h"
#include "AudioClipView.h"
#include "LineView.h"
#include "SnapList.h"
#include "ViewItem.h"
#include "Fade.h"
#include "Themer.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


SplitClip::SplitClip(AudioClipView* view)
        : TMoveCommand(view->get_sheetview(), view->get_clip(), tr("Split Clip"))
{
    m_canvasCursorFollowsMouseCursor = true;
	m_clip = view->get_clip();
    m_session = d->sv->get_sheet();
	m_cv = view;
	m_track = m_clip->get_track();
	leftClip = nullptr;
	rightClip = nullptr;
	m_splitPoint = TTimeRef();
	Q_ASSERT(m_clip->get_sheet());
}


int SplitClip::prepare_actions()
{
    if (m_splitPoint == qint64(0)) {
        m_splitPoint = TTimeRef(cpointer().scene_x() * d->sv->timeref_scalefactor);
    }

	if (m_splitPoint <= m_clip->get_location()->get_start() || m_splitPoint >= m_clip->get_location()->get_start() + m_clip->get_length()) {
		return -1;
	}

	leftClip = resources_manager()->get_clip(m_clip->get_id());
	rightClip = resources_manager()->get_clip(m_clip->get_id());
	
	leftClip->set_sheet(m_clip->get_sheet());
	leftClip->set_location_start(m_clip->get_location()->get_start());
	leftClip->set_right_edge(m_splitPoint);
	if (leftClip->get_fade_out()) {
		FadeRange* cmd = (FadeRange*)leftClip->reset_fade_out();
        cmd->set_do_not_push_to_historystack();
		TCommand::process_command(cmd);
	}
	
	rightClip->set_sheet(m_clip->get_sheet());
	rightClip->set_left_edge(m_splitPoint);
	rightClip->set_location_start(m_splitPoint);
	if (rightClip->get_fade_in()) {
		FadeRange* cmd = (FadeRange*)rightClip->reset_fade_in();
        cmd->set_do_not_push_to_historystack();
		TCommand::process_command(cmd);
	}
	
	return 1;
}


int SplitClip::do_action()
{
	PENTER;

	TCommand::process_command(m_track->add_clip(leftClip, false));
	TCommand::process_command(m_track->add_clip(rightClip, false));
	
	TCommand::process_command(m_track->remove_clip(m_clip, false));
	
	return 1;
}

int SplitClip::undo_action()
{
	PENTER;

	TCommand::process_command(m_track->add_clip(m_clip, false));
	
	TCommand::process_command(m_track->remove_clip(leftClip, false));
	TCommand::process_command(m_track->remove_clip(rightClip, false));
	
	return 1;
}

int SplitClip::begin_hold()
{
	m_splitcursor = new LineView(m_cv);
	m_splitcursor->set_color(themer()->get_color("AudioClip:contour"));
    // fake mouse move to update splitcursor position
    jog();

	return 1;
}

int SplitClip::finish_hold()
{
	delete m_splitcursor;
    m_splitcursor = nullptr;
	m_cv->update();
	return 1;
}

void SplitClip::cancel_action()
{
	finish_hold();
}

void SplitClip::set_cursor_shape(int useX, int useY)
{
	Q_UNUSED(useX);
	Q_UNUSED(useY);
	
	cpointer().set_canvas_cursor_shape(":/cursorHoldLr");
}


int SplitClip::jog()
{
	int x = cpointer().scene_x();

	if (x < 0) {
		x = 0;
	}

    m_splitPoint = x * d->sv->timeref_scalefactor;

	if (m_clip->get_sheet()->is_snap_on()) {
		SnapList* slist = m_clip->get_sheet()->get_snap_list();
		m_splitPoint = slist->get_snap_value(m_splitPoint);
	}
	
    QPointF point = m_cv->mapFromScene(m_splitPoint / d->sv->timeref_scalefactor, cpointer().mouse_viewport_y());
	int xpos = (int) point.x();
	if (xpos < 0) {
		xpos = 0;
	}
	if (xpos > m_cv->boundingRect().width()) {
		xpos = (int)m_cv->boundingRect().width();
	}
	m_splitcursor->setPos(xpos, 0);

    cpointer().set_canvas_cursor_text(TTimeRef::timeref_to_text(m_splitPoint, d->sv->timeref_scalefactor));
	
	return 1;
}


void SplitClip::move_left()
{
        
        if (d->doSnap) {
                return prev_snap_pos();
        }
        do_keyboard_move(m_splitPoint - (d->sv->timeref_scalefactor * d->speed));
}


void SplitClip::move_right()
{
        
        if (d->doSnap) {
                return next_snap_pos();
        }
        do_keyboard_move(m_splitPoint + (d->sv->timeref_scalefactor * d->speed));
}


void SplitClip::next_snap_pos()
{
        
        do_keyboard_move(m_session->get_snap_list()->next_snap_pos(m_splitPoint));
}

void SplitClip::prev_snap_pos()
{
        
        do_keyboard_move(m_session->get_snap_list()->prev_snap_pos(m_splitPoint));
}

void SplitClip::do_keyboard_move(TTimeRef location)
{
        m_splitPoint = location;

        if (m_splitPoint < m_clip->get_location()->get_start()) {
                m_splitPoint = m_clip->get_location()->get_start();
        }
        if (m_splitPoint > m_clip->get_location()->get_end()) {
                m_splitPoint = m_clip->get_location()->get_end();
        }

        QPointF pos = m_cv->mapFromScene(m_splitPoint / d->sv->timeref_scalefactor, m_splitcursor->scenePos().y());
        m_splitcursor->setPos(pos);
}

// eof

