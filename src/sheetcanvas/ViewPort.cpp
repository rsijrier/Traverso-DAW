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

*/

#include <QMouseEvent>
#include <QResizeEvent>
#include <QEvent>
#include <QRect>
#include <QPainter>
#include <QPixmap>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QEvent>
#include <QStyleOptionGraphicsItem>
#include <QApplication>

#include <Utils.h>
#include "TInputEventDispatcher.h"
#include "Themer.h"

#include "SheetView.h"
#include "ViewPort.h"
#include "ViewItem.h"
#include "ContextPointer.h"

#include "Import.h"
#include <cstdio>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


/**
 * \class ViewPort
 * \brief An Interface class to create Contextual, or so called 'Soft Selection' enabled Widgets.

	The ViewPort class inherits QGraphicsView, and thus is a true Canvas type of Widget.<br />
	Reimplement ViewPort to create a 'Soft Selection' enabled widget. You have to create <br />
	a QGraphicsScene object yourself, and set it as the scene the ViewPort visualizes.

	ViewPort should be used to visualize 'core' data objects. This is done by creating a <br />
	ViewItem object for each core class that has to be visualized. The naming convention <br />
	for classes that inherit ViewItem is: core class name + View.<br />
	E.g. the ViewItem class that represents an AudioClip should be named AudioClipView.

	All keyboard and mouse events by default are propagated to the InputEngine, which in <br />
	turn will parse the events. In case the event sequence was recognized by the InputEngine <br />
	it will ask a list of (pointed) ContextItem's from ContextPointer, which in turns <br />
	call's the pure virtual function get_pointed_context_items(), which you have to reimplement.<br />
	In the reimplemented function, you have to fill the supplied list with ViewItems that are <br />
	under the mouse cursor, and if needed, ViewItem's that _always_ have to be taken into account. <br />
	One can use the convenience functions of QGraphicsView for getting ViewItem's under the mouse cursor!

	Since there can be a certain delay before a key sequence has been verified, the ContextPointer <br />
	stores the position of the first event of a new key fact. This improves the pointed ViewItem <br />
	detection a lot in case the mouse is moved during a key sequence.<br />
	You should use these x/y coordinates in the get_pointed_context_items() function, see:<br />
	ContextPointer::on_first_input_event_x(), ContextPointer::on_first_input_event_y()


 *	\sa ContextPointer, InputEventDispatcher
 */

ViewPort::ViewPort(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
    , AbstractViewPort()
    , m_sv(nullptr)
    , m_mode(0)
{
    PENTERCONS;
    setFrameStyle(QFrame::NoFrame);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);

    setOptimizationFlag(DontAdjustForAntialiasing);
    setOptimizationFlag(DontSavePainterState);
    setMouseTracking(true);
}

ViewPort::~ViewPort()
{
	PENTERDES;
	
    cpointer().set_current_viewport(nullptr);
}

bool ViewPort::event(QEvent * event)
{
	// We want Tab events also send to the InputEngine
	// so treat them as 'normal' key events.
	if (event->type() == QEvent::KeyPress)
	{
		QKeyEvent *ke = static_cast<QKeyEvent *>(event);
		if (ke->key() == Qt::Key_Tab)
		{
			keyPressEvent(ke);
			return true;
		}
	}

	if (event->type() == QEvent::KeyRelease)
	{
		QKeyEvent *ke = static_cast<QKeyEvent *>(event);
		if (ke->key() == Qt::Key_Tab)
		{
			keyReleaseEvent(ke);
			return true;
		}
        }

	return QGraphicsView::event(event);
}

void ViewPort::grab_mouse()
{
    viewport()->grabMouse();
}

void ViewPort::release_mouse()
{
    viewport()->releaseMouse();
}


