/*
    Copyright (C) 2010 Remon Sijrier

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


#include "MoveCurveNode.h"

#include "Curve.h"
#include "CurveView.h"
#include "CurveNode.h"
#include "SheetView.h"
#include "Mixer.h"

MoveCurveNode::MoveCurveNode(Curve* curve,
	QList<CurveNode*> nodes,
	float height,
	qint64 scalefactor,
	TimeRef minWhenDiff,
	TimeRef maxWhenDiff,
	double	minValueDiff,
	double	maxValueDiff,
	const QString& des)
    : TMoveCommand(nullptr, curve, des)
    , mcnd(new MoveCurveNode::MoveCurveNodeData)
{
	foreach(CurveNode* node, nodes) {
		CurveNodeData curveData{};
		curveData.node = node;
		curveData.origValue = node->get_value();
		curveData.origWhen = node->get_when();
		m_nodeDatas.append(curveData);
	}

    mcnd->height = height;
    mcnd->minWhenDiff = minWhenDiff;
    mcnd->maxWhenDiff = maxWhenDiff;
    mcnd->minValueDiff = minValueDiff;
    mcnd->maxValueDiff = maxValueDiff;
        mcnd->scalefactor = scalefactor;
        mcnd->verticalOnly = false;

    m_valueDiff = 0.0;
}

void MoveCurveNode::toggle_vertical_only()
{
    mcnd->verticalOnly = !mcnd->verticalOnly;
    if (mcnd->verticalOnly)
	{
		cpointer().set_canvas_cursor_text(tr("Vertical On"), 1000);

	}
	else
	{
		cpointer().set_canvas_cursor_text(tr("Vertical Off"), 1000);
	}
}

int MoveCurveNode::prepare_actions()
{
        return 1;
}

int MoveCurveNode::finish_hold()
{
        delete mcnd;
    mcnd = nullptr;
        return 1;
}

void MoveCurveNode::cancel_action()
{
        delete mcnd;
    mcnd = nullptr;
        undo_action();
}

int MoveCurveNode::begin_hold()
{
        mcnd->mousepos = QPoint(cpointer().on_first_input_event_x(), cpointer().on_first_input_event_y());
	check_and_apply_when_and_value_diffs();
        return 1;
}


int MoveCurveNode::do_action()
{
	foreach(const CurveNodeData& nodeData, m_nodeDatas) {
		nodeData.node->set_when_and_value(nodeData.origWhen + m_whenDiff.universal_frame(), nodeData.origValue + m_valueDiff);
	}

        return 1;
}

int MoveCurveNode::undo_action()
{
	foreach(const CurveNodeData& nodeData, m_nodeDatas) {
		nodeData.node->set_when_and_value(nodeData.origWhen, nodeData.origValue);
	}

    return 1;
}

void MoveCurveNode::move_up()
{
    m_valueDiff += d->speed / mcnd->height;

	check_and_apply_when_and_value_diffs();
}

void MoveCurveNode::move_down()
{
    m_valueDiff -= d->speed / mcnd->height;

	check_and_apply_when_and_value_diffs();
}

void MoveCurveNode::move_left()
{
    m_whenDiff -= mcnd->scalefactor * d->speed;

	check_and_apply_when_and_value_diffs();
}

void MoveCurveNode::move_right()
{
    m_whenDiff += mcnd->scalefactor * d->speed;

	check_and_apply_when_and_value_diffs();
}

void MoveCurveNode::set_cursor_shape(int useX, int useY)
{
//        cpointer().setCursor(":/cursorHoldLrud");
}

int MoveCurveNode::jog()
{
	QPoint mousepos = cpointer().mouse_viewport_pos();

	int dx, dy;
    dx = mousepos.x() - mcnd->mousepos.x();
    dy = mousepos.y() - mcnd->mousepos.y();

    mcnd->mousepos = mousepos;

    m_whenDiff += dx * mcnd->scalefactor;
    m_valueDiff -= dy / mcnd->height;

	return check_and_apply_when_and_value_diffs();
}

int MoveCurveNode::check_and_apply_when_and_value_diffs()
{
    if (mcnd->verticalOnly) {
		m_whenDiff = TimeRef();
	}

    if (m_whenDiff > mcnd->maxWhenDiff) {
        m_whenDiff = mcnd->maxWhenDiff;
	}

    if (m_whenDiff < mcnd->minWhenDiff) {
        m_whenDiff = mcnd->minWhenDiff;
	}

    if (m_valueDiff > mcnd->maxValueDiff) {
        m_valueDiff = mcnd->maxValueDiff;
	}

    if (m_valueDiff < mcnd->minValueDiff) {
        m_valueDiff = mcnd->minValueDiff;
	}

        // NOTE: this obviously only makes sense when the Node == GainEnvelope Node
        // Use a delegate (or something similar) in the future that set's the correct value.
	if (m_nodeDatas.size() == 1) {
		float dbFactor = coefficient_to_dB(m_nodeDatas.first().origValue + m_valueDiff);
        cpointer().set_canvas_cursor_text(QByteArray::number(dbFactor, 'f', 1).append(" dB"));
	}

    return do_action();
}



