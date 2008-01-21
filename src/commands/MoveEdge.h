/*
    Copyright (C) 2005-2006 Remon Sijrier 
 
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
 
    $Id: MoveEdge.h,v 1.10 2008/01/21 16:22:11 r_sijrier Exp $
*/

#ifndef MOVEEDGE_H
#define MOVEEDGE_H

#include "Command.h"
#include "defines.h"
#include <QByteArray>

class AudioClip;
class Sheet;
class SheetView;
class AudioClipView;

class MoveEdge : public Command
{
public :
        MoveEdge(AudioClipView* cv, SheetView* sv, QByteArray whichEdge);
        ~MoveEdge();

        int begin_hold();
        int finish_hold();
	void cancel_action();
        int prepare_actions();
        int do_action();
        int undo_action();

        int jog();

private :
        AudioClip* 	m_clip;
        Sheet*		m_sheet;
	AudioClipView*	m_cv;
	SheetView*	m_sv;
        QByteArray	m_edge;
        TimeRef		m_originalPos;
	TimeRef		m_newPos;
	TimeRef		m_otherEdgePos;
};

#endif