void ViewPort::mouseMoveEvent(QMouseEvent* event)
{
    PENTER4;

    cpointer().update_mouse_positions(event->pos(), event->globalPos());

    if (cpointer().keyboard_only_input()) {
        event->accept();
        return;
    }

    // Qt generates mouse move events when the scrollbars move
    // since a mouse move event generates a jog() call for the
    // active holding command, this has a number of nasty side effects :-(
    // For now, we ignore such events....
    if (event->pos() == m_oldMousePos) {
        event->accept();
        return;
    }

    m_oldMousePos = event->pos();

    if (ied().is_holding()) {
        // cpointer().update_mouse_positions() will instruct ied() to update holdcommand
        // of new mouse position, so nothing to be done here
        event->accept();
        return;
    }

    QList<ViewItem*> mouseTrackingItems;

    QList<QGraphicsItem *> itemsUnderCursor = scene()->items(cpointer().scene_pos());
    QList<ContextItem*> activeContextItems;

    if (itemsUnderCursor.size())
    {
        foreach(QGraphicsItem* item, itemsUnderCursor)
        {
            if (ViewItem::is_viewitem(item))
            {
                ViewItem* viewItem = static_cast<ViewItem*>(item);
                if (!viewItem->ignore_context())
                {
                    activeContextItems.append(viewItem);
                    if (viewItem->has_mouse_tracking())
                    {
                        mouseTrackingItems.append(viewItem);
                    }
                }
            }
        }
    } else {
        // If no item is below the mouse, default to default cursor
        setCanvasCursorShape(":/cursorFloat", Qt::AlignTop | Qt::AlignHCenter);
    }

    // since sheetview has no bounding rect, and should always have 'active context'
    // add it if it's available
    if (m_sv) {
        activeContextItems.append(m_sv);
    }

    cpointer().set_active_context_items_by_mouse_movement(activeContextItems);

    if (m_sv)
    {
        m_sv->set_canvas_cursor_pos(cpointer().scene_pos());
    }

    // Some ViewItems want to track mouse move events themselves like CurveView
    // to update the soft selected node which cannot be done by the boudingRect of
    // CurveNode since it is too small.
    for(auto item : mouseTrackingItems) {
        item->mouse_hover_move_event();
    }

    event->accept();
}

void ViewPort::tabletEvent(QTabletEvent * event)
{
	PMESG("ViewPort tablet event:: x, y: %d, %d", (int)event->x(), (int)event->y());
	PMESG("ViewPort tablet event:: high resolution x, y: %f, %f",
	      event->hiResGlobalX(), event->hiResGlobalY());
//	cpointer().store_mouse_cursor_position((int)event->x(), (int)event->y());
	
	QGraphicsView::tabletEvent(event);
}

void ViewPort::enterEvent(QEvent* e)
{
    if (ied().is_holding()) {
        // we allready have viewport so do nothing
        e->accept();
        return;
    }

    QGraphicsView::enterEvent(e);

    // even if the mouse is grabbed, the window system can set a default cursor on a leave event
    // with this we override that behavior
    QGuiApplication::setOverrideCursor(Qt::BlankCursor);

	cpointer().set_current_viewport(this);
    setFocus();
    e->accept();
}

void ViewPort::leaveEvent(QEvent* e)
{
    if (ied().is_holding()) {
        e->accept();
        return;
    }

    // always restore an overrided cursor, we did set one in enterEvent()
    QGuiApplication::restoreOverrideCursor();

    cpointer().set_current_viewport(nullptr);

    // Force the next mouse move event to do something
    // even if the mouse didn't move, so switching viewports
    // does update the current context!
    m_oldMousePos = QPoint();
    e->accept();
}

void ViewPort::keyPressEvent( QKeyEvent * e)
{
	ied().catch_key_press(e);
	e->accept();
}

void ViewPort::keyReleaseEvent( QKeyEvent * e)
{
	ied().catch_key_release(e);
	e->accept();
}

void ViewPort::mousePressEvent( QMouseEvent * e )
{
	ied().catch_mousebutton_press(e);
	e->accept();
}

void ViewPort::mouseReleaseEvent( QMouseEvent * e )
{
	ied().catch_mousebutton_release(e);
	e->accept();
}

void ViewPort::mouseDoubleClickEvent( QMouseEvent * e )
{
	ied().catch_mousebutton_press(e);
	e->accept();
}

void ViewPort::wheelEvent( QWheelEvent * e )
{
	ied().catch_scroll(e);
	e->accept();
}

void ViewPort::paintEvent( QPaintEvent* e )
{
// 	PWARN("ViewPort::paintEvent()");
	QGraphicsView::paintEvent(e);
}

void ViewPort::setCanvasCursorShape(const QString &shape, int alignment)
{
    m_sv->set_cursor_shape(shape, alignment);
}

void ViewPort::setCursorText( const QString & text, int mseconds)
{
	m_sv->set_edit_cursor_text(text, mseconds);
}

void ViewPort::set_holdcursor_pos(QPointF pos)
{
	m_sv->set_canvas_cursor_pos(pos);
}

void ViewPort::set_current_mode(int mode)
{
	m_mode = mode;
}
