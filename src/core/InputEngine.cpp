/*
Copyright (C) 2005-2008 Remon Sijrier

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

#include "InputEngine.h"

#include "ContextPointer.h"
#include "Information.h"
#include "TCommand.h"
#include <CommandPlugin.h>
#include "Utils.h"
#include "TShortcutManager.h"

#include <QTime>
#include <QFile>
#include <QTextStream>
#include <QDomDocument>
#include <QMetaMethod>
#include <QCoreApplication>
#include <QPluginLoader>
#include <QDir>
#include <QKeyEvent>
#include <QWheelEvent>


// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

/**
 * \class InputEngine
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
	uses a CommandPlugin, the list of loaded CommandPlugins is searched to find a match for the <br />
	plugin name supplied in the keymap file, if there is a match, the Plugin is used to create <br />
	the Command object, and the same routine is used to handle this Command object.

	If the Command object returned no error during the handling, it'll be placed on it's <br />
	historystack. If no historystack was available, it's do_action() will be called, and <br />
	deleted afterwards.


 *	\sa Command, ContextPointer, ViewPort, CommandPlugin
 */


InputEngine& ie()
{
	static InputEngine inputengine;
	return inputengine;
}

InputEngine::InputEngine()
{
	PENTERCONS;
        m_holdingCommand = 0;
	// holdEvenCode MUST be a value != ANY key code!
	// when set to 'not matching any key!!!!!!
        m_holdEventCode = -100;
        m_isJogging = false;
	reset();

        m_collectedNumber = -1;
        m_sCollectedNumber = "";

        connect(&m_holdKeyRepeatTimer, SIGNAL(timeout()), this, SLOT(process_hold_modifier_keys()));


//#define profile

#if defined (profile)
	trav_time_t starttime = get_microseconds();
#endif
	
	foreach (QObject* obj, QPluginLoader::staticInstances()) {
		CommandPlugin* plug = qobject_cast<CommandPlugin*>(obj);
                m_commandplugins.insert(plug->metaObject()->className(), plug);
	}
	
#if !defined (STATIC_BUILD)
	QDir pluginsDir("lib/commandplugins");
	foreach (const QString &fileName, pluginsDir.entryList(QDir::Files)) {
		QPluginLoader loader(pluginsDir.absoluteFilePath(fileName));
		CommandPlugin* plug = qobject_cast<CommandPlugin*>(loader.instance());
		if (plug) {
                        m_commandplugins.insert(plug->metaObject()->className(), plug);
			printf("InputEngine:: Succesfully loaded plugin: %s\n", plug->metaObject()->className());
		} else {
			printf("InputEngine:: Plugin load failed with %s\n", QS_C(loader.errorString()));
		}
	}
	
#endif

#if defined (profile)
	int processtime = (int) (get_microseconds() - starttime);
	printf("InputEngine::Plugin load time: %d useconds\n\n", processtime);
#endif
}

InputEngine::~ InputEngine( )
{
	foreach(TShortcutKey* action, m_ieActions) {
		delete action;
	}
}

int InputEngine::broadcast_action_from_contextmenu(const QString& keySequence)
{
	PENTER2;
	TShortcutKey* action = 0;

	foreach(TShortcutKey* ieaction, m_ieActions) {
		if (ieaction->keyString == keySequence) {
			action = ieaction;
			break;
		}
	}

	if (! action) {
		PERROR("ContextMenu keySequence doesn't apply to any InputEngine knows off!! (%s)", QS_C(keySequence));
		return -1;
	}
	
	return broadcast_action(action, false, true);
}


