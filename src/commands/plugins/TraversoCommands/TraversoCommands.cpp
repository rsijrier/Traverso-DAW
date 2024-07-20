/*
Copyright (C) 2007-2010 Remon Sijrier

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

#include "TraversoCommands.h"

#include <QInputDialog>
#include <QStringList>

#include "AudioClipManager.h"
#include "AudioTrack.h"
#include "Information.h"
#include "Project.h"
#include "ProjectManager.h"
#include "Sheet.h"
#include "TBusTrack.h"
#include "TInputEventDispatcher.h"
#include "TShortCutFunction.h"
#include "TTimeLineRuler.h"
#include "libtraversosheetcanvas.h"
#include "commands.h"
#include <cfloat>
#include "TMainWindow.h"
#include "TTransport.h"
#include "TShortCutManager.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

/**
 *	\class TraversoCommands
    \brief The Traverso TCommandPlugin class which 'implements' many of the default Commands

    With this plugin, the TInputEventDispatcher is able to dispatch key actions by directly
    asking this Plugin for the needed Command object.
 */


TraversoCommands::TraversoCommands()
{

    tShortCutManager().add_meta_object(&Sheet::staticMetaObject);
    tShortCutManager().add_meta_object(&SheetView::staticMetaObject);
    tShortCutManager().add_meta_object(&AudioTrack::staticMetaObject);
    tShortCutManager().add_meta_object(&AudioTrackView::staticMetaObject);
    tShortCutManager().add_meta_object(&TBusTrack::staticMetaObject);
    tShortCutManager().add_meta_object(&TBusTrackView::staticMetaObject);
    tShortCutManager().add_meta_object(&AudioClip::staticMetaObject);
    tShortCutManager().add_meta_object(&AudioClipView::staticMetaObject);
    tShortCutManager().add_meta_object(&Curve::staticMetaObject);
    tShortCutManager().add_meta_object(&CurveView::staticMetaObject);
    tShortCutManager().add_meta_object(&TTimeLineRuler::staticMetaObject);
    tShortCutManager().add_meta_object(&TimeLineView::staticMetaObject);
    tShortCutManager().add_meta_object(&Plugin::staticMetaObject);
    tShortCutManager().add_meta_object(&PluginView::staticMetaObject);
    tShortCutManager().add_meta_object(&FadeCurve::staticMetaObject);
    tShortCutManager().add_meta_object(&FadeCurveView::staticMetaObject);
    tShortCutManager().add_meta_object(&TMainWindow::staticMetaObject);
    tShortCutManager().add_meta_object(&ProjectManager::staticMetaObject);
    tShortCutManager().add_meta_object(&TGainGroupCommand::staticMetaObject);
    tShortCutManager().add_meta_object(&MoveTrack::staticMetaObject);
    tShortCutManager().add_meta_object(&MoveClip::staticMetaObject);
    tShortCutManager().add_meta_object(&MovePlugin::staticMetaObject);
    tShortCutManager().add_meta_object(&MoveCurveNode::staticMetaObject);
    tShortCutManager().add_meta_object(&Zoom::staticMetaObject);
    tShortCutManager().add_meta_object(&TrackPan::staticMetaObject);
    tShortCutManager().add_meta_object(&MoveMarker::staticMetaObject);
    tShortCutManager().add_meta_object(&WorkCursorMove::staticMetaObject);
    tShortCutManager().add_meta_object(&PlayHeadMove::staticMetaObject);
    tShortCutManager().add_meta_object(&MoveEdge::staticMetaObject);
    tShortCutManager().add_meta_object(&CropClip::staticMetaObject);
    tShortCutManager().add_meta_object(&FadeRange::staticMetaObject);
    tShortCutManager().add_meta_object(&SplitClip::staticMetaObject);
    tShortCutManager().add_meta_object(&TPanKnobView::staticMetaObject);
    tShortCutManager().add_meta_object(&TMoveCommand::staticMetaObject);
    tShortCutManager().add_meta_object(&TTransport::staticMetaObject);

    tShortCutManager().add_translation("TTransport", tr("Transport"));

    tShortCutManager().add_translation("ArrowKeyBrowser", tr("Arrow Key Browser"));
    tShortCutManager().add_translation("ArmTracks", tr("Arm Tracks"));
    tShortCutManager().add_translation("CropClip", tr("Cut Clip (Magnetic)"));
    tShortCutManager().add_translation("Fade", tr("Fade In/Out"));
    tShortCutManager().add_translation("TGainGroupCommand", tr("Gain"));
    tShortCutManager().add_translation("MoveClip", tr("Move Clip"));
    tShortCutManager().add_translation("MoveCurveNode", tr("Move Node"));
    tShortCutManager().add_translation("MoveEdge", tr("Move Clip Edge"));
    tShortCutManager().add_translation("MoveMarker", tr("Move Marker"));
    tShortCutManager().add_translation("MoveTrack", tr("Move Track"));
    tShortCutManager().add_translation("MovePlugin", tr("Move Plugin"));
    tShortCutManager().add_translation("PlayHeadMove", tr("Move Play Head"));
    tShortCutManager().add_translation("Shuttle", tr("Shuttle"));
    tShortCutManager().add_translation("SplitClip", tr("Split Clip"));
    tShortCutManager().add_translation("TrackPan", tr("Track Pan"));
    tShortCutManager().add_translation("TPanKnob", tr("Pan Knob"));
    tShortCutManager().add_translation("TPanKnobView", tr("Pan Knob"));

    tShortCutManager().add_translation("WorkCursorMove", tr("Move Work Cursor"));
    tShortCutManager().add_translation("Zoom", tr("Zoom"));

    tShortCutManager().add_translation("AudioClip",tr("Audio Clip"));
    tShortCutManager().add_translation("AudioTrack", tr("Audio Track"));
    tShortCutManager().add_translation("Curve",tr("Curve"));
    tShortCutManager().add_translation("CurveNode",tr("Curve Node"));
    tShortCutManager().add_translation("FadeCurve",tr("Fade Curve"));
    tShortCutManager().add_translation("FadeRange", tr("Fade Length"));
    tShortCutManager().add_translation("FadeBend", tr("Bend Factor"));
    tShortCutManager().add_translation("FadeStrength", tr("Strength Factor"));
    tShortCutManager().add_translation("Marker",tr("Marker"));
    tShortCutManager().add_translation("Sheet",tr("Sheet"));
    tShortCutManager().add_translation("TBusTrack",tr("Bus Track"));
    tShortCutManager().add_translation("TTimeLineRuler",tr("Time Line"));
    tShortCutManager().add_translation("TBusTrackPanel", tr("Bus Track"));
    tShortCutManager().add_translation("TMainWindow", tr("Global"));
    tShortCutManager().add_translation("ProjectManager", tr("Project Manager"));
    tShortCutManager().add_translation("TrackPanelGain", tr("Gain"));
    tShortCutManager().add_translation("TrackPanelPan", tr("Panorama"));
    tShortCutManager().add_translation("TrackPanelLed", tr("Track Panel Button"));
    tShortCutManager().add_translation("TrackPanelBus", tr("Routing Indicator"));
    tShortCutManager().add_translation("VUMeterLevel", tr("VU Level"));
    tShortCutManager().add_translation("VUMeter", tr("VU Level"));
    tShortCutManager().add_translation("AudioTrackPanel",tr("Audio Track"));
    tShortCutManager().add_translation("Plugin",tr("Plugin"));
    tShortCutManager().add_translation("PlayHead", tr("Play Head"));
    tShortCutManager().add_translation("PositionIndicator", tr("Position Indicator"));
    tShortCutManager().add_translation("WorkCursor", tr("Work Cursor"));
    tShortCutManager().add_translation("Playhead", tr("Play Cursor"));
    tShortCutManager().add_translation("CorrelationMeter", tr("Correlation Meter"));
    tShortCutManager().add_translation("SpectralMeter", tr("Spectral Analyzer"));
    tShortCutManager().add_translation("Track", tr("Track"));
    tShortCutManager().add_translation("TMoveCommand", tr("Shuttle"));
    tShortCutManager().add_translation("EditProperties", tr("Edit Properties"));
    tShortCutManager().add_translation("TAudioProcessingNode", tr("Audio Processing Node"));
    tShortCutManager().add_translation("ToggleBypass", tr("Toggle Bypass"));
    tShortCutManager().add_translation("HoldCommand", tr("Hold Command"));
    tShortCutManager().add_translation("Navigate", tr("Navigate"));

    TShortCutFunction* function;

    function = new TShortCutFunction();
    function->object = "SheetView::ArrowKeyBrowser";
    function->setSlotSignature("up");
    function->setDescription(tr("Up"));
    function->commandName = "ArrowKeyBrowserUp";
    add_function(function, ArrowKeyBrowserCommand);

    function = new TShortCutFunction();
    function->object = "SheetView::ArrowKeyBrowser";
    function->setSlotSignature("down");
    function->setDescription(tr("Down"));
    function->commandName = "ArrowKeyBrowserDown";
    add_function(function, ArrowKeyBrowserCommand);

    function = new TShortCutFunction();
    function->object = "SheetView::ArrowKeyBrowser";
    function->setSlotSignature("left");
    function->setDescription(tr("Left"));
    function->commandName = "ArrowKeyBrowserLeft";
    add_function(function, ArrowKeyBrowserCommand);

    function = new TShortCutFunction();
    function->object = "SheetView::ArrowKeyBrowser";
    function->setSlotSignature("right");
    function->setDescription(tr("Right"));
    function->commandName = "ArrowKeyBrowserRight";
    add_function(function, ArrowKeyBrowserCommand);

    function = new TShortCutFunction();
    function->object = "AudioTrack";
    function->setDescription(tr("Import Audio"));
    function->commandName = "ImportAudio";
    add_function(function, ImportAudioCommand);

    function = new TShortCutFunction();
    function->object = "AudioTrackView";
    function->setDescription(tr("Fold Track"));
    function->commandName = "FoldTrack";
    function->useX = true;
    function->arguments << "fold_track";
    add_function(function, MoveClipCommand);

    function = new TShortCutFunction();
    function->object = "TimeLineView";
    function->setDescription(tr("Fold Markers"));
    function->commandName = "FoldMarkers";
    function->useX = true;
    function->arguments << "fold_markers";
    add_function(function, MoveClipCommand);

    function = new TShortCutFunction();
    function->object = "TrackView";
    function->setDescription(tr("Move Up/Down"));
    function->commandName = "MoveTrack";
    function->useY = true;
    function->setInheritedBase("MoveBase");
    add_function(function, MoveTrackCommand);

    function = new TShortCutFunction();
    function->object = "CurveView";
    function->setDescription(tr("Move Curve Node(s)"));
    function->commandName = "MoveCurveNodes";
    function->useX = function->useY = true;
    function->setInheritedBase("MoveBase");
    add_function(function, MoveCurveNodesCommand);

    function = new TShortCutFunction();
    function->object = "AudioTrack";
    function->setDescription(tr("Gain"));
    function->commandName = "AudioTrackGain";
    function->setInheritedBase("GainBase");
    add_function(function, GainCommand);

    function = new TShortCutFunction();
    function->object = "TBusTrack";
    function->setDescription(tr("Gain"));
    function->commandName = "BusTrackGain";
    function->setInheritedBase("GainBase");
    add_function(function, GainCommand);

    function = new TShortCutFunction();
    function->object = "SheetView";
    function->setDescription(tr("Zoom"));
    function->commandName = "Zoom";
    function->useX = true;
    function->arguments << "HJogZoom" << "1.2" << "0.2";
    add_function(function, ZoomCommand);

    function = new TShortCutFunction();
    function->object = "TimeLineView";
    function->setDescription(tr("Move Marker"));
    function->commandName = "TimeLineMoveMarker";
    function->useX = true;
    function->setInheritedBase("MoveBase");
    add_function(function, MoveMarkerCommand);

    function = new TShortCutFunction();
    function->object = "MarkerView";
    function->setDescription(tr("Move Marker"));
    function->commandName = "MoveMarker";
    function->useX = true;
    function->setInheritedBase("MoveBase");
    add_function(function, MoveMarkerCommand);

    function = new TShortCutFunction();
    function->object = "Track";
    function->setDescription(tr("Track Pan"));
    function->commandName = "TrackPan";
    add_function(function, TrackPanCommand);


    function = new TShortCutFunction();
    function->object = "Track";
    function->commandName = "RemoveTrack";
    function->setInheritedBase("DeleteBase");
    add_function(function, RemoveTrackCommand);

    function = new TShortCutFunction();
    function->object = "PluginView";
    function->commandName = "RemovePlugin";
    function->setInheritedBase("DeleteBase");
    add_function(function, RemovePluginCommand);


    function = new TShortCutFunction();
    function->object = "CurveView";
    function->commandName = "RemoveCurveNode";
    function->setDescription("Remove Node(s)");
    function->setInheritedBase("DeleteBase");
    add_function(function, RemoveCurveNodeCommmand);

    function = new TShortCutFunction();
    function->object = "TPanKnobView";
    function->setDescription(tr("Panorama"));
    function->commandName = "PanKnobPanorama";
    add_function(function, TrackPanCommand);

    create_and_add_function("AudioClipView", tr("Copy"), "CopyClip", MoveClipCommand, QStringList() << "copy", "", true, true);
    create_and_add_function("AudioClipView", tr("Split"), "SplitClip", SplitClipCommand, QStringList(), "", true);
    create_and_add_function("AudioClipView", tr("Magnetic Cut"), "CropClip", CropClipCommand);
    create_and_add_function("AudioClipView", tr("Move"), "MoveClip", MoveClipCommand, QStringList() << "move", "MoveBase", true, true);
    create_and_add_function("AudioClipView", tr("Move Edge"), "MoveClipEdge", MoveEdgeCommand, QStringList() << "false", "", true);

    create_and_add_function("AudioClip", tr("External Processing"), "AudioClipExternalProcessing", AudioClipExternalProcessingCommand);
    create_and_add_function("AudioClip", tr("Gain"), "AudioClipGain", GainCommand, QStringList(), "GainBase");
    create_and_add_function("AudioClip", tr("Normalize Clip"), "NormalizeClip", NormalizeClipCommand);
    create_and_add_function("AudioClip", tr("Remove AudioClip"), "RemoveClip", RemoveClipCommand, QStringList(), "DeleteBase");
    create_and_add_function("AudioClip", tr("(De)Select"), "ClipSelectionSelect", ClipSelectionCommand, QStringList() << "toggle_selected");

    create_and_add_function("FadeCurveView", tr("Length"), "FadeLength", FadeRangeCommand);

    create_and_add_function("PluginView", tr("Move"), "MovePlugin", MovePluginCommand, QStringList(), "MoveBase", true);

    create_and_add_function("SheetView", tr("Fold Sheet"), "FoldSheet", MoveClipCommand, QStringList() << "fold_sheet", "", true, true);
    create_and_add_function("SheetView", tr("Move Work Cursor"), "WorkCursorMove", WorkCursorMoveCommand);
    create_and_add_function("SheetView", tr("Shuttle"), "Shuttle", ShuttleCommand, QStringList(), "", true, true);
}

