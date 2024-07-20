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

#ifndef MOVECLIPACTION_H
#define MOVECLIPACTION_H

#include "TMoveCommand.h"

#include <QPoint>
#include <QVariantList>
#include <defines.h>
#include "AudioClipGroup.h"
#include "Marker.h"

class AudioClip;
class TSession;
class AudioTrack;
class SheetView;
class ViewItem;
class Zoom;

typedef struct {
	Marker*	marker;
	TTimeRef origin;
} MarkerAndOrigin;

class MoveClip : public TMoveCommand
{
	Q_OBJECT
	
public :
	MoveClip(ViewItem* view, const QVariantList& args);
        ~MoveClip();

        int begin_hold();
        int finish_hold();
        int prepare_actions();
        int do_action();
        int undo_action();
        void cancel_action();
        int jog();

        void set_jog_bypassed(bool bypassed);
	
private :
	enum ActionType {
        UNDEFINED,
		MOVE,
		COPY,
		FOLD_TRACK,
		FOLD_SHEET,
		FOLD_MARKERS,
		MOVE_TO_START,
		MOVE_TO_END
	};
	
        TSession*	m_session;
	AudioClipGroup  m_group;
	QList<MarkerAndOrigin>	m_markers;
        TTimeRef 	m_trackStartLocation;
        TTimeRef 	m_posDiff;
	ActionType	m_actionType;
	int		m_origTrackIndex;
	int		m_newTrackIndex;
	
    struct MoveClipData {
        qreal 		sceneXStartPos;
        int 		pointedTrackIndex;
		bool		verticalOnly;
		Zoom*		zoom;
        TTimeRef     relativeWorkCursorPos;
	};
	
    MoveClipData* m_d;

	void do_prev_next_snap(TTimeRef trackStartLocation, TTimeRef trackEndLocation);
	void do_move();
	
public slots:
	void next_snap_pos();
	void prev_snap_pos();
        void move_to_start();
        void move_to_end();
	void move_up();
	void move_down();
	void move_left();
	void move_right();
	void start_zoom();
	void toggle_vertical_only();
};

#endif
