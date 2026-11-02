/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TrackPan.h"
#include <cmath>
#include "TContextPointer.h"
#include "TTrack.h"
#include "Utils.h"
#include "TInputEventDispatcher.h"
#include "TFloatCompare.h"

TrackPan::TrackPan(TTrack* track, QVariantList args)
    : TCommand(track, "")
{
    m_track = track;
    m_canvasCursorFollowsMouseCursor = false;

    QString des;

    if (!args.empty()) {
        m_newPan = args.at(0).toDouble();
        des = tr("Track Pan: %1").arg("Reset");
        m_origPan = m_track->get_pan();
    } else {
        des = tr("Track Pan");
    }

    setText(des);
}

int TrackPan::prepare_actions()
{
    if (TFloatCompare::compare(m_origPan, m_newPan))
    {
        return -1;
    }
    return 1;
}

int TrackPan::begin_hold()
{
    m_origX = m_contextPointer->mouse_viewport_x();
    m_origPan = m_newPan = m_track->get_pan();

    m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(m_newPan));

    return 1;
}

int TrackPan::finish_hold()
{
    return 1;
}

int TrackPan::do_action()
{
    m_track->set_pan(m_newPan);
    return 1;
}

int TrackPan::undo_action()
{
    m_track->set_pan(m_origPan);
    return 1;
}

void TrackPan::cancel_action()
{
    finish_hold();
    undo_action();
}

void TrackPan::set_cursor_shape(int useX, int useY)
{
    Q_UNUSED(useX);
    Q_UNUSED(useY);

    m_contextPointer->set_canvas_cursor_type(TContextPointer::CursorType::Panning);
}

int TrackPan::jog()
{
    float w = 600.0;
    float ofx = (float) m_origX - m_contextPointer->mouse_viewport_x();
    float p = -2.0f * (ofx) / w ;

    if (p > 0.0f && p < 0.01f) {
        p = 0.01;
    }
    if (p < 0.0f && p > -0.01f) {
        p = -0.01f;
    }

    m_newPan = p + m_newPan;

    if (m_newPan < -1.0f)
        m_newPan = -1.0f;
    if (m_newPan > 1.0)
        m_newPan = 1.0f;

    if (std::fabs(m_newPan) < 0.01f) {
        m_newPan = 0.0f;
    }

    m_track->set_pan(m_newPan);
    m_origX = m_contextPointer->mouse_viewport_x();

    m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(m_newPan));

    return 1;
}

void TrackPan::pan_left()
{
    m_newPan -= 0.05f;
    if (m_newPan < -1.0)
        m_newPan = -1.0f;

    set_value_by_keyboard_input(m_newPan);
}

void TrackPan::pan_right()
{
    m_newPan += 0.05f;
    if (m_newPan > 1.0f)
        m_newPan = 1.0f;

    set_value_by_keyboard_input(m_newPan);
}

void TrackPan::reset_pan()
{
    set_value_by_keyboard_input(0.0f);
}

void TrackPan::set_value_by_keyboard_input(float newPan)
{
    ied().bypass_jog_until_mouse_movements_exceeded_manhattenlength();

    m_newPan = newPan;
    m_track->set_pan(m_newPan);

    m_contextPointer->set_canvas_cursor_text(QByteArray::number(m_newPan, 'f', 2));

    m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(m_newPan));
}
