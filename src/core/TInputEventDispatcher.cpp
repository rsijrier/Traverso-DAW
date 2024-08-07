/*
Copyright (C) 2005-2019 Remon Sijrier

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

#include "TInputEventDispatcher.h"

#include "ContextPointer.h"
#include "Information.h"
#include "TCommand.h"
#include "TMoveCommand.h"
#include "TCommandPlugin.h"
#include "TShortCut.h"
#include "TShortCutFunction.h"
#include "Utils.h"
#include "TShortCutManager.h"
#include "TConfig.h"

#include <QMetaMethod>
#include <QKeyEvent>
#include <QWheelEvent>


// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

#define NO_HOLD_EVENT -100

/**
 * \class InputEventDispatcher
 * \brief Processes keyboard/mouse events, dispatches the result, and handles the returned Command objects
 *
        InputEngine forms, together with ViewPort, Command, ContextPointer, QObject <br />
        and Qt's Undo Framework, the framework that makes up the the Contextual <br />
        Interaction Interface, with analog type of actions, and un/redo (aka History) support.


        <b>Dispatching key facts to QObject objects</b>

        InputEngine parses the keyboard/mouse events generated by the pointed ViewPort<br />
        If the keysequence matches that of any given in the keymap file, it call's<br />
        broadcast_action(). A list of pointed QObject objects is retrieved then <br />
        from ContextPointer. This list represents all (gui) QObject objects with their <br />
        corresponding 'core' QObject objects, stacked, with the topmost gui object on top.

        For each QObject in the list, the class name is retreived, and looked up in the keymap <br />
        if for the detected key fact an object was supplied with the exact same name.<br />
        If this is the case, the function name, also given in the keymap file by the given object name <br />
        is used to call the QObject's function. If succesfull, the InputEngine will stop iterating <br />
        over the list, and start handling the returned Command object.

        If the keymap specified that the object's doesn't have a function (slot) to be called, but instead<br />
        uses a TCommandPlugin, the list of loaded CommandPlugins is searched to find a match for the <br />
        plugin name supplied in the keymap file, if there is a match, the Plugin is used to create <br />
        the Command object, and the same routine is used to handle this Command object.

        If the Command object returned no error during the handling, it'll be placed on it's <br />
        historystack. If no historystack was available, it's do_action() will be called, and <br />
        deleted afterwards.


 *	\sa Command, ContextPointer, ViewPort, TCommandPlugin
 */


TInputEventDispatcher& ied()
{
    static TInputEventDispatcher inputEventDispatcher;
    return inputEventDispatcher;
}

TInputEventDispatcher::TInputEventDispatcher()
{
    PENTERCONS;
    m_holdingCommand = nullptr;
    m_moveCommand = nullptr;
    // holdEvenCode MUST be a value != ANY key code!
    // when set to 'not matching any key!!!!!!
    m_holdEventCode = NO_HOLD_EVENT;
    m_cancelHold = false;
    m_bypassJog = false;
    m_enterFinishesHold = false;
    m_sCollectedNumber = "";

    m_modifierKeys << Qt::Key_Shift << Qt::Key_Control << Qt::Key_Alt << Qt::Key_Meta;

    connect(&m_holdKeyRepeatTimer, SIGNAL(timeout()), this, SLOT(process_hold_modifier_keys()));
}

TInputEventDispatcher::~ TInputEventDispatcher( )
= default;

int TInputEventDispatcher::dispatch_shortcut_from_contextmenu(TShortCutFunction* function)
{
    PENTER2;
    QStringList keys = function->getKeys();
    if (!keys.size())
    {
        return -1;
    }
    TShortCut* shortCut = tShortCutManager().getShortcutForKey(keys.first());

    if (! shortCut) {
        //		PERROR("ContextMenu keySequence doesn't apply to any InputEngine knows off!! (%s)", QS_C(keys.first()));
        return -1;
    }

    foreach(int modifier, function->getModifierKeys())
    {
        m_activeModifierKeys.append(modifier);
    }

    if (function->commandName == "RejectHoldCommand") {
        process_press_event(Qt::Key_Escape);
        return 1;
    }

    if (function->commandName == "AcceptHoldCommand") {
        process_press_event(Qt::Key_Enter);
        return 1;
    }

    dispatch_shortcut(shortCut, true);

    m_activeModifierKeys.clear();

    return 1;
}