int InputEngine::broadcast_action(TShortcutKey* action, bool autorepeat, bool fromContextMenu)
{
	PENTER2;

	PMESG("Trying to find IEAction for key sequence %s", QS_C(action->keyString));

        TCommand* k = 0;
	QObject* item = 0;

	QList<QObject* > list;
	 
        if (fromContextMenu) {
                list = cpointer().get_contextmenu_items();
        } else {
                list = cpointer().get_context_items();
	}

	QString slotsignature = "";
	
        if (m_holdingCommand) {
                list.prepend(m_holdingCommand);
	}
	
	for (int i=0; i < list.size(); ++i) {
		k = 0;
		m_broadcastResult = 0;
		
		item = list.at(i);
		
		if (!item) {
			PERROR("no item in cpointer()'s context item list ??");
			continue;
		}
		
		TFunction* data = 0;

                const QMetaObject* metaobject = item->metaObject();
		// traverse upwards till no more superclasses are found
		// this supports inheritance on QObjects.
		while (metaobject)
		{
			QList<TFunction*> dataList = action->objects.values(metaobject->className());

			foreach(TFunction* maybeData, dataList) {
				if (!maybeData) {
					continue;
				}

				if (m_activeModifierKeys.size())
				{
					if (modifierKeysMatch(m_activeModifierKeys, maybeData->modifierkeys)) {
						data = maybeData;
						PMESG("found match in objectUsingModierKeys");
						break;
					} else {
						PMESG("m_activeModifierKeys doesn't contain code %d", action->keyvalue);
					}
				}
				else
				{
					if (maybeData->modifierkeys.isEmpty())
					{
						data = maybeData;
						PMESG("found match in obects NOT using modifier keys");
						break;
					}
				}
			}

			if (data)
			{
				// Now that we found a match, we still have to check if
				// the current mode is valid for this data!
				QString currentmode = m_modes.key(cpointer().get_current_mode());
				QString allmodes = m_modes.key(0);
				if ( data->modes.size() && (! data->modes.contains(currentmode)) && (! data->modes.contains(allmodes))) {
					PMESG("%s on %s is not valid for mode %s", QS_C(action->keyString), item->metaObject()->className(), QS_C(currentmode));
					continue;
				}

				break;
			}

			metaobject = metaobject->superClass();
		}


		if (! data ) {
			PMESG("No data found for object %s", item->metaObject()->className());
			continue;
		}
				
		PMESG("Data found for %s!", metaobject->className());
		PMESG("setting slotsignature to %s", QS_C(data->slotsignature));
		PMESG("setting pluginname to %s", QS_C(data->pluginname));
		PMESG("setting plugincommand to %s", QS_C(data->commandname));
		
		QString pluginname = "", commandname = "";
		slotsignature = data->slotsignature;
		pluginname = data->pluginname;
		commandname = data->commandname;

                if (item == m_holdingCommand) {
			if (QMetaObject::invokeMethod(item, QS_C(slotsignature), Qt::DirectConnection, Q_ARG(bool, autorepeat))) {
                                PMESG("HIT, invoking %s::%s", m_holdingCommand->metaObject()->className(), QS_C(slotsignature));
                                // only now we know which object this hold modifier key was dispatched on.
                                // the process_hold_modifier_keys() only knows about the corresonding ieaction
                                // next time it'll be called, autorepeat interval of the object + keysequence will
                                // be used!
                                action->autorepeatInterval = data->autorepeatInterval;
                                action->autorepeatStartDelay = data->autorepeatStartDelay;
				break;
			}
		}
		
		
		// We first try to find if there is a match in the loaded plugins.
                if ( ! m_holdingCommand ) {
			
			if ( ! pluginname.isEmpty() ) {
				CommandPlugin* plug = m_commandplugins.value(pluginname);
				if (!plug) {
					info().critical(tr("Command Plugin %1 not found!").arg(pluginname));
				} else {
					if ( ! plug->implements(commandname) ) {
						info().critical(tr("Plugin %1 doesn't implement Command %2")
								.arg(pluginname).arg(commandname));
					} else {
						PMESG("InputEngine:: Using plugin %s for command %s",
								QS_C(pluginname), QS_C(data->commandname));
						k = plug->create(item, commandname, data->arguments);
					}
				}
			} 
		}
		
		// Either the plugins didn't have a match, or we are holding.
		if ( ! k ) {
		
			TFunction* delegatingdata;
			QString delegatedobject;
			
                        if (m_holdingCommand) {
				delegatingdata = action->objects.value("HoldCommand");
				delegatedobject = "HoldCommand";
			} else {
                                delegatedobject = metaobject->className();
				if (m_activeModifierKeys.size() > 0) {
					//FIXME: objects has values inserted with insertMulti()
					// do we have to use values(delegatedobject) instead of value(delegatedobject)
					// here too?
					delegatingdata = action->objects.value(delegatedobject);
				} else {
					delegatingdata = action->objects.value(delegatedobject);
				}
				PMESG("delegatedobject is %s", QS_C(delegatedobject));
			}
				
			if ( ! delegatingdata) {
				PMESG("No delegating data ? WEIRD");
				continue;
			}
			
			QStringList strlist = delegatingdata->slotsignature.split("::");
			
			if (strlist.size() == 2) {
				PMESG("Detected delegate action, checking if it is valid!");
				QString classname = strlist.at(0);
				QString slot = strlist.at(1);
				QObject* obj = 0;
				bool validobject = false;
				
				for (int j=0; j < list.size(); ++j) {
					obj = list.at(j);
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
					if (QMetaObject::invokeMethod(obj, QS_C(slot),  Qt::DirectConnection, Q_RETURN_ARG(TCommand*, k))) {
						PMESG("HIT, invoking (delegated) %s::%s", QS_C(classname), QS_C(slot));
					} else {
						PMESG("Delegated object slot call didn't work out, sorry!");
						PMESG("%s::%s() --> %s::%s()", item->metaObject()->className(), QS_C(slot), QS_C(classname), QS_C(slot));
					}
				} else {
					PMESG("Delegated object %s was not found in the context items list!", QS_C(classname));
				}
			} else {
				if (QMetaObject::invokeMethod(item, QS_C(slotsignature), Qt::DirectConnection, Q_RETURN_ARG(TCommand*, k))) {
					PMESG("HIT, invoking %s::%s", item->metaObject()->className(), QS_C(slotsignature));
				} else {
					PMESG("nope %s wasn't the right one, next ...", item->metaObject()->className());
				}
			}
		}
	
		
		// Let's see if the invoked object used either succes(), failure() or did_not_implement()
		// return functions, so we can detect to either return happily, the action was succesfull
		// but no command object needed to be returned, the action was not succesfull, and we 
		// don't want to try lower level context items or the action was succesfull but we'd like 
		// to give a lower level object precedence over the current one.
		if (m_broadcastResult) {
			if (m_broadcastResult == SUCCES) {
				PMESG("Broadcast Result indicates succes, but no returned Command object");
				conclusion();
				return 1;
			}
			if (m_broadcastResult == FAILURE) {
				PMESG("Broadcast Result indicates failure, and doesn't want lower level items to be processed");
				conclusion();
				return 0;
			}
			if (m_broadcastResult == DIDNOTIMPLEMENT) {
				PMESG("Broadcast Result indicates succes, but didn't want to perform it's action,"
						 "so we continue traversing the objects list");
				continue;
			}
		}
		

		if (k) {
			if (k->begin_hold() != -1) {
				k->set_valid(true);
				k->set_cursor_shape(data->useX, data->useY);
                                m_holdingCommand = k;
				m_isHolding = true;
				m_holdEventCode = action->keyvalue;
				set_jogging(true);
			} else {
				PERROR("hold action begin_hold() failed!");
				// OOPSSS, something went wrong when making the Command
				// set following stuff to zero to make finish_hold do nothing
				delete k;
				k = 0;
				set_jogging( false );
			}
		}
		
		break;
	}

	return 1;
}

