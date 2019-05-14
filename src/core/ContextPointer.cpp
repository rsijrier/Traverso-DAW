/*
Copyright (C) 2005-2010 Remon Sijrier

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

#include "ContextPointer.h"
#include "AbstractViewPort.h"
#include "ContextItem.h"
#include "TConfig.h"
#include "TInputEventDispatcher.h"
#include "TCommand.h"
#include <QCursor>


// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


/**
 * \class ContextPointer
 * \brief ContextPointer forms the bridge between the ViewPort (GUI) and the InputEngine (core)
 *
	Use it in classes that inherit ViewPort to discover ViewItems under <br />
	the mouse cursor on the first input event x/y coordinates.<br />

	Also provides convenience functions to get ViewPort x/y coordinates<br />
	as well as scene x/y coordinates, which can be used for example in the <br />
	jog() implementation of Command classes.

	ViewPort's mouse event handling automatically updates the state of ContextPointer <br />
	as well as the InputEngine, which makes sure the mouse is grabbed and released <br />
	during Hold type Command's.

    Use cpointer() to get a reference to the singleton object

 *	\sa ViewPort, TInputEventDispatcher
 */



struct TMouseData {
    QPoint  onFirstInputEventPos;
    QPoint  jogStartGlobalMousePos;   // global Mouse Screen position at jog start
    QPoint  mousePos;
    QPoint  globalMousePos;           // global Mouse Screen position while holding
    QPoint  mouseCursorPosDuringHold;  // global Mouse Screen pos while holding centered in ViewPort
    QPoint  canvasCursorPos;
};

/**
 *
 * @return A reference to the singleton (static) ContextPointer object
 */
ContextPointer& cpointer()
{
	static ContextPointer contextPointer;
	return contextPointer;
}

ContextPointer::ContextPointer()
{
    m_viewPort = nullptr;
    m_currentContext = nullptr;
	m_keyboardOnlyInput = false;
    m_mouseData = new TMouseData{};

	m_mouseLeftClickBypassesJog = config().get_property("InputEventDispatcher", "mouseclicktakesoverkeyboardnavigation", false).toBool();
    m_jogBypassDistance = config().get_property("InputEventDispatcher", "jobbypassdistance", 70).toInt();
}

/**
 *  	Returns a list of all 'soft selected' ContextItems.

	To be able to also dispatch key facts to objects that
	don't inherit from ContextItem, but do inherit from
	QObject, the returned list holds QObjects.

 * @return A list of 'soft selected' ContextItems, as QObject's.
 */
QList< QObject * > ContextPointer::get_context_items( )
{
	PENTER;

	QList<QObject* > contextItems;
	ContextItem* item;
	ContextItem*  nextItem;

	QList<ContextItem*> activeItems;
	if (m_keyboardOnlyInput) {
		activeItems = m_activeContextItems;
	} else {
		activeItems = m_onFirstInputEventActiveContextItems;
	}

	for (int i=0; i < activeItems.size(); ++i) {
        item = activeItems.at(i);
        contextItems.append(item);
        while ((nextItem = item->get_context())) {
			contextItems.append(nextItem);
			item = nextItem;
		}
	}

	for (int i=0; i < m_contextItemsList.size(); ++i) {
		contextItems.append(m_contextItemsList.at(i));
	}


	return contextItems;
}

/**
 * 	Use this function to add an object that inherits from QObject <br />
	permanently to the 'soft selected' item list.

	The added object will always be added to the list returned in <br />
	get_context_items(). This way, one can add objects that do not <br />
	inherit ContextItem, to be processed into the key fact dispatching <br />
	of InputEngine.

 * @param item The QObject to be added to the 'soft selected' item list
 */
void ContextPointer::add_contextitem( QObject * item )
{
	if (! m_contextItemsList.contains(item))
		m_contextItemsList.append(item);
}

void ContextPointer::remove_contextitem(QObject* item)
{
	int index = m_contextItemsList.indexOf(item);
	m_contextItemsList.removeAt(index);
}

void ContextPointer::jog_start()
{
    if (m_viewPort) {
        m_viewPort->grab_mouse();
    }
    m_mouseData->jogStartGlobalMousePos = QCursor::pos();
}

