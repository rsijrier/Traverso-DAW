/*
    Copyright (C) 2010-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "MoveCurveNode.h"
#include "TCurve.h"
#include "TCurveView.h"
#include "TCurveNode.h"
#include "TSheetView.h"
#include "Mixer.h"
#include "TContextPointer.h"

MoveCurveNode::MoveCurveNode(TCurve* curve,
                             QList<TCurveNode*> nodes,
                             float height,
                             qint64 scalefactor,
                             TTimeRef minWhenDiff,
                             TTimeRef maxWhenDiff,
                             double	minValueDiff,
                             double	maxValueDiff,
                             const QString& des)
    : TMoveCommand(nullptr, curve, des)
    , mcnd(std::make_unique<MoveCurveNodeData>())
{
    for (TCurveNode* node : nodes) {
        CurveNodeData curveData{};
        curveData.node = node;
        curveData.origValue = node->get_value();
        curveData.origWhen = node->get_when();
        m_nodeDatas.push_back(curveData);
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
    Q_ASSERT(mcnd != nullptr);

    mcnd->verticalOnly = !mcnd->verticalOnly;
    if (mcnd->verticalOnly) {
        m_contextPointer->set_canvas_cursor_text(tr("Vertical On"), 1000);
    } else {
        m_contextPointer->set_canvas_cursor_text(tr("Vertical Off"), 1000);
    }
}

int MoveCurveNode::prepare_actions()
{
    if (m_whenDiff.universal_frame() == 0 && TraversoDAW::Float::equals_0(m_valueDiff)) {
        return -1;
    }
    return 1;
}

int MoveCurveNode::finish_hold()
{
    // EFFICIENT MEMORY RELEASE: The active drag window is closed.
    // Release temporary data before storing this on the history stack.
    mcnd.reset();
    return 1;
}

void MoveCurveNode::cancel_action()
{
    mcnd.reset();
    undo_action();
}

int MoveCurveNode::begin_hold()
{
    Q_ASSERT(mcnd != nullptr);

    mcnd->mousepos = QPoint(m_contextPointer->on_first_input_event_x(), m_contextPointer->on_first_input_event_y());
    check_and_apply_when_and_value_diffs();
    return 1;
}

int MoveCurveNode::do_action()
{
    for (const CurveNodeData& nodeData : m_nodeDatas) {
        nodeData.node->set_when_and_value(nodeData.origWhen + m_whenDiff.universal_frame(), nodeData.origValue + m_valueDiff);
    }
    return 1;
}

int MoveCurveNode::undo_action()
{
    for (const CurveNodeData& nodeData : m_nodeDatas) {
        nodeData.node->set_when_and_value(nodeData.origWhen, nodeData.origValue);
    }
    return 1;
}

void MoveCurveNode::move_up()
{
    Q_ASSERT(mcnd != nullptr);
    m_valueDiff += d->speed / mcnd->height;
    check_and_apply_when_and_value_diffs();
}

void MoveCurveNode::move_down()
{
    Q_ASSERT(mcnd != nullptr);
    m_valueDiff -= d->speed / mcnd->height;
    check_and_apply_when_and_value_diffs();
}

void MoveCurveNode::move_left()
{
    Q_ASSERT(mcnd != nullptr);
    m_whenDiff -= mcnd->scalefactor * d->speed;
    check_and_apply_when_and_value_diffs();
}

void MoveCurveNode::move_right()
{
    Q_ASSERT(mcnd != nullptr);
    m_whenDiff += mcnd->scalefactor * d->speed;
    check_and_apply_when_and_value_diffs();
}

void MoveCurveNode::set_cursor_shape(int useX, int useY)
{
    Q_UNUSED(useX);
    Q_UNUSED(useY);

    // Switch to the automation node cursor
    m_contextPointer->set_canvas_cursor_type(TContextPointer::CursorType::AutomationNode);

    // The [DRAG] prefix makes the cursor renderer draw drag arrows as soon as
    // the hold starts.
    if (m_nodeDatas.size() == 1) {
        float dbFactor = Mixer::coefficient_to_dB(m_nodeDatas.at(0).origValue + m_valueDiff);
        m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(QString("[DRAG]%1 dB").arg(dbFactor, 0, 'f', 1)));
    } else {
        m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(QStringLiteral("[DRAG]Multiple Nodes")));
    }
}

int MoveCurveNode::jog()
{
    Q_ASSERT(mcnd != nullptr);

    QPoint mousepos = m_contextPointer->mouse_viewport_pos();

    int dx = mousepos.x() - mcnd->mousepos.x();
    int dy = mousepos.y() - mcnd->mousepos.y();

    mcnd->mousepos = mousepos;

    m_whenDiff += dx * mcnd->scalefactor;
    m_valueDiff -= dy / mcnd->height;

    return check_and_apply_when_and_value_diffs();
}

int MoveCurveNode::check_and_apply_when_and_value_diffs()
{
    Q_ASSERT(mcnd != nullptr);

    if (mcnd->verticalOnly) {
        m_whenDiff = TTimeRef();
    }

    if (m_whenDiff > mcnd->maxWhenDiff)  m_whenDiff = mcnd->maxWhenDiff;
    if (m_whenDiff < mcnd->minWhenDiff)  m_whenDiff = mcnd->minWhenDiff;
    if (m_valueDiff > mcnd->maxValueDiff) m_valueDiff = mcnd->maxValueDiff;
    if (m_valueDiff < mcnd->minValueDiff) m_valueDiff = mcnd->minValueDiff;

    if (m_nodeDatas.size() == 1) {
        float dbFactor = Mixer::coefficient_to_dB(m_nodeDatas.at(0).origValue + m_valueDiff);

        // Append the [DRAG] token prefix to signal the crosshair to morph into directional arrows
        if (dbFactor <= Mixer::min_fader_dB()) {
            m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(QStringLiteral("[DRAG]-inf dB")));
        } else {
            m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(QString("[DRAG]%1 dB").arg(dbFactor, 0, 'f', 1)));
        }
    }

    return do_action();
}