TCommand* InputEngine::succes()
{
	m_broadcastResult = SUCCES;
	return 0;
}

TCommand* InputEngine::failure()
{
	m_broadcastResult = FAILURE;
	return 0;
}

TCommand* InputEngine::did_not_implement()
{
	m_broadcastResult = DIDNOTIMPLEMENT;
	return 0;
}

void InputEngine::jog()
{
	PENTER3;

        if (m_isJogging) {
                if (m_holdingCommand) {
			if (m_bypassJog) {
				QPoint diff = m_jogBypassPos - cpointer().pos();
				if (diff.manhattanLength() > m_unbypassJogDistance) {
					m_bypassJog = false;
                                        m_holdingCommand->set_jog_bypassed(m_bypassJog);
				} else {
					return;
				}
				m_jogBypassPos = cpointer().pos();
			}
			
                        m_holdingCommand->jog();
		}
	}
}

void InputEngine::bypass_jog_until_mouse_movements_exceeded_manhattenlength(int length)
{
	m_unbypassJogDistance = length;
	m_bypassJog = true;
	m_jogBypassPos = cpointer().pos();
        if (m_holdingCommand) {
                m_holdingCommand->set_jog_bypassed(m_bypassJog);
        }
}

void InputEngine::update_jog_bypass_pos()
{
        m_jogBypassPos = cpointer().pos();
}

