/*
Copyright (C) 2009 Remon Sijrier

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

#include "Crop.h"

#include "AudioClipView.h"
#include "ContextPointer.h"
#include "TCommand.h"
#include "Fade.h"
#include "SheetView.h"
#include "AudioClip.h"
#include "ResourcesManager.h"
#include "ProjectManager.h"
#include "Sheet.h"
#include "AudioTrack.h"
#include "TInputEventDispatcher.h"
#include <limits.h>


 #include <QGraphicsRectItem>

class CropItemView : public QGraphicsRectItem
{
public:
    CropItemView(QGraphicsItem* parent) : QGraphicsRectItem(parent) {}
    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem */*option*/, QWidget */*widget*/);
};


void CropItemView::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) {
    painter->save();
    painter->fillRect(rect(), brush());
    painter->restore();
}

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

/**
 *	\class Crop
	\brief

	\sa TraversoCommands
 */



CropClip::CropClip(AudioClipView* view)
	: TCommand(view->get_related_context_item(), tr("AudioClip: Magnetic Cut"))
	, m_cv(view)
{
	m_clip = view->get_clip();
	m_track = m_clip->get_track();

    m_selection = new CropItemView(m_cv);
    m_selection->setBrush(QColor(100, 100, 100, 100));
	m_selection->setPen(QPen(Qt::NoPen));
	// Set the selection Z value to something sufficiently high
	// to be _always_ on top of all the child views of m_cv
	m_selection->setZValue(m_cv->zValue() + 20);
	x2 = -1;
	x1 = LLONG_MAX;
}


CropClip::~CropClip()
{}

int CropClip::prepare_actions()
{
	leftClip = resources_manager()->get_clip(m_clip->get_id());
	rightClip = resources_manager()->get_clip(m_clip->get_id());

	leftClip->set_sheet(m_clip->get_sheet());
	leftClip->set_location_start(m_clip->get_location()->get_start());
	leftClip->set_right_edge(TTimeRef(x1 * m_cv->get_sheetview()->timeref_scalefactor) + m_clip->get_location()->get_start());
	if (leftClip->get_fade_out()) {
        auto cmd = leftClip->reset_fade_out();
        cmd->set_do_not_push_to_historystack();
		TCommand::process_command(cmd);
	}

	rightClip->set_sheet(m_clip->get_sheet());
	rightClip->set_left_edge(TTimeRef(x2 * m_cv->get_sheetview()->timeref_scalefactor) + m_clip->get_location()->get_start());
    rightClip->get_location()->set_start(leftClip->get_location()->get_end());
	if (rightClip->get_fade_in()) {
        auto cmd = rightClip->reset_fade_in();
        cmd->set_do_not_push_to_historystack();
		TCommand::process_command(cmd);
	}

	return 1;
}

int CropClip::begin_hold()
{
	return 1;
}

int CropClip::finish_hold()
{
	delete m_selection;
    m_selection = nullptr;
	return 1;
}

int CropClip::do_action()
{
	PENTER;

	TCommand::process_command(m_track->add_clip(leftClip, false));
	TCommand::process_command(m_track->add_clip(rightClip, false));

	TCommand::process_command(m_track->remove_clip(m_clip, false));

	return 1;
}

int CropClip::undo_action()
{
	PENTER;

	TCommand::process_command(m_track->add_clip(m_clip, false));

	TCommand::process_command(m_track->remove_clip(leftClip, false));
	TCommand::process_command(m_track->remove_clip(rightClip, false));

	return 1;
}


void CropClip::cancel_action()
{
	finish_hold();
}


int CropClip::jog()
{
	PENTER;

	int x = cpointer().scene_x();

	if (x < 0) {
		x = 0;
	}

	long long splitPoint = x * m_cv->get_sheetview()->timeref_scalefactor;

	QPointF point = m_cv->mapFromScene(splitPoint / m_cv->get_sheetview()->timeref_scalefactor, cpointer().mouse_viewport_y());
	int xpos = (int) point.x();
	if (xpos < 0) {
		xpos = 0;
	}
	if (xpos > m_cv->boundingRect().width()) {
		xpos = (int)m_cv->boundingRect().width();
	}

	if (xpos < x1) x1 = xpos;
	if (xpos > x2) x2 = xpos;

	QRectF rect(0, 0, x2 - x1, m_cv->boundingRect().height());
	m_selection->setRect(rect);
	m_selection->setPos(x1, 0);

	return 1;
}

void CropClip::adjust_left()
{
	ied().bypass_jog_until_mouse_movements_exceeded_manhattenlength();

	int x = (int) m_selection->mapFromScene(cpointer().scene_x(), cpointer().mouse_viewport_y()).x();

	if (x < (m_selection->boundingRect().width() / 2)) {
		x1 -= 1;
		if (x1 < 0) x1 = 0;
	} else {
		if (x2 > (x1 + 1)) {
			x2 -= 1;
		}
	}

	QRectF rect(0, 0, x2 - x1, m_cv->boundingRect().height());
	m_selection->setRect(rect);
	m_selection->setPos(x1, 0);
}

void CropClip::adjust_right()
{
	ied().bypass_jog_until_mouse_movements_exceeded_manhattenlength();

	int x = (int) m_selection->mapFromScene(cpointer().scene_x(), cpointer().mouse_viewport_y()).x();

	if (x < (m_selection->boundingRect().width() / 2)) {
		if (x2 > (x1 + 1)) {
			x1 += 1;
		}
	} else {
		x2 += 1;
		if (x2 > m_cv->boundingRect().width()) {
			x2 = (int)m_cv->boundingRect().width();
		}
	}

	QRectF rect(0, 0, x2 - x1, m_cv->boundingRect().height());
	m_selection->setRect(rect);
	m_selection->setPos(x1, 0);

}