void ContextPointer::jog_finished()
{
    if (m_viewPort) {
        m_viewPort->release_mouse();
		emit contextChanged();
	}
}

void ContextPointer::set_jog_bypass_distance(int distance)
{
	m_jogBypassDistance = distance;
}

void ContextPointer::set_left_mouse_click_bypasses_jog(bool bypassOnLeftMouseClick)
{
	m_mouseLeftClickBypassesJog = bypassOnLeftMouseClick;
}

void ContextPointer::mouse_button_left_pressed()
{
	if (m_mouseLeftClickBypassesJog) {
		set_keyboard_only_input(false);
	}
}

QList< QObject * > ContextPointer::get_contextmenu_items() const
{
	return m_contextMenuItems;
}

void ContextPointer::set_contextmenu_items(const QList< QObject * >& list)
{
	m_contextMenuItems = list;
}

void ContextPointer::set_current_viewport(AbstractViewPort *vp)
{
	PENTER;
    if (m_viewPort) {
        // just in case, it should not be possible at this stage that a viewport
        // still has mouse grab, but if a key release is not catched at the proper
        // time this will avoid a locked terminal ??
        m_viewPort->release_mouse();
    }

    m_viewPort = vp;

    if (!m_viewPort) {
		m_onFirstInputEventActiveContextItems.clear();
		QList<ContextItem *> items;
		set_active_context_items(items);
	}
}

void ContextPointer::set_canvas_cursor_shape(const QString &cursor, int alignment)
{
    if (!m_viewPort)
	{
		return;
	}

    m_viewPort->set_canvas_cursor_shape(cursor, alignment);
}

void ContextPointer::set_canvas_cursor_text(const QString &text, int mseconds)
{
    if (!m_viewPort)
	{
		return;
	}

    m_viewPort->set_canvas_cursor_text(text, mseconds);
}

void ContextPointer::set_canvas_cursor_pos(QPointF pos)
{
    PENTER;
    if (!m_viewPort)
	{
		return;
	}

	if (ied().get_holding_command() && ied().get_holding_command()->restoreCursorPosition())
	{
        QCursor::setPos(m_mouseData->jogStartGlobalMousePos);
	}

    m_viewPort->set_canvas_cursor_pos(pos);
}

int ContextPointer::mouse_viewport_x() const {
    return m_mouseData->mousePos.x();
}

int ContextPointer::mouse_viewport_y() const
{
    return m_mouseData->mousePos.y();
}

QPoint ContextPointer::mouse_viewport_pos() const
{
    return m_mouseData->mousePos;
}

qreal ContextPointer::scene_x() const
{
    if (!m_viewPort) {
        qDebug("scene_x() called, but no ViewPort was set!");
        return 0;
    }
    return m_viewPort->map_to_scene(m_mouseData->mousePos).x();
}

qreal ContextPointer::scene_y() const
{
    if (!m_viewPort) {
        qDebug("scene_y() called, but no ViewPort was set!");
        return 0;
    }
    return m_viewPort->map_to_scene(m_mouseData->mousePos).y();
}

QPointF ContextPointer::scene_pos() const
{
    if (!m_viewPort) {
        qDebug("scene_pos() called, but no ViewPort was set!");
        return QPointF(0,0);
    }
    return m_viewPort->map_to_scene(m_mouseData->mousePos);
}

void ContextPointer::store_canvas_cursor_position(const QPoint& pos)
{
    m_mouseData->canvasCursorPos = pos;
    m_mouseData->mousePos = pos;
}

int ContextPointer::on_first_input_event_x() const
{
    return m_mouseData->onFirstInputEventPos.x();
}

int ContextPointer::on_first_input_event_y() const
{
    return m_mouseData->onFirstInputEventPos.y();
}

qreal ContextPointer::on_first_input_event_scene_x() const
{
    if (!m_viewPort) {
        // what else to do?
        return -1;
    }
    return m_viewPort->map_to_scene(m_mouseData->onFirstInputEventPos).x();
}