void InputEngine::set_jogging(bool jog)
{
        m_isJogging = jog;

        if (m_isJogging) {
                emit jogStarted();
	} else {
                emit jogFinished();
	}
}

bool InputEngine::is_jogging()
{
        return m_isJogging;
}

void InputEngine::reset()
{
	PENTER3;
        m_isHolding = false;
	m_cancelHold = false;
	m_bypassJog = false;
	
        set_numerical_input("");
}

void InputEngine::abort_current_hold_actions()
{
	m_activeModifierKeys.clear();
        clear_hold_modifier_keys();
        // Fake an escape key fact, so if a hold action was
        // running it will be canceled!
        if (is_holding()) {
                process_press_event(Qt::Key_Escape);
        }
}


// Everthing starts here. Catch event takes anything happen in the keyboard
// and pushes it into a stack.
void InputEngine::catch_key_press(QKeyEvent * e )
{
        if (e->isAutoRepeat()) {
		return;
	}
        PENTER3;

        process_press_event(e->key());
}

void InputEngine::catch_key_release( QKeyEvent * e)
{
	if (e->isAutoRepeat()) {
		return;
	}
        PENTER;
	process_release_event(e->key());
}

void InputEngine::catch_mousebutton_press( QMouseEvent * e )
{
        if (e->button() == Qt::LeftButton) {
                cpointer().mouse_button_left_pressed();
        }
	process_press_event(e->button());
}

void InputEngine::catch_mousebutton_release( QMouseEvent * e )
{
	process_release_event(e->button());
}

void InputEngine::catch_mousebutton_doubleclick( QMouseEvent * e )
{
	process_press_event(e->button());
	process_release_event(e->button());
	process_press_event(e->button());
	process_release_event(e->button());
}


void InputEngine::catch_scroll(QWheelEvent* e)
{
	if (e->orientation() == Qt::Horizontal) {
		if (e->delta() > 0) {
		}
		if (e->delta() < 0) {
		}
	} else {
		if (e->delta() > 0) {
			process_press_event(MouseScrollVerticalUp);
			process_release_event(MouseScrollVerticalUp);
		}
		if (e->delta() < 0) {
			process_press_event(MouseScrollVerticalDown);
			process_release_event(MouseScrollVerticalDown);
		}
	}
}

void InputEngine::process_press_event(int eventcode)
{
	if (eventcode == Qt::Key_Escape && is_holding()) {
		m_cancelHold = true;
		finish_hold();
		return;
	}
	
	// first check if this key is just a collected number
	if (check_number_collection(eventcode)) {
		// another digit was collected.
		return;
	}
	
	if (is_modifier_keyfact(eventcode)) {
                if (!m_activeModifierKeys.contains(eventcode)) {
			m_activeModifierKeys.append(eventcode);
		}
		return;
	}
	
        if (m_isHolding) {
		int index = find_index_for_key(eventcode);
		// PRE-CONDITION:
		// The eventcode must be bind to a single key fact AND
		// the eventcode must be != the current active holding 
		// command's eventcode!
                if (index >= 0 && m_holdEventCode != eventcode) {
                        HoldModifierKey* hmk = new HoldModifierKey;
                        hmk->keycode = eventcode;
                        hmk->wasExecuted = false;
                        hmk->lastTimeExecuted = 0;
                        hmk->ieaction = m_ieActions.at(index);
                        m_holdModifierKeys.insert(eventcode, hmk);
                        // execute the first one directly, this is needed
                        // if the release event comes before the timer actually
                        // fires (mouse scroll wheel does press/release events real quick
                        process_hold_modifier_keys();
                        // only start it once
                        if (!m_holdKeyRepeatTimer.isActive()) {
                                m_holdKeyRepeatTimer.start(10);
                        }
		}
		return;
	}

	int fkey_index = find_index_for_key(eventcode);
	if (fkey_index >= 0) {

		cpointer().inputengine_first_input_event();

		TShortcutKey* action = m_ieActions.at(fkey_index);
		broadcast_action(action);
		return;
	}
}