int TInputEventDispatcher::dispatch_shortcut(TShortCut* shortCut, bool fromContextMenu)
{
    PENTER2;
    PMESG("Dispatching key %d", shortCut->getKeyValue());

    TCommand* command = nullptr;
    QString slotsignature = "";
    QList<QObject* > contextItemsList = fromContextMenu ? cpointer().get_contextmenu_items() : cpointer().get_context_items();
    QObject* contextItem = nullptr;

    if (m_holdingCommand) {
        contextItemsList.prepend(m_holdingCommand);
    }

    for (int i=0; i < contextItemsList.size(); ++i) {
        command = nullptr;
        m_dispatchResult = 0;

        contextItem = contextItemsList.at(i);

        if (!contextItem) {
            PERROR("no item in cpointer()'s context item list ??");
            continue;
        }

        TShortCutFunction* shortCutFunction = nullptr;

        const QMetaObject* metaObject = contextItem->metaObject();
        // traverse upwards till no more superclasses are found
        // this supports inheritance on QObjects.
        while (metaObject)
        {
            QList<TShortCutFunction*> functions = shortCut->getFunctionsForObject(metaObject->className());

            foreach(TShortCutFunction* function, functions) {
                if (!function) {
                    continue;
                }

                if (m_activeModifierKeys.size())
                {
                    if (modifierKeysMatch(m_activeModifierKeys, function->getModifierKeys())) {
                        shortCutFunction = function;
                        PMESG("found match in objectUsingModierKeys");
                        break;
                    } else {
                        PMESG("m_activeModifierKeys doesn't contain code %d", shortCut->getKeyValue());
                    }
                }
                else
                {
                    if (function->getModifierKeys().isEmpty())
                    {
                        shortCutFunction = function;
                        PMESG("found match in obects NOT using modifier keys");
                        break;
                    }
                }
            }

            if (shortCutFunction)
            {
                break;
            }

            metaObject = metaObject->superClass();
        }


        if (! shortCutFunction ) {
            PMESG("No data found for object %s", contextItem->metaObject()->className());
            continue;
        }

        PMESG("Function found for %s!", metaObject->className());
        PMESG("setting slotsignature to %s", QS_C(shortCutFunction->getSlotSignature()));
        PMESG("setting pluginname to %s", QS_C(shortCutFunction->pluginname));
        PMESG("setting plugincommand to %s", QS_C(shortCutFunction->commandName));

        slotsignature = shortCutFunction->getSlotSignature();
        QString pluginname = shortCutFunction->pluginname;
        QString commandname = shortCutFunction->commandName;

        if (contextItem == m_holdingCommand) {
            PMESG("Dispatching to holdcommand %s", m_holdingCommand->metaObject()->className());
            if (QMetaObject::invokeMethod(contextItem, QS_C(slotsignature), Qt::DirectConnection)) {
                PMESG("HIT, invoking %s::%s", m_holdingCommand->metaObject()->className(), QS_C(slotsignature));
                // only now we know which object this hold modifier key was dispatched on.
                // the process_hold_modifier_keys() only knows about the corresonding ieaction
                // next time it'll be called, autorepeat interval of the object + keysequence will
                // be used!
                shortCut->autorepeatInterval = shortCutFunction->getAutoRepeatInterval();
                shortCut->autorepeatStartDelay = shortCutFunction->getAutoRepeatStartDelay();
                if (shortCutFunction->usesAutoRepeat() && !m_holdKeyRepeatTimer.isActive()) {
                    m_holdKeyRepeatTimer.start(10);
                }

                break;
            } else {
                PMESG("InvokeMethod failed: %s::%s", m_holdingCommand->metaObject()->className(), QS_C(slotsignature));
            }
        }


        // We first try to find if there is a match in the loaded plugins.
        if ( ! m_holdingCommand ) {

            if ( ! pluginname.isEmpty() ) {
                TCommandPlugin* plug = tShortCutManager().getCommandPlugin(pluginname);
                if (!plug)
                {
                    info().critical(tr("Command Plugin %1 not found!").arg(pluginname));
                    continue;
                }

                if ( ! plug->implements(commandname) )
                {
                    info().critical(tr("Plugin %1 doesn't implement Command %2").arg(pluginname, commandname));
                } else
                {
                    PMESG("InputEngine:: Using plugin %s for command %s", QS_C(pluginname), QS_C(shortCutFunction->commandName));
                    command = plug->create(contextItem, commandname, shortCutFunction->arguments);
                }
            }
        }

        // Either the plugins didn't have a match, or we are holding.
        if ( ! command )
        {
            // FIXME shortCut->getFunctionsForObject() returns a list,
            // we need to iterate over the list for a match or what ?
            QString delegatedobject;
            QList<TShortCutFunction*> objectFunctions;

            if (m_holdingCommand) {
                objectFunctions = shortCut->getFunctionsForObject("HoldCommand");
                delegatedobject = "HoldCommand";
            } else {
                delegatedobject = metaObject->className();
                if (!m_activeModifierKeys.empty()) {
                    //FIXME: objects has values inserted with insertMulti()
                    // do we have to use values(delegatedobject) instead of value(delegatedobject)
                    // here too?
                    objectFunctions = shortCut->getFunctionsForObject(delegatedobject);
                } else {
                    objectFunctions = shortCut->getFunctionsForObject(delegatedobject);
                }
                PMESG("delegatedobject is %s", QS_C(delegatedobject));
            }

            if (!objectFunctions.empty()) {
                shortCutFunction = objectFunctions.first();
            } else {
                shortCutFunction = nullptr;
            }

            if ( ! shortCutFunction) {
                PMESG("No delegating data ? WEIRD");
                continue;
            }

            QStringList strlist = shortCutFunction->getSlotSignature().split("::");

            if (strlist.size() == 2) {
                PMESG("Detected delegate action, checking if it is valid!");
                const QString& classname = strlist.at(0);
                const QString& slot = strlist.at(1);
                QObject* obj = nullptr;
                bool validobject = false;

                for (int j=0; j < contextItemsList.size(); ++j) {
                    obj = contextItemsList.at(j);
                    const QMetaObject* mo = obj->metaObject();
                    while (mo) {
                        if (mo->className() == classname) {
                            PMESG("Found an item in the objects list that equals delegated object");
                            validobject = true;
                            break;
                        }
                        mo = mo->superClass();
                    }
                    if (validobject) {
                        break;
                    }

                }

                if (validobject) {
                    if (QMetaObject::invokeMethod(obj, QS_C(slot),  Qt::DirectConnection, Q_RETURN_ARG(TCommand*, command))) {
                        PMESG("HIT, invoking (delegated) %s::%s", QS_C(classname), QS_C(slot));
                    } else {
                        PMESG("Delegated object slot call didn't work out, sorry!");
                        PMESG("%s::%s() --> %s::%s()", contextItem->metaObject()->className(), QS_C(slot), QS_C(classname), QS_C(slot));
                    }
                } else {
                    PMESG("Delegated object %s was not found in the context items list!", QS_C(classname));
                }
            } else {
                if (QMetaObject::invokeMethod(contextItem, QS_C(slotsignature), Qt::DirectConnection, Q_RETURN_ARG(TCommand*, command))) {
                    PMESG("HIT, invoking %s::%s", contextItem->metaObject()->className(), QS_C(slotsignature));
                } else {
                    PMESG("nope %s wasn't the right one, next ...", contextItem->metaObject()->className());
                }
            }
        }


        // Let's see if the invoked object used either succes(), failure() or did_not_implement()
        // return functions, so we can detect to either return happily, the action was succesfull
        // but no command object needed to be returned, the action was not succesfull, and we
        // don't want to try lower level context items or the action was succesfull but we'd like
        // to give a lower level object precedence over the current one.
        if (m_dispatchResult) {
            if (m_dispatchResult == SUCCESS) {
                PMESG("Broadcast Result indicates succes, but no returned Command object");
                reset();
                return 1;
            }
            if (m_dispatchResult == FAILURE) {
                PMESG("Broadcast Result indicates failure, and doesn't want lower level items to be processed");
                reset();
                return 0;
            }
            if (m_dispatchResult == DIDNOTIMPLEMENT) {
                PMESG("Broadcast Result indicates succes, but didn't want to perform it's action,"
                      "so we continue traversing the objects list");
                continue;
            }
        }


        if (command) {
            if (command->is_hold_command())
            {
                if (command->begin_hold() != -1) {
                    command->set_valid(true);
                    command->set_cursor_shape(shortCutFunction->useX, shortCutFunction->useY);
                    if (has_collected_number()) {
                        command->process_collected_number(get_collected_number());
                        set_numerical_input("");
                    }
                    m_holdingCommand = command;
                    m_moveCommand = qobject_cast<TMoveCommand*>(m_holdingCommand);
                    if (m_moveCommand) {
                        m_moveCommand->TMoveCommand::begin_hold();
                    }
                    m_holdEventCode = shortCut->getKeyValue();
                    set_holding(true);
                    m_enterFinishesHold = config().get_property("InputEventDispatcher", "EnterFinishesHold", false).toBool();
                    if (fromContextMenu && command->supportsEnterFinishesHold())
                    {
                        m_enterFinishesHold = true;
                    }
                    if (shortCutFunction->usesAutoRepeat())
                    {
                        PMESG("Function uses autorepeat");
                        process_press_event(shortCut->getKeyValue());
                    }
                    if (!command->supportsEnterFinishesHold())
                    {
                        m_enterFinishesHold = false;
                    }

                    bool showCursorShortCutHelp = config().get_property("ShortCuts", "ShowCursorHelp", false).toBool();
                    if (showCursorShortCutHelp && (fromContextMenu || m_enterFinishesHold)) {
                        int key = shortCut->getKeyValue();
                        QString keyString;
                        if (key == Qt::LeftButton) {
                            keyString = tr("Mouse Button Left");
                        } else {
                            keyString = QKeySequence(shortCut->getKeyValue()).toString();
                        }
                        cpointer().set_canvas_cursor_text(tr("%1 or Enter to accept, Esc to cancel").arg(keyString));
                    }
                } else {
                    PWARN("hold action begin_hold() returned -1");
                    // OOPSSS, something went wrong when making the Command
                    // set following stuff to zero to make finish_hold do nothing
                    delete command;
                    command = nullptr;
                    set_holding( false );
                }
            }
            else
            {
                TCommand::process_command(command);
            }
        }

        break;
    }

    return 1;
}

