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

#ifndef PLAYHEAD_MOVE_H
#define PLAYHEAD_MOVE_H

#include "TMoveCommand.h"
#include "TTimeRef.h"

class TSession;
class SheetView;
class PlayHead;

class PlayHeadMove : public TMoveCommand
{
	Q_OBJECT

public :
        PlayHeadMove(SheetView* sv);
        ~PlayHeadMove(){}

	int finish_hold();
	int begin_hold();
	void cancel_action();
	int jog();

	void set_cursor_shape(int useX, int useY);
    bool supportsEnterFinishesHold() const {return false;}

private :
	PlayHead*	m_playhead;
        TSession*	m_session;
	bool		m_resync;
	int		m_origXPos{};
	int		m_newXPos{};
	int		m_newYPos{};
        int             m_holdCursorSceneY{};
        TTimeRef         m_newTransportLocation;

        void do_keyboard_move(TTimeRef newLocation, bool centerInView = false);

public slots:
        void move_left();
        void move_right();
        void next_snap_pos();
        void prev_snap_pos();
        void move_to_work_cursor();
        void move_to_start();
};

#endif
