/*
Copyright (C) 2011 Remon Sijrier

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

#include "TShortcutManager.h"

#include <QPluginLoader>
#include <QSettings>
#include <QApplication>
#include <QMap>
#include <QPalette>
#include <QTextDocument>
#include <QDir>
#include <QTextStream>

#include "PCommand.h"
#include "ContextItem.h"
#include "Information.h"
#include "Utils.h"
#include "CommandPlugin.h"
#include "TConfig.h"

#include "Debugger.h"

QList<TFunction*> TShortcut::getFunctionsForObject(const QString &objectName)
{
	return objects.values(objectName);
}

QList<TFunction*> TShortcut::getFunctions()
{
    return objects.values();
}

QString TFunction::getModifierSequence(bool fromInheritedBase)
{
	QString modifiersString;

	foreach(int modifier, getModifierKeys(fromInheritedBase)) {
		if (modifier == Qt::Key_Alt) {
			modifiersString += "Alt+";
		} else if (modifier == Qt::Key_Control) {
			modifiersString += "Ctrl+";
		} else if (modifier == Qt::Key_Shift) {
			modifiersString += "Shift+";
		} else if (modifier == Qt::Key_Meta) {
			modifiersString += "Meta+";
		}
	}
	return modifiersString;
}

QString TFunction::getKeySequence()
{
	QString sequence;
	QStringList sequenceList;
	QString modifiersString = getModifierSequence();

	if (getModifierKeys().size())
	{
		modifiersString += " ";
	}

	foreach(QString keyString, getKeys())
	{

		sequenceList << (modifiersString + keyString);
	}

    sequence = sequenceList.join(" , ");

	TShortcutManager::makeShortcutKeyHumanReadable(sequence);

	return sequence;
}

QList<int> TFunction::getModifierKeys(bool fromInheritedBase)
{
	if (m_inheritedFunction && m_usesInheritedBase && fromInheritedBase)
	{
		return m_inheritedFunction->getModifierKeys();
	}

	return m_modifierkeys;
}

QString TFunction::getSlotSignature() const
{
    // a slotsignature is only set for hold commands, not for modifier keys
    // if the shortcut is from a modifier key then the slotsignature will be
    // empty in which case we do return the slotsignature set for the TFunction
    if (m_inheritedFunction && !m_inheritedFunction->getSlotSignature().isEmpty())
	{
		return m_inheritedFunction->getSlotSignature();
	}

	return slotsignature;
}

QString TFunction::getDescription() const
{
	if (!m_description.isEmpty())
	{
		return m_description;
	}

	if (m_inheritedFunction)
	{
		return m_inheritedFunction->getDescription();
	}

	return m_description;
}

QString TFunction::getLongDescription() const
{
	QString description = getDescription();
	if (!submenu.isEmpty())
	{
		description = submenu + " : " + description;
	}
	return description;
}

void TFunction::setDescription(const QString& description)
{
	m_description = description;
}

void TFunction::setInheritedBase(const QString &base)
{
	m_inheritedBase = base;
}

QStringList TFunction::getKeys(bool fromInheritedBase) const
{
	if (m_inheritedFunction && m_usesInheritedBase && fromInheritedBase)
	{
		return m_inheritedFunction->getKeys();
	}

	return m_keys;
}

QStringList TFunction::getObjects() const
{
	return object.split("::", QString::SkipEmptyParts);
}

QString TFunction::getObject() const
{
	return getObjects().first();
}

void TFunction::setInheritedFunction(TFunction *inherited)
{
	m_inheritedFunction = inherited;
}

int TFunction::getAutoRepeatInterval() const
{
	if (m_inheritedFunction && m_usesInheritedBase)
	{
		return m_inheritedFunction->getAutoRepeatInterval();
	}

	return m_autorepeatInterval;
}

int TFunction::getAutoRepeatStartDelay() const
{
	if (m_inheritedFunction && m_usesInheritedBase)
	{
		return m_inheritedFunction->getAutoRepeatStartDelay();
	}

	return m_autorepeatStartDelay;
}

void TShortcutManager::makeShortcutKeyHumanReadable(QString& keyfact)
{
    keyfact.replace(QString("MOUSESCROLLVERTICALUP"), tr("Scroll Up"));
	keyfact.replace(QString("MOUSESCROLLVERTICALDOWN"), tr("Scroll Down"));
	keyfact.replace(QString("MOUSEBUTTONRIGHT"), tr("Right Button"));
	keyfact.replace(QString("MOUSEBUTTONLEFT"), tr("Left Button"));
	keyfact.replace(QString("MOUSEBUTTONMIDDLE"), tr("Center Button"));
    keyfact.replace(QString("UPARROW"), tr("Up"));
    keyfact.replace(QString("DOWNARROW"), tr("Down"));
    keyfact.replace(QString("LEFTARROW"), tr("Left"));
    keyfact.replace(QString("RIGHTARROW"), tr("Right"));
	keyfact.replace(QString("DELETE"), "Delete");
	keyfact.replace(QString("MINUS"), "-");
	keyfact.replace(QString("PLUS"), "+");
	keyfact.replace(QString("PAGEDOWN"), "Page Down");
	keyfact.replace(QString("PAGEUP"), "Page Up");
	keyfact.replace(QString("ESC"), "Esc");
	keyfact.replace(QString("ENTER"), "Enter");
	keyfact.replace(QString("RETURN"), "Return");
	keyfact.replace(QString("SPACE"), tr("Space Bar"));
	keyfact.replace(QString("HOME"), "Home");
	keyfact.replace(QString("END"), "End");
	keyfact.replace(QString("NUMERICAL"), tr("0, 1, ... 9"));
}


TShortcutManager& tShortCutManager()
{
	static TShortcutManager manager;
	return manager;
}

TShortcutManager::TShortcutManager()
{
	cpointer().add_contextitem(this);
}

void TShortcutManager::registerFunction(TFunction *function)
{
	if (m_functions.contains(function->commandName))
	{
		printf("There is already a function registered with command name %s\n", QS_C(function->commandName));
		return;
	}

	m_functions.insert(function->commandName, function);
}

TFunction* TShortcutManager::getFunction(const QString &functionName) const
{
    TFunction* function = m_functions.value(functionName, nullptr);
	if (!function)
	{
		printf("TShortcutManager::getFunction: Function %s not in database!!\n", functionName.toLatin1().data());
	}

	return function;
}

QList< TFunction* > TShortcutManager::getFunctionsFor(QString className)
{
	QList<TFunction* > functionsList;
	QStringList classes = m_classes.value(className.remove("View"));
	foreach(QString object, classes)
	{
		foreach(TFunction* function, m_functions)
		{
			// filter out objects that inherit from MoveCommand
			// but do not support move up/down
			bool hasRequiredSlot = true;
			if (!function->slotsignature.isEmpty())
			{
				QList<const QMetaObject*> metaList = m_metaObjects.value(className);
				if (metaList.size())
				{
					if (function->slotsignature == "move_up" && metaList.first()->indexOfMethod("move_up(bool)") == -1) {
						hasRequiredSlot = false;
					}
					if (function->slotsignature == "move_down" && metaList.first()->indexOfMethod("move_down(bool)") == -1) {
						hasRequiredSlot = false;
					}
				}
			}

			if (function->getObject() == object && hasRequiredSlot)
			{
				functionsList.append(function);
			}
		}
	}

	qSort(functionsList.begin(), functionsList.end(), TFunction::smaller);
    return functionsList;
}

TShortcut* TShortcutManager::getShortcutForKey(const QString &keyString)
{
	int keyValue = -1;

	if (!t_KeyStringToKeyValue(keyValue, keyString)) {
	       info().warning(tr("Shortcut Manager: Loaded keymap has this unrecognized key: %1").arg(keyString));
           return nullptr;
	}

    TShortcut* shortcut = m_shortcuts.value(keyValue, nullptr);

	if (!shortcut)
	{
		shortcut = new TShortcut(keyValue);
		m_shortcuts.insert(keyValue, shortcut);
	}

	return shortcut;
}

TShortcut* TShortcutManager::getShortcutForKey(int key)
{
    TShortcut* shortcut = m_shortcuts.value(key, nullptr);
	return shortcut;
}

CommandPlugin* TShortcutManager::getCommandPlugin(const QString &pluginName)
{
	return m_commandPlugins.value(pluginName);
}

void TShortcutManager::register_command_plugin(CommandPlugin *plugin, const QString &pluginName)
{
    m_commandPlugins.insert(pluginName, plugin);
}

bool TShortcutManager::isCommandClass(const QString &className)
{
	QList<const QMetaObject*> list = m_metaObjects.value(className);

	// A Command class only has one metaobject, compared to a
	// core + its view item which equals 2 metaobjects for just one 'object'
	if (list.size() == 1)
	{
		return classInherits(list.at(0)->className(), "TCommand");
	}

	return false;
}

QList<QString> TShortcutManager::getClassNames() const
{
    return m_classes.keys();
}

QString TShortcutManager::getClassForObject(const QString &object) const
{
	QStringList keys = m_classes.keys();
	foreach(QString key, keys)
	{
		QStringList objects = m_classes.value(key);
		if (objects.contains(object))
		{
			return key;
		}
	}
	return "";
}

void TShortcutManager::loadFunctions()
{
//	foreach (QObject* obj, QPluginLoader::staticInstances()) {
//		CommandPlugin* plug = qobject_cast<CommandPlugin*>(obj);
//		if (plug)
//		{
//			m_commandPlugins.insert(plug->metaObject()->className(), plug);
//		}
//	}

    add_translation("ToggleBypassBase", tr("Toggle Bypass"));
    m_classes.insert("ToggleBypassBase", QStringList() << "ToggleBypassBase");

    add_translation("ToggleVerticalBase", tr("Toggle Vertical"));
    m_classes.insert("ToggleVerticalBase", QStringList() << "ToggleVerticalBase");

	add_translation("GainBase", tr("Gain"));
	m_classes.insert("GainBase", QStringList() << "GainBase");

	add_translation("DeleteBase", tr("Remove"));
	m_classes.insert("DeleteBase", QStringList() << "DeleteBase");


	add_translation("MoveBase", tr("Move"));
	m_classes.insert("MoveBase", QStringList() << "MoveBase");

	add_translation("EditPropertiesBase", tr("Edit Properties"));
	m_classes.insert("EditPropertiesBase", QStringList() << "EditPropertiesBase");


	TFunction* function;

    function = new TFunction();
    function->object = "ToggleVerticalBase";
    function->setDescription(tr("Toggle Vertical"));
    function->commandName = "ToggleVerticalBase";
    registerFunction(function);

	function = new TFunction();
	function->object = "GainBase";
	function->setDescription(tr("Gain"));
	function->commandName = "GainBase";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveBase";
	function->m_description = tr("Move");
	function->commandName = "MoveBase";
    registerFunction(function);

	function = new TFunction();
	function->object = "DeleteBase";
	function->m_description = tr("Remove");
	function->commandName = "DeleteBase";
    registerFunction(function);

	function = new TFunction();
    function->object = "ToggleBypassBase";
    function->slotsignature = "toggle_bypass";
    function->setDescription(tr("Toggle Bypass"));
    function->commandName = "ToggleBypassBase";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveCommand";
	function->slotsignature = "toggle_snap_on_off";
	function->setDescription(tr("Toggle Snap on/off"));
	function->commandName = "MoveCommandToggleSnap";
    registerFunction(function);

	function = new TFunction();
	function->object = "EditPropertiesBase";
	function->slotsignature = "edit_properties";
	function->setDescription(tr("Edit Properties"));
	function->commandName = "EditPropertiesBase";
    registerFunction(function);

    function = new TFunction();
	function->object = "AudioTrack";
	function->slotsignature = "toggle_arm";
	function->m_description = tr("Record: On/Off");
	function->commandName = "AudioTrackToggleRecord";
    registerFunction(function);

	function = new TFunction();
	function->object = "AudioTrack";
	function->slotsignature = "silence_others";
	function->m_description = tr("Silence other tracks");
	function->commandName = "AudioTrackSilenceOthers";
    registerFunction(function);

	function = new TFunction();
	function->object = "FadeCurve";
	function->slotsignature = "set_mode";
	function->m_description = tr("Cycle Shape");
	function->commandName = "FadeCurveCycleShape";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveCommand";
	function->slotsignature = "move_right";
	function->m_description = tr("Move Right");
	function->commandName = "MoveCommandRight";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveCommand";
	function->slotsignature = "move_left";
	function->m_description = tr("Move Left");
	function->commandName = "MoveCommandLeft";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveCommand";
	function->slotsignature = "move_up";
	function->m_description = tr("Move Up");
	function->commandName = "MoveCommandUp";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveCommand";
	function->slotsignature = "move_down";
	function->m_description = tr("Move Down");
	function->commandName = "MoveCommandDown";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveCommand";
	function->slotsignature = "move_faster";
	function->m_description = tr("Move Faster");
	function->commandName = "MoveCommandFaster";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveCommand";
	function->slotsignature = "move_slower";
	function->m_description = tr("Move Slower");
	function->commandName = "MoveCommandSlower";
    registerFunction(function);

	function = new TFunction();
	function->object = "TMainWindow";
	function->slotsignature = "quick_start";
	function->m_description = tr("Show Help");
	function->commandName = "ShowHelp";
    registerFunction(function);

	function = new TFunction();
	function->object = "TMainWindow";
	function->slotsignature = "full_screen";
	function->m_description = tr("Full Screen");
	function->commandName = "MainWindowShowFullScreen";
    registerFunction(function);

	function = new TFunction();
	function->object = "TShortcutManager";
	function->slotsignature = "export_keymap";
	function->m_description = tr("Export keymap");
	function->commandName = "ExportShortcutMap";
    registerFunction(function);

	function = new TFunction();
	function->object = "TrackView";
	function->slotsignature = "add_new_plugin";
	function->m_description = tr("Add new Plugin");
	function->commandName = "TrackAddPlugin";
    registerFunction(function);

	function = new TFunction();
	function->object = "TPanKnobView";
	function->slotsignature = "pan_left";
	function->m_description = tr("Pan to Left");
	function->commandName = "PanKnobPanLeft";
    registerFunction(function);

	function = new TFunction();
	function->object = "TPanKnobView";
	function->slotsignature = "pan_right";
	function->m_description = tr("Pan to Right");
	function->commandName = "PanKnobPanRight";
    registerFunction(function);

	function = new TFunction();
	function->object = "Zoom";
	function->slotsignature = "hzoom_out";
	function->setDescription(tr("Out"));
	function->commandName = "ZoomOut";
    registerFunction(function);

	function = new TFunction();
	function->object = "Zoom";
	function->slotsignature = "hzoom_in";
	function->setDescription(tr("In"));
	function->commandName = "ZoomIn";
    registerFunction(function);

	function = new TFunction();
	function->object = "TrackPan";
	function->slotsignature = "pan_left";
	function->m_description = tr("Pan to Left");
	function->commandName = "TrackPanLeft";
    registerFunction(function);

	function = new TFunction();
	function->object = "TrackPan";
	function->slotsignature = "pan_right";
	function->m_description = tr("Pan to Right");
	function->commandName = "TrackPanRight";
    registerFunction(function);

	function = new TFunction();
	function->object = "Gain";
	function->slotsignature = "increase_gain";
	function->m_description = tr("Increase");
	function->commandName = "GainIncrease";
    registerFunction(function);

	function = new TFunction();
	function->object = "Gain";
	function->slotsignature = "decrease_gain";
	function->m_description = tr("Decrease");
	function->commandName = "GainDecrease";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveTrack";
	function->slotsignature = "move_up";
	function->m_description = tr("Move Up");
	function->commandName = "MoveTrackUp";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveTrack";
	function->slotsignature = "move_down";
	function->m_description = tr("Move Down");
	function->commandName = "MoveTrackDown";
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "scroll_up";
	function->m_description =tr("Up");
	function->commandName = "ViewScrollUp";
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "scroll_down";
	function->m_description = tr("Down");
	function->commandName = "ViewScrollDown";
    registerFunction(function);

	function = new TFunction();
	function->object = "Zoom";
	function->slotsignature = "track_vzoom_out";
	function->m_description = tr("Track Vertical Zoom Out");
	function->commandName = "ZoomTrackVerticalOut";
    registerFunction(function);

	function = new TFunction();
	function->object = "Zoom";
	function->slotsignature = "track_vzoom_in";
	function->m_description = tr("Track Vertical Zoom In");
	function->commandName = "ZoomTrackVerticalIn";
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "to_upper_context_level";
	function->m_description = tr("One Layer Up");
	function->commandName = "NavigateToUpperContext";
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "to_lower_context_level";
	function->m_description = tr("One Layer Down");
	function->commandName = "NavigateToLowerContext";
    registerFunction(function);

	function = new TFunction();
	function->object = "AudioClipView";
	function->slotsignature = "fade_range";
	function->m_description = tr("Adjust Length");
	function->commandName = "AudioClipFadeLength";
    registerFunction(function);

	function = new TFunction();
	function->object = "TTransport";
	function->slotsignature = "start_transport";
	function->m_description = tr("Play (Start/Stop)");
	function->commandName = "TransportPlayStartStop";
    registerFunction(function);

	function = new TFunction();
	function->object = "TTransport";
	function->slotsignature = "set_recordable_and_start_transport";
	function->m_description = tr("Start Recording");
	function->commandName = "TransportSetRecordingPlayStart";
    registerFunction(function);

	function = new TFunction();
	function->object = "TTransport";
	function->slotsignature = "to_start";
	function->setDescription(tr("To start"));
	function->commandName = "TransportToStart";
    registerFunction(function);

	function = new TFunction();
	function->object = "TTransport";
	function->slotsignature = "to_end";
	function->setDescription(tr("To end"));
	function->commandName = "TransportToEnd";
    registerFunction(function);

	function = new TFunction();
	function->object = "TTransport";
	function->setSlotSignature("set_transport_position");
	function->setDescription(tr("Set Play Position"));
	function->commandName = "TransportSetPosition";
	function->useX = true;
    registerFunction(function);

	function = new TFunction();
	function->object = "TrackView";
	function->setInheritedBase("EditPropertiesBase");
	function->commandName = "EditTrackProperties";
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->setInheritedBase("EditPropertiesBase");
	function->commandName = "EditSongProperties";
    registerFunction(function);

	function = new TFunction();
	function->object = "PluginView";
	function->setInheritedBase("EditPropertiesBase");
	function->commandName = "EditPluginProperties";
    registerFunction(function);

	function = new TFunction();
	function->object = "SpectralMeterView";
	function->setInheritedBase("EditPropertiesBase");
	function->commandName = "EditSpectralMeterProperties";
    registerFunction(function);

	function = new TFunction();
	function->object = "AudioClipView";
	function->setInheritedBase("EditPropertiesBase");
	function->commandName = "EditAudioClipProperties";
    registerFunction(function);

	function = new TFunction();
	function->object = "Zoom";
	function->slotsignature = "toggle_expand_all_tracks";
	function->m_description = tr("Expand/Collapse Tracks");
	function->commandName = "ZoomToggleExpandAllTracks";
    registerFunction(function);

	function = new TFunction();
	function->object = "TrackPanelLed";
	function->slotsignature = "toggle";
	function->m_description = tr("Toggle On/Off");
	function->commandName = "PanelLedToggle";
    registerFunction(function);

	function = new TFunction();
	function->object = "CurveView";
	function->slotsignature = "add_node";
	function->m_description = tr("New Node");
	function->commandName = "AddCurveNode";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveCommand";
	function->slotsignature = "numerical_input";
	function->m_description = tr("Moving Speed");
	function->commandName = "MoveCommandSpeed";
    registerFunction(function);

	function = new TFunction();
	function->object = "TCommand";
	function->m_description = tr("Reject");
	function->commandName = "RejectHoldCommand";
    registerFunction(function);

	function = new TFunction();
	function->object = "TCommand";
	function->m_description = tr("Accept");
	function->commandName = "AcceptHoldCommand";
    registerFunction(function);

	function = new TFunction();
	function->object = "Gain";
	function->slotsignature = "numerical_input";
	function->m_description = tr("Input dB value");
	function->commandName = "GainNumericalInput";
    registerFunction(function);

	function = new TFunction();
	function->object = "Zoom";
	function->slotsignature = "numerical_input";
	function->m_description = tr("Track Height");
	function->commandName = "ZoomNumericalInput";
    registerFunction(function);

	function = new TFunction();
	function->object = "FadeCurveView";
	function->slotsignature = "select_fade_shape";
	function->m_description = tr("Select Preset");
	function->commandName = "FadeSelectPreset";
    registerFunction(function);

	function = new TFunction();
	function->object = "TMainWindow";
	function->slotsignature = "show_newtrack_dialog";
	function->m_description = tr("New Track Dialog");
	function->commandName = "ShowNewTrackDialog";
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "browse_to_time_line";
	function->m_description = tr("To Timeline");
	function->commandName = "NavigateToTimeLine";
    registerFunction(function);

	function = new TFunction();
	function->object = "TrackPanelGain";
	function->slotsignature = "gain_decrement";
	function->m_description = tr("Decrease");
	function->commandName = "TrackPanelGainDecrement";
    registerFunction(function);

	function = new TFunction();
	function->object = "TrackPanelGain";
	function->slotsignature = "gain_increment";
	function->m_description = tr("Increase");
	function->commandName = "TrackPanelGainIncrement";
    registerFunction(function);

	function = new TFunction();
	function->object = "CropClip";
	function->slotsignature = "adjust_left";
	function->m_description = tr("Adjust Left");
    function->commandName = "CropClipAdjustLeft";
    registerFunction(function);

	function = new TFunction();
	function->object = "CropClip";
	function->slotsignature = "adjust_right";
	function->m_description = tr("Adjust Right");
	function->commandName = "CropClipAdjustRight";
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "touch";
	function->m_description = tr("Set");
	function->commandName = "WorkCursorTouch";
    registerFunction(function);

	function = new TFunction();
	function->object = "TimeLineView";
	function->slotsignature = "playhead_to_marker";
	function->m_description = tr("Playhead to Marker");
	function->commandName = "TimeLinePlayheadToMarker";
    registerFunction(function);

	function = new TFunction();
	function->object = "AudioClipView";
	function->slotsignature = "set_audio_file";
	function->m_description = tr("Reset Audio File");
	function->commandName = "AudioClipSetAudioFile";
    registerFunction(function);

	function = new TFunction();
	function->object = "FadeCurve";
	function->slotsignature = "toggle_raster";
	function->m_description = tr("Toggle Raster");
	function->commandName = "FadeCurveToggleRaster";
    registerFunction(function);

	function = new TFunction();
	function->object = "TrackPan";
	function->slotsignature = "reset_pan";
	function->m_description = tr("Reset");
	function->commandName = "TrackPanReset";
    registerFunction(function);

	function = new TFunction();
	function->object = "SpectralMeterView";
	function->slotsignature = "reset";
	function->m_description = tr("Reset average curve");
	function->commandName = "SpectralMeterResetAverageCurve";
    registerFunction(function);

	function = new TFunction();
	function->object = "AudioClip";
	function->slotsignature = "lock";
	function->m_description = tr("Lock");
	function->commandName = "AudioClipLock";
    registerFunction(function);

	function = new TFunction();
	function->object = "CorrelationMeterView";
	function->slotsignature = "set_mode";
	function->m_description = tr("Toggle display range");
	function->commandName = "CorrelationMeterToggleDisplayRange";
    registerFunction(function);

	function = new TFunction();
	function->object = "SpectralMeterView";
	function->slotsignature = "set_mode";
	function->m_description = tr("Toggle average curve");
	function->commandName = "SpectralMeterToggleDisplayRange";
    registerFunction(function);

	function = new TFunction();
	function->object = "TimeLineView";
	function->slotsignature = "add_marker";
	function->m_description = tr("Add Marker");
	function->commandName = "TimeLineAddMarker";
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "add_marker";
	function->m_description = tr("Add Marker");
	function->commandName = "SheetAddMarker";
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "add_marker_at_playhead";
	function->m_description = tr("Add Marker at Playhead");
	function->commandName = "SheetAddMarkerAtPlayhead";
    registerFunction(function);

	function = new TFunction();
	function->object = "TimeLineView";
	function->slotsignature = "add_marker_at_playhead";
	function->m_description = tr("Add Marker at Playhead");
	function->commandName = "TimeLineAddMarkerAtPlayhead";
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "add_marker_at_work_cursor";
	function->m_description = tr("Add Marker at Work Cursor");
	function->commandName = "SheetAddMarkerAtWorkCursor";
    registerFunction(function);

	function = new TFunction();
	function->object = "TimeLineView";
	function->slotsignature = "add_marker_at_work_cursor";
	function->m_description = tr("Add Marker at Work Cursor");
	function->commandName = "TimeLineAddMarkerAtWorkCursor";
    registerFunction(function);

	function = new TFunction();
	function->object = "FadeCurve";
    function->setInheritedBase("ToggleBypassBase");
	function->commandName = "FadeCurveToggleBypass";
    registerFunction(function);

	function = new TFunction();
	function->object = "Plugin";
    function->setInheritedBase("ToggleBypassBase");
	function->commandName = "PluginToggleBypass";
    registerFunction(function);

	function = new TFunction();
	function->object = "FadeCurveView";
	function->slotsignature = "bend";
	function->setDescription(tr("Adjust Bend"));
	function->useY = true;
	function->commandName = "FadeCurveBend";
    registerFunction(function);

	function = new TFunction();
	function->object = "TimeLineView";
	function->slotsignature = "TMainWindow::show_marker_dialog";
	function->setDescription(tr("Edit Markers"));
	function->commandName = "TimeLineShowMarkerDialog";
    registerFunction(function);

	function = new TFunction();
	function->object = "TMainWindow";
	function->slotsignature = "show_context_menu";
	function->setDescription(tr("Context Menu"));
	function->commandName = "ShowContextMenu";
    registerFunction(function);

	function = new TFunction();
	function->object = "HoldCommand";
	function->slotsignature = "TMainWindow::show_context_menu";
	function->setDescription(tr("Context Menu"));
	function->commandName = "HoldCommandShowContextMenu";
    registerFunction(function);

	function = new TFunction();
	function->object = "TMainWindow";
	function->slotsignature = "show_project_manager_dialog";
	function->setDescription(tr("Show Project Management Dialog"));
	function->commandName = "MainWindowShowProjectManagementDialog";
    registerFunction(function);

	function = new TFunction();
	function->object = "ProcessingData";
	function->slotsignature = "mute";
	function->setDescription(tr("Mute"));
	function->commandName = "Mute";
    registerFunction(function);

	function = new TFunction();
	function->object = "Track";
	function->slotsignature = "solo";
	function->setDescription(tr("Solo"));
	function->commandName = "Solo";
    registerFunction(function);

	function = new TFunction();
	function->object = "CurveView";
	function->slotsignature = "toggle_select_all_nodes";
	function->setDescription(tr("Select All Nodes"));
	function->commandName = "CurveSelectAllNodes";
    registerFunction(function);

	function = new TFunction();
	function->object = "ProjectManager";
	function->slotsignature = "save_project";
	function->setDescription(tr("Save Project"));
	function->commandName = "ProjectSave";
    registerFunction(function);

	function = new TFunction();
	function->object = "CurveView";
	function->slotsignature = "select_lazy_selected_node";
	function->setDescription(tr("Select Node"));
	function->commandName = "CurveSelectNode";
    registerFunction(function);

	function = new TFunction();
	function->object = "FadeCurveView";
	function->slotsignature = "strength";
	function->setDescription(tr("Adjust Strength"));
	function->commandName = "FadeCurveStrenght";
	function->useX = true;
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "goto_end";
	function->setDescription(tr("To end"));
	function->commandName = "WorkCursorToEnd";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveTrack";
	function->slotsignature = "to_bottom";
	function->setDescription(tr("To Bottom"));
	function->commandName = "MoveTrackToBottom";
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "goto_begin";
	function->setDescription(tr("To start"));
	function->commandName = "WorkCursorToStart";
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveTrack";
	function->slotsignature = "to_top";
	function->setDescription(tr("To Top"));
	function->commandName = "MoveTrackToTop";
    registerFunction(function);

	function = new TFunction();
	function->object = "PlayHeadMove";
	function->slotsignature = "move_to_work_cursor";
	function->setDescription(tr("To Work Cursor"));
	function->commandName = "PlayHeadMoveToWorkCursor";
    registerFunction(function);

    function = new TFunction();
    function->object = "PlayHeadMove";
    function->slotsignature = "move_to_start";
    function->setDescription(tr("To Start"));
    function->commandName = "PlayHeadMoveToStart";
    registerFunction(function);

    function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "touch_play_cursor";
	function->setDescription(tr("Set"));
	function->commandName = "SheetSetPlayPosition";
    registerFunction(function);

	function = new TFunction();
	function->object = "SheetView";
	function->slotsignature = "center_playhead";
	function->setDescription(tr("Center"));
	function->commandName = "SheetCenterPlayhead";
    registerFunction(function);

	function = new TFunction();
	function->object = "Zoom";
	function->slotsignature = "toggle_vertical_horizontal_jog_zoom";
	function->setDescription(tr("Toggle Vertical / Horizontal"));
	function->commandName = "ZoomToggleVerticalHorizontal";
    function->setInheritedBase("ToggleVerticalBase");
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveClip";
	function->slotsignature = "toggle_vertical_only";
	function->setDescription(tr("Toggle Vertical Only"));
	function->commandName = "MoveClipToggleVerticalOnly";
    function->setInheritedBase("ToggleVerticalBase");
    registerFunction(function);

	function = new TFunction();
	function->object = "MoveCurveNode";
	function->slotsignature = "toggle_vertical_only";
	function->setDescription(tr("Toggle Vertical Only"));
	function->commandName = "MoveCurveNodeToggleVerticalOnly";
    function->setInheritedBase("ToggleVerticalBase");
    registerFunction(function);

	function = new TFunction();
	function->object = "WorkCursorMove";
	function->slotsignature = "move_to_play_cursor";
	function->setDescription(tr("To Playhead"));
	function->commandName = "WorkCursorMoveToPlayhead";
    registerFunction(function);

    function = new TFunction();
    function->object = "WorkCursorMove";
    function->slotsignature = "move_to_start";
    function->setDescription(tr("To Start"));
    function->commandName = "WorkCursorMoveToStart";
    registerFunction(function);

	function = new TFunction();
	function->object = "TMainWindow";
	function->slotsignature = "show_track_finder";
	function->setDescription(tr("Activate Track Finder"));
	function->commandName = "MainWindowActivateTrackFinder";
    registerFunction(function);

	function = new TFunction();
	function->object = "TMainWindow";
	function->slotsignature = "browse_to_first_track_in_active_sheet";
	function->setDescription(tr("Browse to first Track in current View"));
	function->commandName = "MainWindowNavigateToFirstTrack";
    registerFunction(function);

	function = new TFunction();
	function->object = "TMainWindow";
	function->slotsignature = "browse_to_last_track_in_active_sheet";
	function->setDescription(tr("Browse to last Track in current View"));
	function->commandName = "MainWindowNavigateToLastTrack";
    registerFunction(function);


    createAndAddFunction("ProcessingData", tr("Gain Envelope"), "toggle_show_gain_automation_curve", "GainShowAutomation");
    createAndAddFunction("AudioTrackView", tr("Insert Silence"), "insert_silence", "AudioTrackInsertSilence");

}

void TShortcutManager::createAndAddFunction(const QString &object, const QString &description, const QString &slotSignature, const QString &commandName)
{
    auto function = new TFunction();
    function->object = object;
    function->m_description = description;
    function->slotsignature = slotSignature;
    function->commandName = commandName;
    registerFunction(function);
}


void TShortcutManager::saveFunction(TFunction *function)
{
	QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Traverso", "Shortcuts");

	settings.beginGroup(function->commandName);
	settings.setValue("keys", function->getKeys(false).join(";"));
	QStringList modifiers = function->getModifierSequence(false).split("+", QString::SkipEmptyParts);
	settings.setValue("modifiers", modifiers.join(";"));
	settings.setValue("sortorder", function->sortorder);
	if (!function->submenu.isEmpty())
	{
		settings.setValue("submenu", function->submenu);
	}
	if (function->getAutoRepeatInterval() >= 0)
	{
		settings.setValue("autorepeatinterval", function->getAutoRepeatInterval());
	}
	if (function->getAutoRepeatStartDelay() >= 0)
	{
		settings.setValue("autorepeatstartdelay", function->getAutoRepeatStartDelay());
	}
	if (function->getInheritedFunction())
	{
		if (function->usesInheritedBase())
		{
			settings.setValue("usesinheritedbase", true);
		}
		else
		{
			settings.setValue("usesinheritedbase", false);
		}
	}
	settings.endGroup();
}

void TShortcutManager::exportFunctions()
{
	foreach(TFunction* function, m_functions)
	{
		saveFunction(function);
	}
}

void TShortcutManager::loadShortcuts()
{
	foreach(TShortcut* shortCut, m_shortcuts)
	{
		delete shortCut;
	}

	m_shortcuts.clear();

	QSettings defaultSettings(":/Traverso/shortcuts.ini", QSettings::IniFormat);
	QSettings userSettings(QSettings::IniFormat, QSettings::UserScope, "Traverso", "Shortcuts");
	QSettings* settings;

	QStringList defaultGroups = defaultSettings.childGroups();
	QStringList userGroups = userSettings.childGroups();
	QList<TFunction*> functionsThatInherit;

	foreach(TFunction* function, m_functions)
	{
		function->m_keys.clear();
		function->m_modifierkeys.clear();

		if (userGroups.contains(function->commandName))
		{ // prefer user settings over default settings
			settings = &userSettings;
		}
		else if (defaultGroups.contains(function->commandName))
		{ // no user setting available, fallback to default
			settings = &defaultSettings;
		}
		else
		{ // huh ?
			printf("No shortcut definition for function %s\n", QS_C(function->commandName));
			continue;
		}

		settings->beginGroup(function->commandName);
		QString keyString = settings->value("keys").toString();
		QStringList keys = keyString.toUpper().split(";", QString::SkipEmptyParts);
		QStringList modifiers = settings->value("modifiers").toString().toUpper().split(";", QString::SkipEmptyParts);
		QString autorepeatinterval = settings->value("autorepeatinterval").toString();
		QString autorepeatstartdelay = settings->value("autorepeatstartdelay").toString();
		QString submenu = settings->value("submenu").toString();
		QString sortorder = settings->value("sortorder").toString();
		if (settings->contains("usesinheritedbase"))
		{
			bool usesInheritedBase = settings->value("usesinheritedbase").toBool();
			if (usesInheritedBase)
			{
				function->setUsesInheritedbase(true);
			}
			else
			{
				function->setUsesInheritedbase(false);
			}
		}
		settings->endGroup();

		function->submenu = submenu;


		foreach(QString string, modifiers)
		{
			int modifier;
			if (t_KeyStringToKeyValue(modifier, string))
			{
				function->m_modifierkeys << modifier;
			}
		}

		bool ok;
		int interval = autorepeatinterval.toInt(&ok);
		if (ok)
		{
            function->setAutoRepeatInterval(interval);
		}

		int startdelay = autorepeatstartdelay.toInt(&ok);
		if (ok)
		{
            function->setAutoRepeatStartDelay(startdelay);
		}

		int order = sortorder.toInt(&ok);
		if (ok)
		{
			function->sortorder = order;
		}

		function->m_keys << keys;

		if (!function->getInheritedBase().isEmpty())
		{
			functionsThatInherit.append(function);
		}

		if (!function->usesInheritedBase())
		{
			foreach(QString key, function->getKeys())
			{
                TShortcut* shortcut = getShortcutForKey(key);
				if (shortcut)
				{
					foreach(QString object, function->getObjects())
					{
						shortcut->objects.insertMulti(object, function);
					}
				}
			}
		}
	}

	foreach(TFunction* function, functionsThatInherit)
	{
		TFunction* inheritedFunction = getFunction(function->getInheritedBase());
		if (inheritedFunction)
		{
			function->setInheritedFunction(inheritedFunction);
			if (function->usesInheritedBase())
			{
				foreach(QString key, function->getKeys())
				{
                    TShortcut* shortcut = getShortcutForKey(key);
					if (shortcut)
					{
						foreach(QString object, function->getObjects())
						{
							shortcut->objects.insertMulti(object, function);
						}
					}
				}
			}
		}
	}
}

void TShortcutManager::modifyFunctionKeys(TFunction *function, const QStringList& keys, QStringList modifiers)
{
	function->m_keys.clear();
	function->m_modifierkeys.clear();

	function->m_keys << keys;

	foreach(QString string, modifiers)
	{
		int modifier;
		if (t_KeyStringToKeyValue(modifier, string))
		{
			function->m_modifierkeys << modifier;
		}
	}


	saveFunction(function);
	loadShortcuts();
	emit functionKeysChanged();
}

void TShortcutManager::modifyFunctionInheritedBase(TFunction *function, bool usesInheritedBase)
{
	function->setUsesInheritedbase(usesInheritedBase);
	saveFunction(function);
	loadShortcuts();
	emit functionKeysChanged();
}

void TShortcutManager::restoreDefaultFor(TFunction *function)
{
	QSettings userSettings(QSettings::IniFormat, QSettings::UserScope, "Traverso", "Shortcuts");

	userSettings.beginGroup(function->commandName);
	foreach(QString key, userSettings.childKeys())
	{
		userSettings.remove(key);
	}
	loadShortcuts();
	emit functionKeysChanged();
}

void TShortcutManager::restoreDefaults()
{
	QSettings userSettings(QSettings::IniFormat, QSettings::UserScope, "Traverso", "Shortcuts");
	userSettings.clear();
	loadShortcuts();
	emit functionKeysChanged();

}

void TShortcutManager::add_meta_object(const QMetaObject* mo)
{
	QString shortcutItem = QString(mo->className()).remove("View");
	QList<const QMetaObject*> list = m_metaObjects.value(shortcutItem);
	list.append(mo);
	m_metaObjects.insert(shortcutItem, list);

	while(mo)
	{
		QString objectName = mo->className();
		if (objectName == "ContextItem" || objectName == "QObject")
		{
			return;
		}
		registerItemClass(shortcutItem, objectName);
		mo = mo->superClass();
	}
}

void TShortcutManager::registerItemClass(const QString &itemName, const QString &className)
{
	QStringList classesList = m_classes.value(itemName);
	classesList.append(className);
	m_classes.insert(itemName, classesList);
}

void TShortcutManager::add_translation(const QString &signature, const QString &translation)
{
	m_translations.insert(signature, translation);
}


QString TShortcutManager::get_translation_for(const QString &entry)
{
	QString key = entry;
	key = key.remove("View");
	if (!m_translations.contains(key)) {
        qDebug("TShortcutManager::get_translation_for(%s) not in database", entry.toLatin1().data());
		return QString("TShortcutManager: %1 not found!").arg(key);
	}
	return m_translations.value(key);
}

QString TShortcutManager::createHtmlForClass(const QString& className, QObject* object)
{
	QString holdKeyFact = "";

	QString name = get_translation_for(className);

	QString baseColor = QApplication::palette().color(QPalette::Base).name();
	QString alternateBaseColor = QApplication::palette().color(QPalette::AlternateBase).name();

	QString html = QString("<html><head><meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n"
	      "<style type=\"text/css\">\n"
	      "table {font-size: 11px;}\n"
	      ".object {background-color: %1; font-size: 11px;}\n"
	      ".description {background-color: %2; font-size: 10px; font-weight: bold;}\n"
	      "</style>\n"
	      "</head>\n<body>\n").arg(QApplication::palette().color(QPalette::AlternateBase).darker(105).name()).arg(alternateBaseColor);

	if (object && object->inherits("PCommand")) {
		PCommand* pc = dynamic_cast<PCommand*>(object);
		html += "<table><tr class=\"object\">\n<td width=220 align=\"center\">" + pc->text() + "</td></tr>\n";
	} else {
		html += "<table><tr class=\"object\">\n<td colspan=\"2\" align=\"center\"><b>" + name + "</b><font style=\"font-size: 11px;\">&nbsp;&nbsp;&nbsp;" + holdKeyFact + "</font></td></tr>\n";
		html += "<tr><td width=110 class=\"description\">" +tr("Description") + "</td><td width=110 class=\"description\">" + tr("Shortcut") + "</td></tr>\n";
	}

	QStringList result;
	int j=0;
	QList<TFunction* > list = getFunctionsFor(className);
	QMap<QString, QList<TFunction*> > functionsMap;

	foreach(TFunction* function, list)
    {
		QList<TFunction*> listForKey = functionsMap.value(function->submenu);
		listForKey.append(function);
		functionsMap.insert(function->submenu, listForKey);
	}

	QStringList subMenus = functionsMap.keys();

	foreach(QString submenu, subMenus)
	{
		if (!submenu.isEmpty()) {
			result += "<tr class=\"object\">\n<td colspan=\"2\" align=\"center\">"
				      "<font style=\"font-size: 11px;\"><b>" + submenu + "</b></font></td></tr>\n";
		}

		QList<TFunction*> subMenuFunctionList = functionsMap.value(submenu);

		foreach(TFunction* function, subMenuFunctionList)
		{
            QString keySequence = function->getKeySequence();
			keySequence.replace(QString("Up Arrow"), QString("&uarr;"));
			keySequence.replace(QString("Down Arrow"), QString("&darr;"));
			keySequence.replace(QString("Left Arrow"), QString("&larr;"));
			keySequence.replace(QString("Right Arrow"), QString("&rarr;"));
			keySequence.replace(QString("-"), QString("&#45;"));
			keySequence.replace(QString("+"), QString("&#43;"));
			keySequence.replace(QString(" , "), QString("<br />"));


			QString alternatingColor;
			if ((j % 2) == 1) {
				alternatingColor = QString("bgcolor=\"%1\"").arg(baseColor);
			} else {
				alternatingColor = QString("bgcolor=\"%1\"").arg(alternateBaseColor);
			}
			j += 1;

			result += QString("<tr %1><td>").arg(alternatingColor) + function->getDescription() + "</td><td>" + keySequence + "</td></tr>\n";
		}
	}

	result.removeDuplicates();
	html += result.join("");
	html += "</table>\n";
	html += "</body>\n</html>";

	return html;
}

TCommand * TShortcutManager::export_keymap()
{
	QTextStream out;
	QFile data(QDir::homePath() + "/traversokeymap.html");
	if (data.open(QFile::WriteOnly | QFile::Truncate)) {
		out.setDevice(&data);
	} else {
        return nullptr;
	}

	QString str;
    get_keymap(str);
	out << str;

	data.close();
    return nullptr;
}

TCommand * TShortcutManager::get_keymap(QString &str)
{
	str = "<html><head><meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n"
	      "<style type=\"text/css\">\n"
	      "H1 {text-align: left; font-size: 20px;}\n"
	      "table {font-size: 12px; border: solid; border-width: 1px; width: 600px;}\n"
	      ".object {background-color: #ccc; font-size: 16px; font-weight: bold;}\n"
	      ".description {background-color: #ddd; width: 300px; padding: 2px; font-size: 12px; font-weight: bold;}\n"
	      "</style>\n"
	      "</head>\n<body>\n<h1>Traverso keymap: " + config().get_property("InputEventDispatcher", "keymap", "default").toString() + "</h1>\n";

	foreach(QString className, tShortCutManager().getClassNames()) {
		str += tShortCutManager().createHtmlForClass(className);
		str += "<p></p><p></p>\n";
	}

	str += "</body>\n</html>";

    return nullptr;
}

bool TShortcutManager::classInherits(const QString& className, const QString &inherited)
{
	QList<const QMetaObject*> metas = m_metaObjects.value(className);

	foreach(const QMetaObject* mo, metas) {
		while (mo) {
			if (mo->className() == inherited)
			{
				return true;
			}
			mo = mo->superClass();
		}
	}

	return false;
}

