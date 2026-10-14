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

#ifndef TINPUT_EVENT_DISPATCHER_H
#define TINPUT_EVENT_DISPATCHER_H

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QPoint>

#include "TViewPortScroller.h"

#include "defines.h"

class TCommand;
class TCommandController;
class TContextItem;
class TContextPointer;
class TMoveCommand;
class TCommandPlugin;
class TShortCut;
class TShortCutFunction;
class TShortCutManager;

class QKeyEvent;
class QWheelEvent;
class QMouseEvent;

class TInputEventDispatcher : public QObject
{
    Q_OBJECT
public:
    void catch_key_press(QKeyEvent *);
    void catch_key_release(QKeyEvent *);
    void catch_mousebutton_press( QMouseEvent * e );
    void catch_mousebutton_release( QMouseEvent * e );
    void catch_scroll(QWheelEvent * e );

    bool has_collected_number();
    bool is_holding();
    bool is_holding_modifier_key(int keycode);

    TCommand* get_holding_command() const;
    QString get_collected_number() const {return m_sCollectedNumber;}

    void set_numerical_input(const QString& number);
    void set_shortcut_manager(TShortCutManager* manager) {
        m_shortCutManager = manager;
    }

    int dispatch_shortcut_from_contextmenu(TShortCutFunction* function);

    void jog();
    void bypass_jog_until_mouse_movements_exceeded_manhattenlength(int length=50);
    void update_jog_bypass_pos();
    void reject_current_hold_actions();

    TCommand* succes();
    TCommand* failure();
    TCommand* did_not_implement();

private:
    TInputEventDispatcher();
    TInputEventDispatcher(const TInputEventDispatcher&) : QObject() {}
    ~TInputEventDispatcher();

    static const int NO_HOLD_EVENT = -100;

    enum BroadcastResult {
        RESULT_NOT_SET = 0,
        SUCCESS=1,
        FAILURE=2,
        DIDNOTIMPLEMENT=3
    };

    struct HoldModifierKey {
        int             keycode;
        bool            wasExecuted;
        trav_time_t     lastTimeExecuted;
        TShortCut*      shortcut;
    };

    QList<int>          m_modifierKeys;
    QList<int>          m_activeModifierKeys;
    QHash<int, HoldModifierKey>  m_holdModifierKeys;

    QHash<QString, int>	m_modes;
    TShortCutManager*   m_shortCutManager = nullptr;
    TContextPointer*    m_contextPointer{nullptr};
    TViewportScroller   m_viewportScroller{};
    TCommandController* m_holdingController{nullptr};
    TCommand*           m_holdingCommand{nullptr};
    TMoveCommand*       m_moveCommand{nullptr};
    QString             m_sCollectedNumber;
    QPoint              m_jogBypassPos;
    QTimer              m_holdKeyRepeatTimer;

    QTimer              m_shuttleTimer;

    bool 			m_isHolding{false};
    bool			m_enterFinishesHold{false};
    bool			m_cancelHold{false};
    bool			m_bypassJog{false};

    int             m_dispatchResult{RESULT_NOT_SET};
    int             m_unbypassJogDistance{};
    // holdEventKeyCode MUST be a value != ANY key code!
    // when set to 'not matching any key!!!!!!
    int             m_holdEventKeyValue{NO_HOLD_EVENT};

    void 			finish_hold();
    void 			reset();
    bool 			check_number_collection(int eventcode);

    //! call the slot that handler a given action
    int dispatch_shortcut(TShortCut* shortCut, bool fromContextMenu=false);
    TShortCutFunction* find_shortcut_function_for_metaobject(const QMetaObject* metaObject, TShortCut* shortCut);
    TCommand* create_command_from_plugin(QObject *contextItem, TShortCutFunction* shortCutFunction);
    TCommandController* create_controller_from_plugin(QObject* contextItem, TShortCutFunction* shortCutFunction);
    int start_controller(TCommandController* controller, TShortCut* shortCut, bool fromContextMenu);
    void commit_controller(TCommandController* controller);

    void set_holding(bool holding);
    void process_press_event(int keyValue);
    void process_release_event(int keyValue);
    bool is_modifier_keyfact(int eventcode);
    bool modifierKeysMatch(QList<int> first, const QList<int>& second);
    void clear_hold_modifier_keys();


    // allow this function to create one instance
    friend TInputEventDispatcher& ied();

private slots:
    void process_hold_modifier_keys();

    void execute_shuttle_heartbeat();

signals:
    void collectedNumberChanged();
    void holdStarted();
    void holdFinished();
};

// use this function to get the InputEngine object
TInputEventDispatcher& ied();

#endif // TINPUT_EVENT_DISPATCHER_H
