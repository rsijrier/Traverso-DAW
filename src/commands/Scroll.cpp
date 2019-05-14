/*
    Copyright (C) 2008 Remon Sijrier
    Copyright (C) 2007 Ben Levitt 
 
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

#include "Scroll.h"

#include "SheetView.h"
#include "ContextPointer.h"
#include "ClipsViewPort.h"
#include "QScrollBar"

Scroll::Scroll(SheetView* sv, QVariantList args)
    : TMoveCommand(sv, nullptr, "Scroll")
{
	m_dx = m_dy = 0;
	
	if (!args.empty()) {
		m_dx = args.at(0).toInt();
	}
	if (args.size() > 1) {
		m_dy = args.at(1).toInt();
	}
}


int Scroll::prepare_actions()
{
	return -1;
}


int Scroll::begin_hold()
{
    set_shuttle_factor_values(m_dx, m_dy);

    if (m_dx) {
		cpointer().set_canvas_cursor_shape("LR");
	} else {
		cpointer().set_canvas_cursor_shape("UD");
	}
	
	return 1;
}


int Scroll::finish_hold()
{
	return 1;
}

int Scroll::do_action( )
{
	return -1;
}

int Scroll::undo_action( )
{
	return -1;
}

void Scroll::move_up()
{
    int step = d->sv->getVScrollBar()->pageStep();
    d->sv->set_vscrollbar_value(d->sv->vscrollbar_value() - step * d->speed);
}

void Scroll::move_down()
{
    int step = d->sv->getVScrollBar()->pageStep();
    d->sv->set_vscrollbar_value(d->sv->vscrollbar_value() + step * d->speed);
}

void Scroll::move_left()
{
    d->sv->set_hscrollbar_value(d->sv->hscrollbar_value() - (d->speed * 5));
}

void Scroll::move_right()
{
    d->sv->set_hscrollbar_value(d->sv->hscrollbar_value() + (d->speed * 5));
}


// eof