void TraversoCommands::add_function(TShortCutFunction *function, TraversoCommand command)
{
    function->pluginname = "TraversoCommands";
    m_dict.insert(function->commandName, command);
    tShortCutManager().registerFunction(function);
}

void TraversoCommands::create_and_add_function(const QString &object, const QString &description, const QString &commandName, TraversoCommands::TraversoCommand command)
{
    create_and_add_function(object, description, commandName, command, QStringList());
}

void TraversoCommands::create_and_add_function(const QString &object, const QString &description, const QString &commandName, TraversoCommand command,
                                               QStringList arguments, const QString &inheritedBase, bool useX, bool useY)
{
    auto function = new TShortCutFunction();
    function->object = object;
    function->setDescription(description);
    function->commandName = commandName;
    if (!arguments.isEmpty()) {
        function->arguments << arguments;
    }
    function->useX = useX;
    function->useY = useY;
    if (!inheritedBase.isEmpty()) {
        function->setInheritedBase(inheritedBase);
    }
    add_function(function, command);
}

TCommand* TraversoCommands::create(QObject* obj, const QString& commandName, QVariantList arguments)
{
    switch (m_dict.value(commandName)) {
    case GainCommand:
    {
        ContextItem* item = qobject_cast<ContextItem*>(obj);

        if (item->metaObject()->className() == QString("TrackPanelGain")) {
            item = item->get_related_context_item();
        } else if (AudioClipView* view = qobject_cast<AudioClipView*>(item)) {
            item = view->get_related_context_item();
        } else if (TrackView* view = qobject_cast<TrackView*>(item)) {
            item = view->get_related_context_item();
        }


        if (!item) {
            PERROR("TraversoCommands: Supplied QObject was not a ContextItem, "
                   "GainCommand only works with ContextItem objects!!");
            return nullptr;
        }

        auto group = new TGainGroupCommand(item, arguments);

        AudioClip* clip = qobject_cast<AudioClip*>(item);
        if (clip && clip->is_selected()) {

            QList<AudioClip* > selection;
            clip->get_sheet()->get_audioclip_manager()->get_selected_clips(selection);

            // always the contextitem first so it will be the primary gain object
            group->add_command(new Gain(item, arguments));

            for(auto audioProcessingNode : selection) {
                // only add the context item once
                if (audioProcessingNode == item) {
                    continue;
                }
                group->add_command(new Gain(audioProcessingNode, arguments));
            }
        } else {
            group->add_command(new Gain(item, arguments));
        }


        return group;
    }

    case TrackPanCommand:
    {
        Track* track = qobject_cast<Track*>(obj);
        if (! track) {
            TPanKnobView* knob = qobject_cast<TPanKnobView*>(obj);
            if(knob) {
                track = knob->get_track();
            }
            if (!track) {
                PERROR("TraversoCommands: Supplied QObject was not a Track! "
                       "TrackPanCommand needs a Track as argument");
                return 0;
            }
        }
        return new TrackPan(track, arguments);
    }

    case ImportAudioCommand:
    {
        AudioTrack* track = qobject_cast<AudioTrack*>(obj);
        if (! track) {
            PERROR("TraversoCommands: Supplied QObject was not a Track! "
                   "ImportAudioCommand needs a Track as argument");
            return 0;
        }

        auto audioFileImportCommand = new TAudioFileImportCommand(track);
        audioFileImportCommand->set_track(track);
        return audioFileImportCommand;
    }

    case InsertSilenceCommand:
    {
        AudioTrack* track = qobject_cast<AudioTrack*>(obj);
        if (! track) {
            PERROR("TraversoCommands: Supplied QObject was not a Track! "
                   "ImportAudioCommand needs a Track as argument");
            return 0;
        }
        TTimeRef length(10*TTimeRef::UNIVERSAL_SAMPLE_RATE);
        auto audioFileImportCommand = new TAudioFileImportCommand(track);
        audioFileImportCommand->set_track(track);
        audioFileImportCommand->set_length(length);
        audioFileImportCommand->set_silent(true);
        return audioFileImportCommand;
    }

    case AddNewAudioTrackCommand:
    {
        Sheet* sheet = qobject_cast<Sheet*>(obj);
        if (!sheet) {
            PERROR("TraversoCommands: Supplied QObject was not a Sheet! "
                   "AddNewAudioTrackCommand needs a Sheet as argument");
            return 0;
        }
        return sheet->add_track(new AudioTrack(sheet, "Unnamed", AudioTrack::INITIAL_HEIGHT));
    }

    case RemoveClipCommand:
    {
        AudioClip* clip = qobject_cast<AudioClip*>(obj);
        if (!clip) {
            PERROR("TraversoCommands: Supplied QObject was not a Clip! "
                   "RemoveClipCommand needs a Clip as argument");
            return 0;
        }
        return new AddRemoveClip(clip, AddRemoveClip::REMOVE);
    }

    case RemoveTrackCommand:
    {
        Track* track = qobject_cast<Track*>(obj);
        if (!track) {
            PERROR("TraversoCommands: Supplied QObject was not a Track! "
                   "RemoveTrackCommand needs a Track as argument");
            return 0;
        }

        TSession* activeSession = pm().get_project()->get_current_session();
        if (!activeSession) {
            // this is rather impossible!!
            info().information(tr("Removing Track %1, but no active (Work) Sheet ??").arg(track->get_name()));
            return 0;
        }

        if (track == activeSession->get_master_out_bus_track()) {
            info().information(tr("It is not possible to remove the Master Out track!"));
            return 0;
        }
        return activeSession->remove_track(track);
    }

    case RemovePluginCommand:
    {
        PluginView* view = qobject_cast<PluginView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not a PluginView! "
                   "RemovePluginCommand needs a PluginView as argument");
            return 0;
        }

        return view->remove_plugin();
    }

    case RemoveCurveNodeCommmand:
    {
        CurveView* curveView = qobject_cast<CurveView*>(obj);
        if (!curveView)
        {
            PERROR("TraversoCommands: Supplied QObject was not a CurveView! "
                   "RemoveClipNodeCommmand needs a CurveView as argument");
            return 0;
        }
        return curveView->remove_node();

    }
    case AudioClipExternalProcessingCommand:
    {
        AudioClip* clip = qobject_cast<AudioClip*>(obj);
        if (!clip) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClip! "
                   "AudioClipExternalProcessingCommand needs an AudioClip as argument");
            return 0;
        }
        return new AudioClipExternalProcessing(clip);
    }

    case ClipSelectionCommand:
    {
        Sheet* sheet = qobject_cast<Sheet*>(obj);
        if (sheet) {
            QString action;
            if (!arguments.empty()) {
                action = arguments.at(0).toString();
                if (action == "select_all_clips") {
                    return sheet->get_audioclip_manager()->select_all_clips();
                }
            }
        }
        AudioClip* clip = qobject_cast<AudioClip*>(obj);
        if (!clip) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClip! "
                   "ClipSelectionCommand needs an AudioClip as argument");
            return 0;
        }

        // audio clip selection doesn't support/need number collection, but
        // other commands do, so if ie() has number collection, ignore it for this clip
        if (ied().has_collected_number()) {
            return ied().did_not_implement();
        }

        return new ClipSelection(clip, arguments);
    }

    case MoveClipCommand:
    {
        // cast to super class ViewItem since we use MoveClip also to fold Track or Sheet
        ViewItem* view = qobject_cast<ViewItem*>(obj);
        if (view) {
            return new MoveClip(view, arguments);
        }
        PERROR("TraversoCommands: Supplied QObject was not a ViewItem, MoveClipCommand needs a ViewItem as argument");
        return nullptr;
    }

    case MoveTrackCommand:
    {
        TrackView* view = qobject_cast<TrackView*>(obj);

        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an TrackView! "
                   "MoveTrackCommand needs an TrackView as argument");
            return 0;
        }

        return new MoveTrack(view);
    }

    case MovePluginCommand:
    {
        PluginView* view = qobject_cast<PluginView*>(obj);
        if (view) {
            return new MovePlugin(view);
        }
        return nullptr;
    }


    case MoveEdgeCommand:
    {
        AudioClipView* view = qobject_cast<AudioClipView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClipView! "
                   "MoveEdgeCommand needs an AudioClipView as argument");
            return 0;
        }

        int x = (int) (cpointer().on_first_input_event_scene_x() - view->scenePos().x());

        if (x < (view->boundingRect().width() / 2)) {
            return new MoveEdge(view, view->get_sheetview(), "set_left_edge");
        } else {
            return new MoveEdge(view, view->get_sheetview(), "set_right_edge");
        }
    }

        // The existence of this is doubtfull. Using [ E ] is so much easier
        // then trying to mimic 'if near to edge, drag edge' features.
    case MoveClipOrEdgeCommand:
    {
        AudioClipView* view = qobject_cast<AudioClipView*>(obj);

        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClipView! "
                   "MoveClipOrEdgeCommand needs an AudioClipView as argument");
            return 0;
        }

        int x = (int) (cpointer().on_first_input_event_scene_x() - view->scenePos().x());

        int edge_width = 0;
        if (arguments.size() == 2) {
            edge_width = arguments[0].toInt();
        }

        if (x < edge_width) {
            return new MoveEdge(view, view->get_sheetview(), "set_left_edge");
        } else if (x > (view->boundingRect().width() - edge_width)) {
            return new MoveEdge(view, view->get_sheetview(), "set_right_edge");
        }

        return new MoveClip(view, QVariantList() << "move");
    }

    case SplitClipCommand:
    {
        AudioClipView* view = qobject_cast<AudioClipView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClipView! "
                   "SplitClipCommand needs an AudioClipView as argument");
            return 0;
        }
        return new SplitClip(view);
    }

    case CropClipCommand:
    {
        AudioClipView* view = qobject_cast<AudioClipView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClipView! "
                   "CropClipCommand needs an AudioClipView as argument");
            return 0;
        }
        return new CropClip(view);
    }

    case ArmTracksCommand:
    {
        SheetView* view = qobject_cast<SheetView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an SheetView! "
                   "ArmTracksCommand needs an SheetView as argument");
            return 0;
        }
        return new ArmTracks(view);
    }

    case ZoomCommand:
    {
        SheetView* view = qobject_cast<SheetView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an SheetView! "
                   "ZoomCommand needs an SheetView as argument");
            return 0;

        }
        return new Zoom(view, arguments);
    }

    case WorkCursorMoveCommand:
    {
        SheetView* view = qobject_cast<SheetView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an SheetView! "
                   "WorkCursorMove Command needs an SheetView as argument");
            return 0;
        }
        return new WorkCursorMove(view);
    }

    case MoveCurveNodesCommand:
    {
        CurveView* curveView = qobject_cast<CurveView*>(obj);
        if (!curveView) {
            return 0;
        }

        return curveView->drag_node();
    }

    case ArrowKeyBrowserCommand:
    {
        SheetView* view = qobject_cast<SheetView*>(obj);
        if (!view) {
            PERROR("TraversoCommands: Supplied QObject was not an SheetView! "
                   "ArrowKeyBrowserCommand needs an SheetView as argument");
            return 0;
        }
        return new ArrowKeyBrowser(view, arguments);
    }

    case ShuttleCommand:
    {
        SheetView* view = qobject_cast<SheetView*>(obj);
        if (view) {
            return new TMoveCommand(view, nullptr, "");
        }
        return nullptr;
    }

    case NormalizeClipCommand:
    {
        AudioClip* clip = qobject_cast<AudioClip*>(obj);
        if (!clip) {
            PERROR("TraversoCommands: Supplied QObject was not a Clip! "
                   "RemoveClipCommand needs a Clip as argument");
            return 0;
        }

        if (clip->is_selected()) {
            bool ok;
            float normfactor = FLT_MAX;

            double d = QInputDialog::getDouble(0, tr("Normalization"),
                                               tr("Set Normalization level:"), 0.0, -120, 0, 1, &ok);

            if (!ok) {
                return 0;
            }
            QList<AudioClip* > selection;
            clip->get_sheet()->get_audioclip_manager()->get_selected_clips(selection);
            foreach(AudioClip* selected, selection) {
                normfactor = std::min(selected->calculate_normalization_factor(d), normfactor);
            }

            CommandGroup* group = new CommandGroup(clip, tr("Normalize Selected Clips"));

            foreach(AudioClip* selected, selection) {
                group->add_command(new PCommand(selected, "set_gain", normfactor, selected->get_gain(), tr("AudioClip: Normalize")));
            }

            return group;
        }

        return clip->normalize();
    }
    case MoveMarkerCommand:
    {
        TimeLineView* view = qobject_cast<TimeLineView*>(obj);
        if (view)
        {
            return view->drag_marker();
        }

        MarkerView* markerView = qobject_cast<MarkerView*>(obj);
        if (markerView)
        {
            return markerView->drag_marker();
        }

        PERROR("TraversoCommands: Supplied QObject was not a TimeLineView or MarkerView! "
               "MoveMarkerCommand needs a TimeLineView or MarkerView as argument");
        return 0;
    }
    case FadeRangeCommand:
    {
        FadeCurveView* view = qobject_cast<FadeCurveView*>(obj);
        if (view) {
            return new FadeRange(view->get_audio_clip(), view->get_fade(), view->get_sheetview()->timeref_scalefactor);
        }
        return nullptr;
    }

    }

    return 0;
}

// eof