void InputEngine::process_release_event(int eventcode)
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
			PMESG("release event for hold action detected!");
			finish_hold();
		}
	}
}

void InputEngine::process_hold_modifier_keys()
{
        if (!m_holdModifierKeys.size()) {
                m_holdKeyRepeatTimer.stop();
                return;
        }

        foreach(HoldModifierKey* hmk, m_holdModifierKeys) {
                if (!hmk->wasExecuted) {
                        hmk->wasExecuted = true;
                        broadcast_action(hmk->ieaction);
                        hmk->lastTimeExecuted = get_microseconds() + hmk->ieaction->autorepeatStartDelay * 1000;
                        continue;
                }

                int timeDiff = qRound(get_microseconds() - hmk->lastTimeExecuted);
                // if timeDiff is very close (-2 ms) to it's interval value, execute it still
                // else the next interval might be too long between the previous one.
                if ((timeDiff + 2 * 1000) >= hmk->ieaction->autorepeatInterval * 1000) {
                        hmk->lastTimeExecuted = get_microseconds();
                        broadcast_action(hmk->ieaction, true);
                }
        }
}

bool InputEngine::is_modifier_keyfact(int eventcode)
{
	return m_modifierKeys.contains(eventcode);
}


int InputEngine::find_index_for_key(int key)
{
	foreach(TShortcutKey* action, m_ieActions) {
			
		if (action->keyvalue == key)
		{
			PMESG("Found a match keyString %s", QS_C(action->keyString));
			return m_ieActions.indexOf(action);
		}
	}
	
	return -1;
}


void InputEngine::dispatch_action(int mapIndex)
{
	PENTER2;
	broadcast_action(m_ieActions.at(mapIndex));
}

void InputEngine::finish_hold()
{
	PENTER3;
	PMESG("Finishing hold action %s", m_holdingCommand->metaObject()->className());

        m_isHolding = false;
	m_holdEventCode = -100;

        clear_hold_modifier_keys();

	if (m_cancelHold) {
		PMESG("Canceling this hold command");
                if (m_holdingCommand) {
                        m_holdingCommand->cancel_action();
                        delete m_holdingCommand;
                        m_holdingCommand = 0;
		}
		cpointer().reset_cursor();
        } else if (m_holdingCommand) {
		
		cpointer().reset_cursor();
		
                int holdFinish = m_holdingCommand->finish_hold();
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

                m_holdingCommand = 0;
	}
	
	set_jogging(false);
	conclusion();
}

void InputEngine::clear_hold_modifier_keys()
{
        m_holdKeyRepeatTimer.stop();
        foreach(HoldModifierKey* hmk, m_holdModifierKeys) {
                delete hmk;
        }
        m_holdModifierKeys.clear();
}


void InputEngine::conclusion()
{
	PENTER3;
	reset();
}


