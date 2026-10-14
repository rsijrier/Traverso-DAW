/*
Copyright (C) 2007-2024 Remon Sijrier

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

#ifndef TTRAVERSO_COMMANDS_H
#define TTRAVERSO_COMMANDS_H


#include <TCommandPlugin.h>

class TShortCutFunction;
class TShortCutManager;
class TCommandController;

class TraversoCommands : public TCommandPlugin
{
	Q_OBJECT

public:
	TraversoCommands();

    void load(TShortCutManager* m);
    TCommand* create(QObject* obj, TActionId action, QVariantList arguments);
    TCommandController* create_controller(QObject* obj, TActionId action, QVariantList arguments);
    QStringList unregistered_commands() const override;

private:

    const bool USE_X = true;
    const bool USE_Y = true;
    const bool NO_X = false;
    const bool NO_Y = false;

    enum TraversoCommand {
        NoCommand,
        AddMarkerCommand,
		GainCommand,
		TrackPanCommand,
		ImportAudioCommand,
		AddNewAudioTrackCommand,
		RemoveClipCommand,
		RemoveTrackCommand,
        RemoveCurveNodeCommmand,
		RemovePluginCommand,
        RemoveTimeLineRulerMarkerCommand,
		AudioClipExternalProcessingCommand,
		ClipSelectionCommand,
		MoveClipCommand,
		MoveTrackCommand,
        SplitAudioClipCommand,
        AudioClipDualTrimCommand,
		CropClipCommand,
		ArmTracksCommand,
		ZoomCommand,
        ShuttleCommand,
		NormalizeClipCommand,
		ArrowKeyBrowserCommand,
		WorkCursorMoveCommand,
		MoveEdgeCommand,
		MoveCurveNodesCommand,
        MoveMarkerCommand,
        MovePluginCommand,
        FadeRangeCommand,
        FadeCurveBendCommand,
        FadeCurveStrengthCommand,
        GainShowAutomationCommand,
        TransportSetPositionCommand,
	};
    Q_ENUM(TraversoCommand)


    enum MarkerLocation {
        PlayCursor = 0,
        WorkCursor,
        CursorLocation
    };

private:
    [[maybe_unused]] TShortCutFunction* add_function(const QMetaObject *metaObject,
                      const TAction &action,
                      TraversoCommand command=NoCommand,
                      bool useX = false,
                      bool useY = false,
                      const QVariantList &args = QVariantList())
    {
        return add_function(metaObject, action, nullptr, command, useX, useY, args);
    }

    [[maybe_unused]] TShortCutFunction* add_function(const QMetaObject *metaObject,
                      const TAction &action,
                      const TActionGroup &group,
                      TraversoCommand command=NoCommand,
                      bool useX = false,
                      bool useY = false,
                      const QVariantList &args = QVariantList())
    {
        return add_function(metaObject, action, &group, command, useX, useY, args);
    }

    TShortCutFunction* add_function(const QMetaObject *metaObject,
                      const TAction &action,
                      const TActionGroup *group,
                      TraversoCommand command,
                      bool useX = false,
                      bool useY = false,
                      const QVariantList &args = QVariantList());

    TShortCutManager* m_manager{nullptr};
};

class TResetBase : public QObject
{
    Q_OBJECT
};
class TToggleBypassBase : QObject
{
    Q_OBJECT
};
class TDeleteBase : QObject
{
    Q_OBJECT
};
class TMoveBase : QObject
{
    Q_OBJECT
};
class TGainBase : QObject
{
    Q_OBJECT
};
class TToggleVerticalBase : QObject
{
    Q_OBJECT
};
class TEditPropertiesBase : QObject
{
    Q_OBJECT
};
class TNavigationBaseUp : QObject
{
    Q_OBJECT
};
class TNavigationBaseDown : QObject
{
    Q_OBJECT
};
class TNavigationBaseLeft : QObject
{
    Q_OBJECT
};
class TNavigationBaseRight : QObject
{
    Q_OBJECT
};

#endif

//eof
