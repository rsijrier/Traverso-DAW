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

#include "TMoveCommand.h"

#include "ClipsViewPort.h"
#include "TInputEventDispatcher.h"
#include "Project.h"
#include "ProjectManager.h"
#include "Sheet.h"
#include "SheetView.h"

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

    d->shuttleCurve.setType(QEasingCurve::InCubic);
    d->shuttleCurve.setAmplitude(2.0);

    d->dragShuttleCurve.setType(QEasingCurve::InExpo);

    connect(&d->shuttleTimer, SIGNAL(timeout()), this, SLOT (update_shuttle()));
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
    cleanup_and_free_data();
    return 1;
}

void TMoveCommand::cancel_action()
{
    PENTER;
    cleanup_and_free_data();
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

    int direction = 1;

    qreal normalizedX = cpointer().x() / d->sv->get_clips_viewport()->width();

    if (normalizedX < 0.5) {
        normalizedX = 0.5 - normalizedX;
        normalizedX *= 2;
        direction = -1;
    } else if (normalizedX > 0.5) {
        normalizedX = normalizedX - 0.5;
        normalizedX *= 2;
        if (normalizedX > 1.0) {
            normalizedX *= 1.15;
        }
    }

    qreal value = 1.0;
    if (d->dragShuttle) {
        value = d->dragShuttleCurve.valueForProgress(normalizedX);
    } else {
        value = d->shuttleCurve.valueForProgress(normalizedX);
    }

    if (direction > 0) {
        d->shuttleXfactor = int(value * 30);
    } else {
        d->shuttleXfactor = int(value * -30);
    }

    direction = 1;
    qreal normalizedY = cpointer().y() / d->sv->get_clips_viewport()->height();

    if (normalizedY < 0) normalizedY = 0;
    if (normalizedY > 1) normalizedY = 1;

    if (normalizedY > 0.35 && normalizedY < 0.65) {
        normalizedY = 0;
    } else if (normalizedY < 0.5) {
        normalizedY = 0.5 - normalizedY;
        direction = -1;
    } else if (normalizedY > 0.5) {
        normalizedY = normalizedY - 0.5;
    }

    normalizedY *= 2;

    if (d->dragShuttle) {
        value = d->dragShuttleCurve.valueForProgress(normalizedY);
    } else {
        value = d->shuttleCurve.valueForProgress(normalizedY);
    }

    int yscale;

    if (!d->sv->get_track_views().empty()) {
        yscale = int(d->sv->get_mean_track_height() / 10);
    } else {
        yscale = int(d->sv->get_clips_viewport()->viewport()->height() / 10);
    }

    if (direction > 0) {
        d->shuttleYfactor = int(value * yscale);
    } else {
        d->shuttleYfactor = int(value * -yscale);
    }

    if (d->dragShuttle) {
        d->shuttleYfactor *= 4;
    }

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
    cpointer().setCursorText(tr("Speed: %1").arg(d->speed), 1000);
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
    cpointer().setCursorText(tr("Speed: %1").arg(d->speed), 1000);
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
        cpointer().setCursorText(tr("Speed: %1").arg(d->speed), 1000);
	}
}

void TMoveCommand::toggle_snap_on_off()
{
	Sheet* sheet = pm().get_project()->get_active_sheet();
	sheet->toggle_snap();
    d->doSnap = sheet->is_snap_on();

    if (d->doSnap)
	{
		cpointer().setCursorText(tr("Snap On"), 1000);
	}
	else
	{
		cpointer().setCursorText(tr("Snap Off"), 1000);
	}
}

void TMoveCommand::start_shuttle(bool drag)
{
    if (!d->sv) {
        return;
    }

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