int InputEngine::init_map(const QString& keymap)
{
	PENTER;
	
	QString filename = ":/keymaps/" + keymap + ".xml";
	if ( ! QFile::exists(filename)) {
		filename = QDir::homePath() + "/.traverso/keymaps/" + keymap + ".xml";
	}
	
	QDomDocument doc("keymap");
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly))
		return -1;
	if (!doc.setContent(&file)) {
		file.close();
		return -1;
	}
	file.close();

	PMESG("Using keymap: %s", QS_C(keymap));
	
	foreach(TShortcutKey* action, m_ieActions) {
		delete action;
	}
	
	m_ieActions.clear();
	m_modifierKeys.clear();
	m_modes.clear();
	
	
	QDomElement root = doc.documentElement();
	
	
	QDomNode modifierKeysNode = root.firstChildElement("ModifierKeys");
	QDomNode modifierKeyNode = modifierKeysNode.firstChild();
	
	int keycode;
	QString key;
	
	while( !modifierKeyNode.isNull() ) {
		QDomElement e = modifierKeyNode.toElement();
		
		key = e.attribute( "key", "");
		
		if (!t_KeyStringToKeyValue(keycode, key)) {
                        info().warning(tr("Input Engine: Loaded keymap has this unrecognized key: %1").arg(key));
                }
		m_modifierKeys.append(keycode);
		
		modifierKeyNode = modifierKeyNode.nextSibling();
	}
		
	
	
	QDomNode modesNode = root.firstChildElement("Modes");
	QDomNode modeNode = modesNode.firstChild();
	
	int id;
	QString modename;
	
	while( !modeNode.isNull() ) {
		QDomElement e = modeNode.toElement();
		
		modename = e.attribute("name", "");
		id = e.attribute("id", "-1").toInt();
		
		m_modes.insert(modename, id);
		
		modeNode = modeNode.nextSibling();
	}
	
	
	
	
	QDomNode keyfactsNode = root.firstChildElement("Keyfacts");
	QDomNode keyfactNode = keyfactsNode.firstChild();
	
	QString mouseHint, modifierKeys;
	TFunction* function;
	
	while( !keyfactNode.isNull() ) {
		QDomElement e = keyfactNode.toElement();
		
		if( e.isNull() ) {
			continue;
		}
			
		if( ! (e.tagName() == "keyfact" ) ) {
			PERROR("Detected wrong tagname, misspelled: keyfact !!");
			continue;
		}
		
		key = e.attribute("key", "");

		TShortcutKey* shortcutKey = tShortCutManager().getShortcutFor(key);

		if (!shortcutKey) {
			continue;
		}

		QDomNode objectNode = e.firstChild();
	
		while(!objectNode.isNull()) {

			QDomElement e = objectNode.toElement();

			QString functionString = e.attribute("function", "");
			if (!functionString.isEmpty())
			{
				function = tShortCutManager().getFunctionshortcut(functionString);
				shortcutKey->objects.insert(function->classname, function);
				objectNode = objectNode.nextSibling();
				continue;
			}
			
			function = new TFunction;

			QString objectname = e.attribute("objectname", "");
			function->slotsignature = e.attribute("slotsignature", "");
			function->modes = e.attribute("modes", "All").split(";");
			function->pluginname = e.attribute( "pluginname", "");
			function->commandname = e.attribute( "commandname", "");
			function->submenu = e.attribute("submenu", "");
			function->sortorder = e.attribute( "sortorder", "0").toInt();
			function->autorepeatInterval = e.attribute("autorepeatinterval", "40").toInt();
			function->autorepeatStartDelay = e.attribute("autorepeatstartdelay", "100").toInt();
			mouseHint = e.attribute( "mousehint", "" );
			QString args = e.attribute("arguments", "");
			modifierKeys = e.attribute("modifierkeys", "");
			
			if ( ! args.isEmpty() ) {
				QStringList arglist = args.split(";");
				for (int i=0; i<arglist.size(); ++i) {
					function->arguments.append(arglist.at(i));
				}
			}
			
			if (! modifierKeys.isEmpty()) {
				QStringList modifierlist = modifierKeys.split(";");
				for (int i=0; i<modifierlist.size(); ++i) {
					int keycode;
					t_KeyStringToKeyValue(keycode, modifierlist.at(i));
					function->modifierkeys.append(keycode);
				}
			}
			
			if (mouseHint == "LR") {
				function->useX = true;
			}
			if (mouseHint == "UD") {
				function->useY = true;
			}
			if (mouseHint == "LRUD") {
				function->useX = function->useY = true;
			}
			
			if (QString(objectname) == "") {
				PERROR("no objectname given in keyaction %s", QS_C(key));
			}
			if (function->slotsignature.isEmpty() && function->pluginname.isEmpty()) {
				PERROR("no slotsignature given in keyaction %s, object %s", QS_C(key), QS_C(objectname));
			}
			if (QString(function->modes.join(";")) == "") {
				PERROR("no modes given in keyaction %s, object %s", QS_C(key), QS_C(objectname));
			}
	
			shortcutKey->objects.insertMulti(objectname, function);
		
			PMESG3("ADDED action: type=%d key=%d useX=%d useY=%d, slot=%s", shortcutKey->type, shortcutKey->keyvalue, function->useX, function->useY, QS_C(function->slotsignature));

                        objectNode = objectNode.nextSibling();
		}
		
		shortcutKey->keyString = key;

		m_ieActions.append(shortcutKey);
		
		keyfactNode = keyfactNode.nextSibling();
	}

	return 1;
}