TCommand* TInputEventDispatcher::succes()
{
    m_dispatchResult = SUCCESS;
    return nullptr;
}

TCommand* TInputEventDispatcher::failure()
{
    m_dispatchResult = FAILURE;
    return nullptr;
}

TCommand* TInputEventDispatcher::did_not_implement()
{
    m_dispatchResult = DIDNOTIMPLEMENT;
    return nullptr;
}

void TInputEventDispatcher::jog()
{
    PENTER3;

    if (!m_isHolding) {
        PERROR("jog() called but not holding, invalid call to jog()");
        return;
    }

    if (!m_holdingCommand) {
        PERROR("jog() called but no holdingCommand but m_isHolding was set to true, internal state error, fix the program!");
        return;
    }

    if (m_bypassJog) {
        QPoint diff = m_jogBypassPos - cpointer().mouse_viewport_pos();
        if (diff.manhattanLength() > m_unbypassJogDistance) {
            m_bypassJog = false;
            m_holdingCommand->set_jog_bypassed(m_bypassJog);
        } else {
            return;
        }
        m_jogBypassPos = cpointer().mouse_viewport_pos();
    }

    if (m_holdingCommand->jog() == 1 && m_holdingCommand->canvas_cursor_follows_mouse_cursor()) {
        if (m_moveCommand) {
            m_moveCommand->TMoveCommand::jog();
        }
        cpointer().set_canvas_cursor_pos(cpointer().scene_pos());
    }
}

