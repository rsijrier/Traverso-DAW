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

#ifndef WORKCURSOR_MOVE_H
#define WORKCURSOR_MOVE_H

#include "TMoveCommand.h"
#include "TTimeRef.h"

class TSession;
class SheetView;
class PlayHead;
class WorkCursor;

class WorkCursorMove : public TMoveCommand
{
	Q_OBJECT

public :
	WorkCursorMove (SheetView* sv);
	~WorkCursorMove (){}

	int finish_hold();
	int begin_hold();
	void cancel_action();
	int jog();

	void set_cursor_shape(int useX, int useY);
    bool supportsEnterFinishesHold() const {return false;}

private :
	TSession*	m_session;
	PlayHead*	m_playCursor;
	WorkCursor*     m_workCursor;
	TTimeRef		m_origPos;
	int             m_holdCursorSceneY;
	bool            m_browseMarkers;

	void do_keyboard_move(TTimeRef newLocation);
	void browse_to_next_marker();
	void browse_to_previous_marker();
	void remove_markers_from_active_context();

public slots:
    void move_left();
    void move_right();
    void next_snap_pos();
    void prev_snap_pos();
    void toggle_snap_on_off();
    void move_to_play_cursor();
    void move_to_start();

};

#endif