QPointF ContextPointer::on_first_input_event_scene_pos() const
{
   if (!m_viewPort) {
       // what else to do?
       return QPointF(-1, -1);
   }
   return m_viewPort->map_to_scene(m_mouseData->onFirstInputEventPos);
}

qreal ContextPointer::on_first_input_event_scene_y() const
{
    if (!m_viewPort) {
        // what else to do?
        return -1;
    }
    return m_viewPort->map_to_scene(m_mouseData->onFirstInputEventPos).y();
}

void ContextPointer::set_active_context_items_by_mouse_movement(const QList<ContextItem *> &items)
{
	set_active_context_items(items);
}

void ContextPointer::set_active_context_items_by_keyboard_input(const QList<ContextItem *> &items)
{
	set_keyboard_only_input(true);
    set_active_context_items(items);
}

QPoint ContextPointer::get_global_mouse_pos() const
{
    return m_mouseData->globalMousePos;
}

void ContextPointer::update_mouse_positions(const QPoint &pos, const QPoint &globalPos)
{
    m_mouseData->mousePos = pos;
    m_mouseData->globalMousePos = globalPos;

    if (ied().is_jogging()) {
        if (m_keyboardOnlyInput) {
            // no need or desire to call the current's
            // Hold Command::jog() function, were moving by keyboard now!
            return;
        }

        ied().jog();
    }

    if (m_keyboardOnlyInput && !m_mouseLeftClickBypassesJog)
    {
        QPoint diff = m_mouseData->jogStartGlobalMousePos - QCursor::pos();
        if (diff.manhattanLength() > m_jogBypassDistance)
        {
            set_keyboard_only_input(false);
        }
    }
}

/**
 * Requests the current set viewport to update the list
 * of contextitems below the mouse cursor even if the
 * mouse did not move (including keyboard navigation)

 * Use case: call when removing/adding a scene object by using Delete
 * and it's related Un/Redo. The canvas cursor will then be updated to
 * the correct contextitem that is below the cursor
**/
void ContextPointer::request_viewport_to_detect_items_below_cursor()
{
    if (!m_viewPort) {
        return;
    }
    set_active_context_items(QList<ContextItem*>());
    m_viewPort->detect_items_below_cursor();
}

void ContextPointer::set_active_context_items(const QList<ContextItem *> &items)
{
    if (items == m_activeContextItems) {
        // identical context items, do nothing
        return;
    }

    // if item had active context set it to false
    foreach(ContextItem* oldItem, m_activeContextItems) {
		if (!items.contains(oldItem)){
			oldItem->set_has_active_context(false);
		}
	}

	m_activeContextItems.clear();

	foreach(ContextItem* item, items) {
		m_activeContextItems.append(item);
		item->set_has_active_context(true);
	}

    if (m_activeContextItems.isEmpty()) {
		if (m_currentContext){
            m_currentContext = nullptr;
			emit contextChanged();
		}
    } else if (m_activeContextItems.first() != m_currentContext){
		m_currentContext = m_activeContextItems.first();
		emit contextChanged();
	}
}

void ContextPointer::prepare_for_shortcut_dispatch()
{
//    `Q_ASSERT(m_viewPort);

    m_onFirstInputEventActiveContextItems = m_activeContextItems;
    m_mouseData->onFirstInputEventPos = m_mouseData->mousePos;
}

void ContextPointer::remove_from_active_context_list(ContextItem *item)
{
    m_activeContextItems.removeAll(item);
    item->set_has_active_context(false);
}

void ContextPointer::about_to_delete(ContextItem *item)
{
	m_activeContextItems.removeAll(item);
	m_onFirstInputEventActiveContextItems.removeAll(item);
}

void ContextPointer::set_keyboard_only_input(bool keyboardOnly)
{
	PENTER;

	if (m_keyboardOnlyInput == keyboardOnly) {
		return;
	}

	m_keyboardOnlyInput = keyboardOnly;

	// Mouse cursor is taking over, let it look like it started
	// from the edit point :)
	if (!keyboardOnly)
	{
        QCursor::setPos(m_viewPort->map_to_global(m_mouseData->canvasCursorPos));
    } else {
        m_mouseData->jogStartGlobalMousePos = QCursor::pos();
    }
}