void TInputEventDispatcher::bypass_jog_until_mouse_movements_exceeded_manhattenlength(int length)
{
    m_unbypassJogDistance = length;
    m_bypassJog = true;
    m_jogBypassPos = cpointer().mouse_viewport_pos();
    if (m_holdingCommand) {
        m_holdingCommand->set_jog_bypassed(m_bypassJog);
    }
}

void TInputEventDispatcher::update_jog_bypass_pos()
{
    m_jogBypassPos = cpointer().mouse_viewport_pos();
}

void TInputEventDispatcher::set_holding(bool holding)
{
    PENTER;

    m_isHolding = holding;

    if (m_isHolding) {
        emit holdStarted();
        cpointer().hold_start();
    } else {
        cpointer().hold_finished();
        emit holdFinished();
    }
}

void TInputEventDispatcher::reset()
{
    PENTER;
    set_holding(false);
    m_cancelHold = false;
    m_bypassJog = false;
    m_enterFinishesHold = false;

    set_numerical_input("");
}

void TInputEventDispatcher::reject_current_hold_actions()
{
    m_activeModifierKeys.clear();
    clear_hold_modifier_keys();
    // Fake an escape key press, so if a hold action was
    // running it will be canceled!
    if (is_holding()) {
        process_press_event(Qt::Key_Escape);
    }
}

void TInputEventDispatcher::catch_key_press(QKeyEvent * e )
{
    if (e->isAutoRepeat()) {
        return;
    }
    PENTER;

    process_press_event(e->key());
}

