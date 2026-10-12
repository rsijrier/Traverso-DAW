/*
    Copyright (C) 2005-2024 Remon Sijrier

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

#ifndef TCONTEXTPOINTER_H
#define TCONTEXTPOINTER_H

#include <QObject>
#include "TTimeRef.h"

class TViewPortInterface;
class TContextItem;

struct TMouseData;

class TContextPointer : public QObject
{
    Q_OBJECT

public:

    enum class CursorType {
        Default,
        GainFader,
        Panning,
        AutomationNode,
        ZoomHorizontal,
        ZoomVertical,
        HoldLeftRight,
        HoldUpDown,
        HoldOmni
    };

    /**
     * 	Returns the current ViewPort's mouse x coordinate

     * @return The current ViewPort's mouse x coordinate
     */
    int mouse_viewport_x() const;

    /**
     * 	Returns the current ViewPort's mouse y coordinate

     * @return The current ViewPort's mouse y coordinate
     */
    int mouse_viewport_y() const;

    /**
     *  Convenience function, equals QPoint(cpointer().x(), cpointer().y())

    *   @return The current ViewPorts mouse position;
    */

    QPoint mouse_viewport_pos() const;

    /**
     * 	Convenience function that maps the ViewPort's mouse x <br />
        coordinate to the scene x coordinate

     * @return The current scene x coordinate, mapped from the ViewPort's mouse x coordinate
     */
    qreal scene_x() const;

    /**
     * 	Convenience function that maps the ViewPort's mouse y <br />
        coordinate to the ViewPort's scene y coordinate

     * @return The current ViewPort's scene y coordinate, mapped from the ViewPort's mouse y coordinate
     */
    qreal scene_y() const;

    /**
     * 	Returns the current's ViewPort's mouse position in the ViewPort's scene position.
     * @return The current's ViewPort's mouse position in the ViewPort's scene position.
     */
    QPointF scene_pos() const;


    /**
     *     	Used by ViewPort to update the internal state of TContextPointer
        Not intended to be used somewhere else.
     * @param x The ViewPort's mouse x coordinate
     * @param y The ViewPort's mouse y coordinate
     */
    void store_canvas_cursor_position(const QPoint& mouse_viewport_pos);

    /**
     *        Returns the ViewPort x coordinate on first input event.
     * @return The ViewPort x coordinate on first input event.
     */
    int on_first_input_event_x() const;

    /**
     *        Returns the ViewPort y coordinate on first input event.
     * @return The ViewPort y coordinate on first input event.
     */
    int on_first_input_event_y() const;

    /**
     *        Returns the scene x coordinate on first input event.
         * @return The scene x coordinate on first input event, -1 if no Port was set
     */
    qreal on_first_input_event_scene_x() const;

    TTimeRef on_first_input_event_timeref_location() const;
    TTimeRef timeref_location() const;

    QPointF on_first_input_event_scene_pos() const;

    /**
     *        Returns the scene y coordinate on first input event.
     * @return The scene y coordinate on first input event.
     */
    qreal on_first_input_event_scene_y() const;

    void set_jog_bypass_distance(int distance);
    void set_left_mouse_click_bypasses_jog(bool bypassOnLeftMouseClick);
    void mouse_button_left_pressed();

    TViewPortInterface* get_viewport() const {
        return m_viewPort;
    }

    void set_current_viewport(TViewPortInterface* vp);
    void set_canvas_cursor_type(CursorType cursorType);
    void set_canvas_cursor_text(const QString& text, int mseconds=-1);
    void set_canvas_cursor_pos(QPointF mouse_viewport_pos);
    void set_canvas_cursor_data(const QVariant& data);
    void set_canvas_cursor_location(const TTimeRef &location, qreal sceneY);


    QList<QObject* > get_context_items();
    QList<TContextItem*> get_active_context_items() const {return m_activeContextItems;}

    void add_contextitem(QObject* item);
    void remove_contextitem(QObject* item);
    void remove_from_active_context_list(TContextItem* item);
    void about_to_delete(TContextItem* item);

    QList<QObject* > get_contextmenu_items() const;
    void set_contextmenu_items(const QList<QObject *> &list);
    void set_active_context_items_by_mouse_movement(const QList<TContextItem*>& items);
    void set_active_context_items_by_keyboard_input(const QList<TContextItem*>& items);

    bool keyboard_only_input() const {return m_keyboardOnlyInput;}
    bool left_mouse_click_bypasses_jog() const {return m_mouseLeftClickBypassesJog;}

    QPointF get_global_mouse_pos() const;
    void restore_global_mouse_pos_after_context_menu_dispatch() const;

    void update_mouse_positions(const QPoint &mouse_viewport_pos, const QPointF &globalPos);
    void request_viewport_to_detect_items_below_cursor();


private:
    TContextPointer();
    TContextPointer(const TContextPointer&);

    // allow this function to create one instance
    friend TContextPointer& cpointer();
    friend class TInputEventDispatcher;
    friend class Traverso;

    void hold_start();
    void hold_finished();

    int m_jogBypassDistance{};

    TMouseData* m_mouseData;

    bool    m_keyboardOnlyInput;
    bool    m_mouseLeftClickBypassesJog;



    TViewPortInterface* m_viewPort;
    TContextItem*     m_currentContext;
    QList<QObject* > m_contextItemsList;
    QList<QObject* > m_contextMenuItems;
    QList<TContextItem*> m_activeContextItems;
    QList<TContextItem*> m_onFirstInputEventActiveContextItems;

    void set_active_context_items(const QList<TContextItem*>& items);

    /**
     *        Called ONLY by InputEngine, not to be used anywhere else.
     */
    void prepare_for_shortcut_dispatch( );

    void set_keyboard_only_input(bool keyboardOnly);

signals:
    void contextChanged();
};

// use this function to access the context pointer
TContextPointer& cpointer();

#endif


//eof