QStringList InputEngine::keyfacts_for_hold_command(const QString& className)
{
        QStringList result;

        for (int i=0; i<m_ieActions.size(); i++) {
		TShortcutKey* ieaction = m_ieActions.at(i);

		foreach(TFunction* data, ieaction->objects) {
			if (data->commandname == className) {
				QString keyfact = ieaction->keyString;
				TShortcutManager::makeShortcutKeyHumanReadable(keyfact);
				result.append(keyfact);
			}
		}
        }
        result.removeDuplicates();

        return result;
}


void InputEngine::filter_unknown_sequence(QString& sequence)
{
	sequence.replace(tr("Up Arrow"), "Up");
	sequence.replace(tr("Down Arrow"), "Down");
	sequence.replace(tr("Left Arrow"), "Left");
	sequence.replace(tr("Right Arrow"), "Right");
//	sequence.replace(tr("Delete", "Delete"));
	sequence.replace(QString("-"), "Minus");
	sequence.replace(QString("+"), "Plus");
//	sequence.replace(tr("Page Down"));
//	sequence.replace(tr("Page Up"));
}

void InputEngine::create_menu_translations()
{
        foreach(CommandPlugin* plug, m_commandplugins) {
                plug->create_menu_translations();
        }
}


// Number colector
bool InputEngine::check_number_collection(int eventcode)
{
	if (((eventcode >= Qt::Key_0) && (eventcode <= Qt::Key_9)) || 
	     (eventcode == Qt::Key_Comma) || (eventcode == Qt::Key_Period)) {
                // it had a ",1" complement after fact1_k1... why?
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

void InputEngine::stop_collecting()
{
	PENTER3;
        bool ok;
        m_collectedNumber = m_sCollectedNumber.toInt(&ok);
        if (!ok) {
                m_collectedNumber = -1;
        }
        set_numerical_input("");
}

int InputEngine::collected_number( )
{
        int n = m_collectedNumber;
        set_numerical_input("");
        return n;
}

bool InputEngine::has_collected_number()
{
        if (m_sCollectedNumber.isEmpty()) {
                return false;
        }

        return true;
}

void InputEngine::set_numerical_input(const QString &number)
{
        m_sCollectedNumber = number;
        bool ok;
        m_collectedNumber = m_sCollectedNumber.toInt(&ok);
        if (!ok) {
                m_collectedNumber = -1;
        }

        if (m_holdingCommand) {
                m_holdingCommand->set_collected_number(m_sCollectedNumber);
        }
        emit collectedNumberChanged();
}

bool InputEngine::is_holding( )
{
        return m_isHolding;
}

TCommand * InputEngine::get_holding_command() const
{
        return m_holdingCommand;
}

TShortcutKey::~ TShortcutKey()
{
	foreach(TFunction* data, objects) {
		delete data;
	}
}

bool InputEngine::modifierKeysMatch(QList<int> first, QList<int> second)
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