void TInputEventDispatcher::catch_key_release( QKeyEvent * e)
{
    if (e->isAutoRepeat()) {
        return;
    }
    PENTER;
    process_release_event(e->key());
}

void TInputEventDispatcher::catch_mousebutton_press( QMouseEvent * e )
{
    if (e->button() == Qt::LeftButton) {
        cpointer().mouse_button_left_pressed();
    }
    process_press_event(int(e->button()));
}

void TInputEventDispatcher::catch_mousebutton_release( QMouseEvent * e )
{
    process_release_event(int(e->button()));
}

void TInputEventDispatcher::catch_scroll(QWheelEvent* e)
{
    if (e->angleDelta().y() > 0) {
        process_press_event(TShortCutManager::MouseScrollVerticalUp);
        process_release_event(TShortCutManager::MouseScrollVerticalUp);
    }
    if (e->angleDelta().y() < 0) {
        process_press_event(TShortCutManager::MouseScrollVerticalDown);
        process_release_event(TShortCutManager::MouseScrollVerticalDown);
    }
}

void TInputEventDispatcher::process_press_event(int keyValue)
{
    PENTER;
    if (keyValue == Qt::Key_Escape && is_holding())
    {
        m_cancelHold = true;
        finish_hold();
        return;
    }

    if (keyValue == m_holdEventCode && is_holding() && m_holdingCommand->supportsEnterFinishesHold())
    {
        finish_hold();
        return;
    }


    if ((keyValue == Qt::Key_Return || keyValue == Qt::Key_Enter) && m_enterFinishesHold)
    {
        finish_hold();
        return;
    }

    // first check if this key is just a collected number
    if (check_number_collection(keyValue))
    {
        // another digit was collected.
        return;
    }

    if (is_modifier_keyfact(keyValue))
    {
        if (!m_activeModifierKeys.contains(keyValue))
        {
            m_activeModifierKeys.append(keyValue);
        }
        return;
    }

    TShortCut* shortCut = tShortCutManager().getShortcutForKey(keyValue);

    if (m_isHolding && shortCut)
    {
        HoldModifierKey* hmk = new HoldModifierKey;
        hmk->keycode = keyValue;
        hmk->wasExecuted = false;
        hmk->lastTimeExecuted = 0;
        hmk->shortcut = shortCut;
        m_holdModifierKeys.insert(keyValue, hmk);
        // execute the first one directly, this is needed
        // if the release event comes before the timer actually
        // fires (mouse scroll wheel does press/release events real quick
        process_hold_modifier_keys();
        return;
    }

    if (shortCut)
    {
        cpointer().prepare_for_shortcut_dispatch();
        dispatch_shortcut(shortCut);
        return;
    }
}

void TInputEventDispatcher::process_release_event(int eventcode)
{

    if (is_modifier_keyfact(eventcode)) {
        m_activeModifierKeys.removeAll(eventcode);
        return;
    }

    if (m_isHolding) {
        if (m_holdModifierKeys.contains(eventcode)) {
            HoldModifierKey* hmk = m_holdModifierKeys.take(eventcode);
            delete hmk;
            if (m_holdModifierKeys.isEmpty()) {
                m_holdKeyRepeatTimer.stop();
            }
        }

        if (eventcode != m_holdEventCode) {
            PMESG("release event during hold action, but NOT for holdaction itself!!");
            return;
        } else {
            if (m_enterFinishesHold)
            {
                PMESG("Only Enter or Esc keys are accepted to finish a hold command");
                return;
            }
            PMESG("release event for hold action detected!");
            finish_hold();
        }
    }
}

void TInputEventDispatcher::process_hold_modifier_keys()
{
    PENTER;
    if (m_holdModifierKeys.empty()) {
        m_holdKeyRepeatTimer.stop();
        return;
    }

    foreach(HoldModifierKey* hmk, m_holdModifierKeys) {
        if (!hmk->wasExecuted) {
            hmk->wasExecuted = true;
            dispatch_shortcut(hmk->shortcut);
            hmk->lastTimeExecuted = TTimeRef::get_milliseconds_since_epoch() + hmk->shortcut->autorepeatStartDelay;
            continue;
        }

        trav_time_t timeDiff = (TTimeRef::get_milliseconds_since_epoch() - hmk->lastTimeExecuted);
        // if timeDiff is very close (-2 ms) to it's interval value, execute it still
        // else the next interval might be too long between the previous one.
        if ((timeDiff + 2) >= hmk->shortcut->autorepeatInterval) {
            hmk->lastTimeExecuted = TTimeRef::get_milliseconds_since_epoch();
            dispatch_shortcut(hmk->shortcut);
        }
    }
}

