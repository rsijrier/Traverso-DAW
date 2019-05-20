/*
    Copyright (C) 2010-2019 Remon Sijrier

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

#include "TMoveCommand.h"

#include "ClipsViewPort.h"
#include "TInputEventDispatcher.h"
#include "Project.h"
#include "ProjectManager.h"
#include "Sheet.h"
#include "SheetView.h"

#include <QScrollBar>

#include "Debugger.h"

TMoveCommand::TMoveCommand(SheetView *sv, ContextItem* item, const QString &description)
    : TCommand(item, description)
    , d(new Data())
{
    d->sv = sv;
    d->speed = pm().get_project()->get_keyboard_arrow_key_navigation_speed();

    if (d->sv) {
        d->doSnap = d->sv->get_sheet()->is_snap_on();
    }

    connect(&d->shuttleTimer, SIGNAL(timeout()), this, SLOT (update_shuttle()));
}

void TMoveCommand::cancel_action()
{
    if (!d) {
        return;
    }
    cleanup_and_free_data();
}


int TMoveCommand::begin_hold()
{
    PENTER;
    bool dragShuttle = true;
    start_shuttle(dragShuttle);
    return 1;
}

int TMoveCommand::finish_hold()
{
    PENTER;
    if (!d) {
        return -1;
    }
    cleanup_and_free_data();
    return 1;
}

void TMoveCommand::cleanup_and_free_data()
{
    PENTER;
    stop_shuttle();
    delete d;
    d = nullptr;
}

int TMoveCommand::jog()
{
    if (!d->sv) {
        return -1;
    }

    auto direction = ShuttleDirection::RIGHT;

    qreal normalizedX = qreal(cpointer().mouse_viewport_x()) / d->sv->get_clips_viewport()->width();
    // clips viewport width to be used for active drag shuttle
    qreal dragShuttleRange = 0.18;

    if (normalizedX < dragShuttleRange || normalizedX > (1.0 - dragShuttleRange)) {
        // this is where dragShuttle operates
        if (normalizedX < dragShuttleRange) {
            direction = ShuttleDirection::LEFT;
            // normalize again to range 0.0 - 1.0
            normalizedX = -dragShuttleRange + normalizedX;
            normalizedX *= (1.0 / dragShuttleRange);
        }
        if (normalizedX > (1.0 - dragShuttleRange)) {
            // normalize again to range 0.0 - 1.0
            normalizedX = normalizedX - (1.0 - dragShuttleRange);
            normalizedX *= (1.0 / dragShuttleRange);
        }
    } else {
        normalizedX = 0;
    }

    qreal value = d->shuttleCurve.valueForProgress(qAbs(normalizedX));
    if (std::abs(normalizedX) > 1.0) {
        // cursor went beyong screen boundaries, add a 50% boost for faster scrolling
        value *= 1.5;
    }

    // make shuttleXFactor dependend on viewport width
    qreal viewportWidthScrollStep = d->sv->get_clips_viewport()->width() * dragShuttleRange * dragShuttleRange;
    d->shuttleXfactor = int(value * viewportWidthScrollStep * direction);



    dragShuttleRange = 0.1;
    direction = ShuttleDirection::UP;
    qreal normalizedY = qreal(cpointer().mouse_viewport_y()) / d->sv->get_clips_viewport()->height();

    if (normalizedY < dragShuttleRange || normalizedY > (1.0 - dragShuttleRange)) {
        // this is where dragShuttle operates
        if (normalizedY < dragShuttleRange) {
            direction = ShuttleDirection::DOWN;
            // normalize again to range 0.0 - 1.0
            normalizedY = -dragShuttleRange + normalizedY;
            normalizedY *= (1.0 / dragShuttleRange);
        }
        if (normalizedY > (1.0 - dragShuttleRange)) {
            // normalize again to range 0.0 - 1.0
            normalizedY = normalizedY - (1.0 - dragShuttleRange);
            normalizedY *= (1.0 / dragShuttleRange);
        }
    } else {
        normalizedY = 0;
    }

    value = d->shuttleCurve.valueForProgress(std::abs(normalizedY));

    qreal yscale = int(qreal(d->sv->get_mean_track_height()) * dragShuttleRange * 2);
    d->shuttleYfactor = int(value * yscale * direction);

    return 1;
}

void TMoveCommand::move_faster()
{
    if (d->speed > 32) {
        d->speed = 32;
	}

    if (d->speed == 1) {
        d->speed = 2;
    } else if (d->speed == 2) {
        d->speed = 4;
    } else if (d->speed == 4) {
        d->speed = 8;
    } else if (d->speed == 8) {
        d->speed = 16;
    } else if (d->speed == 16) {
        d->speed = 32;
	}

    pm().get_project()->set_keyboard_arrow_key_navigation_speed(d->speed);
    cpointer().set_canvas_cursor_text(tr("Speed: %1").arg(d->speed), 1000);
}


void TMoveCommand::move_slower()
{
    if (d->speed > 32) {
        d->speed = 32;
	}

    if (d->speed == 32) {
        d->speed = 16;
    } else if (d->speed == 16) {
        d->speed = 8;
    } else if (d->speed == 8) {
        d->speed = 4;
    } else if (d->speed == 4) {
        d->speed = 2;
    } else if (d->speed == 2) {
        d->speed = 1;
	}

    pm().get_project()->set_keyboard_arrow_key_navigation_speed(d->speed);
    cpointer().set_canvas_cursor_text(tr("Speed: %1").arg(d->speed), 1000);
}

void TMoveCommand::process_collected_number(const QString &collected)
{
	PENTER;
	int number = 0;
	bool ok = false;
	QString cleared = collected;
	cleared = cleared.remove(".").remove("-").remove(",");

	if (cleared.size() >= 1) {
		number = QString(cleared.data()[cleared.size() -1]).toInt(&ok);
	}

	if (ok)
	{
		switch(number)
		{
        case 0: d->speed = 1; break;
        case 1: d->speed = 2; break;
        case 2: d->speed = 4; break;
        case 3: d->speed = 8; break;
        case 4: d->speed = 16; break;
        case 5: d->speed = 32; break;
        case 6: d->speed = 64; break;
        case 7: d->speed = 128; break;
        case 8: d->speed = 128; break;
        case 9: d->speed = 128; break;
        default: d->speed = 2;
		}
        pm().get_project()->set_keyboard_arrow_key_navigation_speed(d->speed);
        cpointer().set_canvas_cursor_text(tr("Speed: %1").arg(d->speed), 1000);
        ied().set_numerical_input("");
	}
}

void TMoveCommand::toggle_snap_on_off()
{
	Sheet* sheet = pm().get_project()->get_active_sheet();
	sheet->toggle_snap();
    d->doSnap = sheet->is_snap_on();

    if (d->doSnap)
	{
		cpointer().set_canvas_cursor_text(tr("Snap On"), 1000);
	}
	else
	{
		cpointer().set_canvas_cursor_text(tr("Snap Off"), 1000);
	}
}

void TMoveCommand::start_shuttle(bool drag)
{
    if (!d->sv) {
        return;
    }

    d->shuttleCurve.setType(QEasingCurve::InOutQuad);

    d->shuttleTimer.start(40);
    d->dragShuttle = drag;
    d->shuttleYfactor = d->shuttleXfactor = 0;
    d->sv->stop_follow_play_head();
}

void TMoveCommand::stop_shuttle()
{
    if (d->shuttleTimer.isActive()) {
        d->shuttleTimer.stop();
    }
}

void TMoveCommand::update_shuttle()
{
    if (!d->sv) {
        return;
    }

    int x = d->sv->hscrollbar_value() + d->shuttleXfactor;
    d->sv->set_hscrollbar_value(x);

    int y = d->sv->vscrollbar_value() + d->shuttleYfactor;
    if (d->dragShuttle) {
           d->sv->set_vscrollbar_value(y);
    }

    if (d->shuttleXfactor != 0 || d->shuttleYfactor != 0) {
        ied().jog();
    }
}

void TMoveCommand::set_shuttle_factor_values(int x, int y)
{
    d->shuttleXfactor = x;
    d->shuttleYfactor = y;
}

void TMoveCommand::move_up()
{
    int step = d->sv->getVScrollBar()->pageStep();
    d->sv->set_vscrollbar_value(d->sv->vscrollbar_value() - step * d->speed);
}

void TMoveCommand::move_down()
{
    int step = d->sv->getVScrollBar()->pageStep();
    d->sv->set_vscrollbar_value(d->sv->vscrollbar_value() + step * d->speed);
}

void TMoveCommand::move_left()
{
    d->sv->set_hscrollbar_value(d->sv->hscrollbar_value() - (d->speed * 5));
}

void TMoveCommand::move_right()
{
    d->sv->set_hscrollbar_value(d->sv->hscrollbar_value() + (d->speed * 5));
}

