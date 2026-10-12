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

#include "TContextPointer.h"
#include "TViewPortInterface.h"
#include "TContextItem.h"
#include "TConfig.h"
#include "TInputEventDispatcher.h"
#include "TCommand.h"
#include <QCursor>




#include "Debugger.h"
#include "qapplication.h"


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
    QPoint onFirstInputEventGlobalMousePos;
    QPoint  jogStartGlobalMousePos;   // global Mouse Screen position at jog start
    QPoint  viewPortMousePos;
    QPointF  globalMousePos;           // global Mouse Screen position while holding
    QPoint  mouseCursorPosDuringHold;  // global Mouse Screen pos while holding centered in ViewPort
    QPoint  canvasCursorPos;
};

/**
 *
 * @return A reference to the singleton (static) TContextPointer object
 */
TContextPointer& cpointer()
{
	static TContextPointer contextPointer;
	return contextPointer;
}

TContextPointer::TContextPointer()
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
QList< QObject * > TContextPointer::get_context_items( )
{
	PENTER;

	QList<QObject* > contextItems;
	TContextItem* item;
	TContextItem*  nextItem;

	QList<TContextItem*> activeItems;
	if (m_keyboardOnlyInput) {
		activeItems = m_activeContextItems;
	} else {
		activeItems = m_onFirstInputEventActiveContextItems;
	}

	for (int i=0; i < activeItems.size(); ++i) {
        item = activeItems.at(i);
        contextItems.append(item);
        while ((nextItem = item->get_related_context_item())) {
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
void TContextPointer::add_contextitem( QObject * item )
{
	if (! m_contextItemsList.contains(item))
		m_contextItemsList.append(item);
}

void TContextPointer::remove_contextitem(QObject* item)
{
	int index = m_contextItemsList.indexOf(item);
	m_contextItemsList.removeAt(index);
}

void TContextPointer::hold_start()
{
    if (m_viewPort) {
        m_viewPort->grab_mouse();
    }
    m_mouseData->jogStartGlobalMousePos = QCursor::pos();

    QGuiApplication::setOverrideCursor(Qt::BlankCursor);
}

void TContextPointer::hold_finished()
{
    QGuiApplication::restoreOverrideCursor();

    if (m_viewPort) {
        m_viewPort->release_mouse();
        emit contextChanged();
    } else {
        PERROR("Hold finished, but no ViewPort set");
    }
}

void TContextPointer::set_jog_bypass_distance(int distance)
{
	m_jogBypassDistance = distance;
}

void TContextPointer::set_left_mouse_click_bypasses_jog(bool bypassOnLeftMouseClick)
{
	m_mouseLeftClickBypassesJog = bypassOnLeftMouseClick;
}

void TContextPointer::mouse_button_left_pressed()
{
	if (m_mouseLeftClickBypassesJog) {
		set_keyboard_only_input(false);
	}
}

QList< QObject * > TContextPointer::get_contextmenu_items() const
{
	return m_contextMenuItems;
}

void TContextPointer::set_contextmenu_items(const QList< QObject * >& list)
{
	m_contextMenuItems = list;
}

void TContextPointer::set_current_viewport(TViewPortInterface *vp)
{
	PENTER;

    if (ied().is_holding()) {
        PERROR("Setting new viewport when Input Event Dispatcher is holding is not supported");
        return;
    }

    if (m_viewPort) {
        // just in case, it should not be possible at this stage that a viewport
        // still has mouse grab, but if a key release is not catched at the proper
        // time this will avoid a locked terminal ??
        m_viewPort->release_mouse();
    }

    m_viewPort = vp;

    if (!m_viewPort) {
		m_onFirstInputEventActiveContextItems.clear();
		QList<TContextItem *> items;
		set_active_context_items(items);
	}
}

void TContextPointer::set_canvas_cursor_type(CursorType cursorType)
{
    if (!m_viewPort) {
        return;
    }

    m_viewPort->set_canvas_cursor_type(cursorType);
}

void TContextPointer::set_canvas_cursor_text(const QString &text, int mseconds)
{
    if (!m_viewPort)
	{
		return;
	}

    m_viewPort->set_canvas_cursor_text(text, mseconds);
}

void TContextPointer::set_canvas_cursor_pos(QPointF pos) {
    PENTER;
    if (!m_viewPort) {
        PERROR( "Setting canvas cursor pos but I have no ViewPort!" );
        return;
    }

    if (ied().get_holding_command() && ied().get_holding_command()->wants_cursor_position_to_be_restored()) {
        // Capture the target position for the lambda function
        QPoint globalPos = m_mouseData->jogStartGlobalMousePos;

        // Defer the cursor relocation to the end of the event loop
        // This hopefully fixes the issue where the cursor is not reset
        // to this location on Mac
        QTimer::singleShot(0, [globalPos]() {
            QCursor::setPos(globalPos);
        });
    }

    m_viewPort->set_canvas_cursor_pos(pos);
}

void TContextPointer::set_canvas_cursor_data(const QVariant& data)
{
    if (m_viewPort) {
        m_viewPort->set_canvas_cursor_data(data);
    }
}

void TContextPointer::set_canvas_cursor_location(const TTimeRef &location, qreal sceneY)
{
    if (!m_viewPort) {
        return;
    }

    m_viewPort->set_canvas_cursor_location(location, sceneY);
}

int TContextPointer::mouse_viewport_x() const {
    return m_mouseData->viewPortMousePos.x();
}

int TContextPointer::mouse_viewport_y() const
{
    return m_mouseData->viewPortMousePos.y();
}

QPoint TContextPointer::mouse_viewport_pos() const
{
    return m_mouseData->viewPortMousePos;
}

qreal TContextPointer::scene_x() const
{
    if (!m_viewPort) {
        qDebug("scene_x() called, but no ViewPort was set!");
        return 0;
    }
    return m_viewPort->map_to_scene(m_mouseData->viewPortMousePos).x();
}

qreal TContextPointer::scene_y() const
{
    if (!m_viewPort) {
        qDebug("scene_y() called, but no ViewPort was set!");
        return 0;
    }
    return m_viewPort->map_to_scene(m_mouseData->viewPortMousePos).y();
}

QPointF TContextPointer::scene_pos() const
{
    if (!m_viewPort) {
        qDebug("scene_pos() called, but no ViewPort was set!");
        return QPointF(0,0);
    }
    return m_viewPort->map_to_scene(m_mouseData->viewPortMousePos);
}

void TContextPointer::store_canvas_cursor_position(const QPoint& pos)
{
    m_mouseData->canvasCursorPos = pos;
    m_mouseData->viewPortMousePos = pos;
}

int TContextPointer::on_first_input_event_x() const
{
    return m_mouseData->onFirstInputEventPos.x();
}

int TContextPointer::on_first_input_event_y() const
{
    return m_mouseData->onFirstInputEventPos.y();
}

qreal TContextPointer::on_first_input_event_scene_x() const
{
    if (!m_viewPort) {
        // what else to do?
        return -1;
    }
    return m_viewPort->map_to_scene(m_mouseData->onFirstInputEventPos).x();
}

TTimeRef TContextPointer::on_first_input_event_timeref_location() const {
    if (!m_viewPort) {
        return TTimeRef();
    }

    return TTimeRef(on_first_input_event_scene_x() * m_viewPort->get_timeref_scale_factor());
}

TTimeRef TContextPointer::timeref_location() const
{
    if (!m_viewPort) {
        return TTimeRef();
    }

    return TTimeRef(scene_x() * m_viewPort->get_timeref_scale_factor());
}

QPointF TContextPointer::on_first_input_event_scene_pos() const
{
   if (!m_viewPort) {
       // what else to do?
       return QPointF(-1, -1);
   }
   return m_viewPort->map_to_scene(m_mouseData->onFirstInputEventPos);
}

qreal TContextPointer::on_first_input_event_scene_y() const
{
    if (!m_viewPort) {
        // what else to do?
        return -1;
    }
    return m_viewPort->map_to_scene(m_mouseData->onFirstInputEventPos).y();
}

void TContextPointer::set_active_context_items_by_mouse_movement(const QList<TContextItem *> &items)
{
    // printf("list size %lld\n", items.size());
	set_active_context_items(items);
}

void TContextPointer::set_active_context_items_by_keyboard_input(const QList<TContextItem *> &items)
{
	set_keyboard_only_input(true);
    set_active_context_items(items);
}

QPointF TContextPointer::get_global_mouse_pos() const
{
    return m_mouseData->globalMousePos;
}

void TContextPointer::restore_global_mouse_pos_after_context_menu_dispatch() const
{
    QCursor::setPos(m_mouseData->onFirstInputEventGlobalMousePos);
    // Although the global mouse cursor is now back at it's original position,
    // opening a context menu means we lost focus of the ViewPort and the
    // QCursor::setPos() does not give us our ViewPort focus back unfortunately.
    // Calling qApp->processEvents() apparently does:
    qApp->processEvents();
}

void TContextPointer::update_mouse_positions(const QPoint &pos, const QPointF &globalPos)
{
    m_mouseData->viewPortMousePos = pos;
    m_mouseData->globalMousePos = globalPos;

    if (ied().is_holding()) {
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
void TContextPointer::request_viewport_to_detect_items_below_cursor()
{
    if (!m_viewPort) {
        return;
    }
    set_active_context_items(QList<TContextItem*>());
    m_viewPort->detect_items_below_cursor();
}

void TContextPointer::set_active_context_items(const QList<TContextItem *> &items)
{
    if (items == m_activeContextItems) {
        // identical context items, do nothing
        return;
    }

    // if item had active context set it to false
    foreach(TContextItem* oldItem, m_activeContextItems) {
		if (!items.contains(oldItem)){
			oldItem->set_has_active_context(false);
		}
	}

	m_activeContextItems.clear();

	foreach(TContextItem* item, items) {
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

void TContextPointer::prepare_for_shortcut_dispatch()
{
//    `Q_ASSERT(m_viewPort);

    m_onFirstInputEventActiveContextItems = m_activeContextItems;
    m_mouseData->onFirstInputEventPos = m_mouseData->viewPortMousePos;
    m_mouseData->onFirstInputEventGlobalMousePos = QCursor::pos();
}

void TContextPointer::remove_from_active_context_list(TContextItem *item)
{
    m_activeContextItems.removeAll(item);
    item->set_has_active_context(false);
}

void TContextPointer::about_to_delete(TContextItem *item)
{
	m_activeContextItems.removeAll(item);
	m_onFirstInputEventActiveContextItems.removeAll(item);
}

void TContextPointer::set_keyboard_only_input(bool keyboardOnly)
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