bool TInputEventDispatcher::is_modifier_keyfact(int keyValue)
{
    return m_modifierKeys.contains(keyValue);
}

void TInputEventDispatcher::finish_hold()
{
    PENTER;
    PMESG("Finishing hold action %s", m_holdingCommand->metaObject()->className());

    m_holdEventCode = NO_HOLD_EVENT;

    clear_hold_modifier_keys();

    if (m_holdingCommand->restoreCursorPosition())
    {
        cpointer().set_canvas_cursor_pos(cpointer().on_first_input_event_scene_pos());
    }

    if (m_cancelHold) {
        PMESG("Canceling this hold command");
        if (m_holdingCommand) {
            m_holdingCommand->cancel_action();
            if (m_moveCommand) {
                m_moveCommand->TMoveCommand::cancel_action();
            }
            delete m_holdingCommand;
            m_holdingCommand = nullptr;
            m_moveCommand = nullptr;
        }
    } else if (m_holdingCommand) {

        int holdFinish = m_holdingCommand->finish_hold();
        if (m_moveCommand) {
            m_moveCommand->TMoveCommand::finish_hold();
        }
        int holdprepare = -1;

        if (holdFinish > 0) {
            holdprepare = m_holdingCommand->prepare_actions();
            if (holdprepare > 0) {
                PMESG("holdingCommand->prepare_actions() returned succes!");
                m_holdingCommand->set_valid(true);
            } else {
                PMESG("holdingCommand->prepare_actions() returned <= 0, so either it failed, or nothing happened!");
                m_holdingCommand->set_valid( false );
            }
        } else {
            PMESG("holdingCommand->finish_hold() returned <= 0, so either it failed, or nothing happened!");
            m_holdingCommand->set_valid( false );
        }

        if (m_holdingCommand->push_to_history_stack() < 0) {
            if (holdprepare == 1) {
                m_holdingCommand->do_action();
            }
            delete m_holdingCommand;
        }

        m_holdingCommand = nullptr;
    }

    reset();
}

void TInputEventDispatcher::clear_hold_modifier_keys()
{
    m_holdKeyRepeatTimer.stop();
    foreach(HoldModifierKey* hmk, m_holdModifierKeys) {
        delete hmk;
    }
    m_holdModifierKeys.clear();
}

bool TInputEventDispatcher::check_number_collection(int eventcode)
{
    if (!m_activeModifierKeys.isEmpty())
    {
        return false;
    }

    if (((eventcode >= Qt::Key_0) && (eventcode <= Qt::Key_9)) ||
            (eventcode == Qt::Key_Comma) || (eventcode == Qt::Key_Period)) {
        set_numerical_input(m_sCollectedNumber + QChar(eventcode));
        PMESG("Collected %s so far...", QS_C(m_sCollectedNumber) ) ;
        return true;
    }
    if (eventcode == Qt::Key_Backspace) {
        if (m_sCollectedNumber.size() > 0) {
            set_numerical_input(m_sCollectedNumber.left(m_sCollectedNumber.size() - 1));
        }
        return true;
    }
    if (eventcode == Qt::Key_Minus) {
        if (m_sCollectedNumber.contains("-")) {
            set_numerical_input(m_sCollectedNumber.remove("-"));
        } else {
            set_numerical_input(m_sCollectedNumber.prepend("-"));
        }
    }
    return false;
}

bool TInputEventDispatcher::has_collected_number()
{
    return !m_sCollectedNumber.isEmpty();
}

void TInputEventDispatcher::set_numerical_input(const QString &number)
{
    m_sCollectedNumber = number;

    if (m_holdingCommand) {
        m_holdingCommand->process_collected_number(m_sCollectedNumber);
    }

    emit collectedNumberChanged();
}

bool TInputEventDispatcher::is_holding( )
{
    return m_isHolding;
}

TCommand * TInputEventDispatcher::get_holding_command() const
{
    return m_holdingCommand;
}

bool TInputEventDispatcher::modifierKeysMatch(QList<int> first, const QList<int>& second)
{
    if (first.size() != second.size())
    {
        return false;
    }

    foreach(int key, first)
    {
        if (!second.contains(key))
        {
            return false;
        }
    }

    return true;
}
