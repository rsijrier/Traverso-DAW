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

    All keyboard and mouse events by default are propagated to the InputEventDispatcher, which in <br />
    turn will parse the events. In case the event sequence was recognized by the InputEventDispatcher <br />
    it will ask a list of (pointed) ContextItem's from ContextPointer


 *	\sa ContextPointer, TInputEventDispatcher
 */

ViewPort::ViewPort(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
    , m_sv(nullptr)
{
    PENTERCONS;
    setFrameStyle(QFrame::NoFrame);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);

    // be aware that if we use antialiazing we have to
    // paint within 2 pixels of the viewitem boudary else
    // make the bouding rect of that viewitem 2 pixels larger
    setOptimizationFlag(DontAdjustForAntialiasing);

    // each viewitem has to call save/store on painter themselves
    setOptimizationFlag(DontSavePainterState);

    // we do not rely on graphicsitem knowing if mouse hovers over it
    // so disable tracking the mouse over items.
    setInteractive(false);

    // but we do enable mouse enter/leave/move events of course so we
    // can inform cpointer and tinputeventdispatcher aoubt soft selected items
    setMouseTracking(true);
}

ViewPort::~ViewPort()
{
	PENTERDES;

    if (cpointer().get_viewport() == this) {
        cpointer().set_current_viewport(nullptr);
    }
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

    // here we detect which items are under the mouse cursor
    detect_items_under_cursor();

    event->accept();
}

void ViewPort::detect_items_under_cursor()
{
    QList<ViewItem*> mouseTrackingItems;

    QList<QGraphicsItem *> itemsUnderCursor = scene()->items(cpointer().scene_pos());
    QList<ContextItem*> activeContextItems;

    if (!itemsUnderCursor.isEmpty())
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

    // update context pointer active context items list
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
    // TODO: setOverrideCursor screws up when showing context menus :(
    // for now, default to old solution by only setting BlankCursor
//    QGuiApplication::setOverrideCursor(Qt::BlankCursor);
    if (m_sv) {
        viewport()->setCursor(Qt::BlankCursor);
    }

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
//    QGuiApplication::restoreOverrideCursor();

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
