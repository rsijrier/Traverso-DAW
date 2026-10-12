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

#include "TraversoCommands.h"

#include <QInputDialog>
#include <QStringList>

#include "TContextPointer.h"
#include "TPositionIndicator.h"
#include "TSheetWidget.h"
#include "TAudioClipManager.h"
#include "TAudioClip.h"
#include "TAudioTrack.h"
#include "TCurve.h"
#include "TCurveNode.h"
#include "TFadeCurve.h"
#include "TAudioPlugin.h"
#include "TInformUser.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TSheet.h"
#include "TBusTrack.h"
#include "TInputEventDispatcher.h"
#include "TShortCutFunction.h"
#include "TShortCutManager.h"
#include "TSplitEntityController.h"
#include "TTimeLineRuler.h"
#include "TVUMeterView.h"
#include "libtraversosheetcanvas.h"
#include "commands.h"
#include "TMainWindow.h"
#include "TTransport.h"
#include "widgets/SpectralMeterView.h"
#include "widgets/CorrelationMeterView.h"

#include "Debugger.h"

/**
 *	\class TraversoCommands
    \brief The Traverso TCommandPlugin class which 'implements' many of the default Commands

    With this plugin, the TInputEventDispatcher is able to dispatch key actions by directly
    asking this TAudioPlugin for the needed TCommand object.
 */


TraversoCommands::TraversoCommands()
{
}

void TraversoCommands::load(TShortCutManager* m)
{
    m_manager = m;

    // known submenu titles
    m->add_translation("Utilities", tr("&Utilities"));
    m->add_translation("Fade", tr("&Fade In/Out"));
    m->add_translation("Navigate", tr("&Navigate"));
    m->add_translation("Playhead", tr("&Playhead"));
    m->add_translation("WorkCursor", tr("&WorkCursor"));
    m->add_translation("Shuttle", tr("&Shuttle"));
    m->add_translation("Edge Trim", tr("&Edge Trim"));

    // register meta objects
    m->add_meta_object(&TSheet::staticMetaObject,               tr("Sheet"));
    m->add_meta_object(&TSheetView::staticMetaObject,           tr("Sheet"));
    m->add_meta_object(&TTrack::staticMetaObject,               tr("Track"));
    m->add_meta_object(&TAudioTrack::staticMetaObject,          tr("Audio Track"));
    m->add_meta_object(&TAudioTrackView::staticMetaObject,      tr("Audio Track"));
    m->add_meta_object(&TBusTrack::staticMetaObject,            tr("Bus Track"));
    m->add_meta_object(&TBusTrackView::staticMetaObject,        tr("Bus Track"));
    m->add_meta_object(&TAudioClip::staticMetaObject,           tr("Audio Clip"));
    m->add_meta_object(&TAudioClipView::staticMetaObject,       tr("Audio Clip"));
    m->add_meta_object(&TCurve::staticMetaObject,               tr("Curve"));
    m->add_meta_object(&TCurveView::staticMetaObject,           tr("Curve"));
    m->add_meta_object(&TTimeLineRuler::staticMetaObject,       tr("Time Line"));
    m->add_meta_object(&TTimeLineRulerView::staticMetaObject,   tr("Time Line"));
    m->add_meta_object(&TAudioPlugin::staticMetaObject,         tr("Plugin"));
    m->add_meta_object(&TAudioPluginView::staticMetaObject,     tr("Plugin"));
    m->add_meta_object(&TFadeCurve::staticMetaObject,           tr("Fade Curve"));
    m->add_meta_object(&TFadeCurveView::staticMetaObject,       tr("Fade In/Out"));
    m->add_meta_object(&TMainWindow::staticMetaObject,          tr("Global"));
    m->add_meta_object(&TProjectManager::staticMetaObject,      tr("Project Manager"));
    m->add_meta_object(&TFaderGroupCommand::staticMetaObject,   tr("Gain"));
    m->add_meta_object(&MoveTrack::staticMetaObject,            tr("Move Track"));
    m->add_meta_object(&MoveClip::staticMetaObject,             tr("Move Clip"));
    m->add_meta_object(&MovePlugin::staticMetaObject,           tr("Move Plugin"));
    m->add_meta_object(&MoveCurveNode::staticMetaObject,        tr("Move Node"));
    m->add_meta_object(&TZoomCommand::staticMetaObject,                 tr("Zoom"));
    m->add_meta_object(&TrackPan::staticMetaObject,             tr("Track Pan"));
    m->add_meta_object(&MoveMarker::staticMetaObject,           tr("Move Marker"));
    m->add_meta_object(&WorkCursorMove::staticMetaObject,       tr("Move Work Cursor"));
    m->add_meta_object(&PlayHeadMove::staticMetaObject,         tr("Move Play Head"));
    m->add_meta_object(&MoveEdge::staticMetaObject,             tr("Move Clip Edge"));
    m->add_meta_object(&CropClip::staticMetaObject,             tr("Cut Clip (Magnetic)"));
    m->add_meta_object(&TFadeRangeCommand::staticMetaObject,    tr("Fade Length"));
    m->add_meta_object(&FadeBend::staticMetaObject,             tr("Bend Factor"));
    m->add_meta_object(&FadeStrength::staticMetaObject,         tr("Strength Factor"));
    m->add_meta_object(&TSplitEntityController::staticMetaObject,tr("Split Clip"));
    m->add_meta_object(&TAudioClipDualTrimCommand::staticMetaObject, tr("Audio Clip Dual Trim"));
    m->add_meta_object(&TPanKnobView::staticMetaObject,         tr("Pan Knob"));
    m->add_meta_object(&TTransport::staticMetaObject,           tr("Transport"));
    m->add_meta_object(&SpectralMeterView::staticMetaObject,    tr("Spectral Analyzer"));
    m->add_meta_object(&CorrelationMeterView::staticMetaObject, tr("Correlation Meter"));
    m->add_meta_object(&ArrowKeyBrowser::staticMetaObject,      tr("Arrow Key Browser"));
    m->add_meta_object(&ArmTracks::staticMetaObject,            tr("Arm Tracks"));
    m->add_meta_object(&TMoveCommand::staticMetaObject,         tr("Shuttle"));
    m->add_meta_object(&TTimeLineMarker::staticMetaObject,      tr("Marker"));
    m->add_meta_object(&TBusTrackPanelView::staticMetaObject,   tr("Bus Track"));
    m->add_meta_object(&TrackPanelGain::staticMetaObject,       tr("Gain"));
    m->add_meta_object(&TrackPanelLed::staticMetaObject,        tr("Track Panel Button"));
    m->add_meta_object(&VUMeterLevelView::staticMetaObject,     tr("VU Level"));
    m->add_meta_object(&TVUMeterView::staticMetaObject,          tr("VU Level"));
    m->add_meta_object(&TAudioTrackPanelView::staticMetaObject,  tr("Audio Track"));
    m->add_meta_object(&TPlayHeadView::staticMetaObject,             tr("Play Cursor"));
    m->add_meta_object(&TWorkCursorView::staticMetaObject,           tr("Work Cursor"));
    m->add_meta_object(&TPositionIndicator::staticMetaObject,    tr("Position Indicator"));
    m->add_meta_object(&TAudioProcessingNode::staticMetaObject, tr("Audio Processing Node"));

    m->add_base_function(&TNavigationBaseUp::staticMetaObject,          tr("Navigate Up"),              "TNavigationBaseUp");
    m->add_base_function(&TNavigationBaseDown::staticMetaObject,        tr("Navigate Down"),            "TNavigationBaseDown");
    m->add_base_function(&TNavigationBaseLeft::staticMetaObject,        tr("Navigate Left"),            "TNavigationBaseLeft");
    m->add_base_function(&TNavigationBaseRight::staticMetaObject,       tr("Navigate Right"),           "TNavigationBaseRight");

    // Hold Commands need 1 key to start, and then additional keys for specific functionality
    // For features like navigating with arrow keys this is less productive, so we register the navigation
    // shortcut function to start autorepeating on itself.
    // We have to register the navigation keys twice, one to start the hold command and one for the
    // the autorepeat slot to get invoked.
    TShortCutFunction* shortCutFunction{nullptr};
    shortCutFunction = add_function(&TSheetView::staticMetaObject, &TNavigationBaseUp::staticMetaObject, "", "ArrowKeyBrowserStartUp",  ArrowKeyBrowserCommand, "", NO_X, USE_Y);
    Q_ASSERT(shortCutFunction);
    shortCutFunction->set_auto_repeats_on_itself(true);

    shortCutFunction = add_function(&TSheetView::staticMetaObject, &TNavigationBaseDown::staticMetaObject, "", "ArrowKeyBrowserStartDown",     ArrowKeyBrowserCommand, "", NO_X, USE_Y);
    Q_ASSERT(shortCutFunction);
    shortCutFunction->set_auto_repeats_on_itself(true);

    shortCutFunction = add_function(&TSheetView::staticMetaObject, &TNavigationBaseLeft::staticMetaObject, "", "ArrowKeyBrowserStartLeft",     ArrowKeyBrowserCommand, "", USE_X, NO_Y);
    Q_ASSERT(shortCutFunction);
    shortCutFunction->set_auto_repeats_on_itself(true);

    shortCutFunction = add_function(&TSheetView::staticMetaObject, &TNavigationBaseRight::staticMetaObject, "", "ArrowKeyBrowserStartRight",   ArrowKeyBrowserCommand, "", USE_X, NO_Y);
    Q_ASSERT(shortCutFunction);
    shortCutFunction->set_auto_repeats_on_itself(true);

    // ArrowKeyBrowserCommand will create an ArrowKeyBrowser Command, and since that will start autorepeat the same key
    // we can now register a new shortcut function to dispatch on the ArrowKeyBrowser Command object
    m->add_function(&ArrowKeyBrowser::staticMetaObject, &TNavigationBaseUp::staticMetaObject,       "ArrowKeyBrowserUp",      "up()");
    m->add_function(&ArrowKeyBrowser::staticMetaObject, &TNavigationBaseDown::staticMetaObject,     "ArrowKeyBrowserDown",    "down()");
    m->add_function(&ArrowKeyBrowser::staticMetaObject, &TNavigationBaseLeft::staticMetaObject,     "ArrowKeyBrowserLeft",    "left()");
    m->add_function(&ArrowKeyBrowser::staticMetaObject, &TNavigationBaseRight::staticMetaObject,    "ArrowKeyBrowserRight",   "right()");

    m->add_base_function(&TDeleteBase::staticMetaObject,            tr("Delete"),               "DeleteBase");

    add_function(&TAudioClip::staticMetaObject,         &TDeleteBase::staticMetaObject, tr("Remove AudioClip"),  "RemoveClip",           RemoveClipCommand);
    add_function(&TAudioPluginView::staticMetaObject,   &TDeleteBase::staticMetaObject, tr("Remove Plugin"),     "RemovePlugin",         RemovePluginCommand);
    add_function(&TCurveView::staticMetaObject,         &TDeleteBase::staticMetaObject, tr("Remove Node(s)"),    "RemoveCurveNode",      RemoveCurveNodeCommmand);
    add_function(&TTrack::staticMetaObject,             &TDeleteBase::staticMetaObject, tr("Remove Track"),      "RemoveTrack",          RemoveTrackCommand);
    add_function(&TTimeLineRulerView::staticMetaObject, &TDeleteBase::staticMetaObject, tr("Remove Marker"),     "RemoveTimeLineRulerMarker",RemoveTimeLineRulerMarkerCommand);

    m->add_base_function(&TEditPropertiesBase::staticMetaObject,    tr("Edit Properties"),      "EditPropertiesBase");

    m->add_function(&TTrackView::staticMetaObject,      &TEditPropertiesBase::staticMetaObject, "EditTrackProperties", "edit_properties()");

    m->add_base_function(&TGainBase::staticMetaObject,              tr("Gain"),                 "GainBase");

    add_function(&TAudioClip::staticMetaObject,         &TGainBase::staticMetaObject, "", "AudioClipGain",   GainCommand);
    add_function(&TAudioTrack::staticMetaObject,        &TGainBase::staticMetaObject, "", "AudioTrackGain",  GainCommand);
    add_function(&TBusTrack::staticMetaObject,          &TGainBase::staticMetaObject, "", "BusTrackGain",    GainCommand);

    m->add_base_function(&TMoveBase::staticMetaObject,              tr("Move"),                 "MoveBase");

    add_function(&TAudioClipView::staticMetaObject,     &TMoveBase::staticMetaObject, "",                        "MoveClip",             MoveClipCommand, "", USE_X, USE_Y, QVariantList() << "move");
    add_function(&TAudioPluginView::staticMetaObject,   &TMoveBase::staticMetaObject, "",                        "MovePlugin",           MovePluginCommand, "", USE_X, NO_Y, QVariantList() << "false");
    add_function(&TCurveView::staticMetaObject,         &TMoveBase::staticMetaObject, tr("Move Curve Node(s)"),  "MoveCurveNodes",       MoveCurveNodesCommand, "", USE_X, USE_Y);
    add_function(&TTrackView::staticMetaObject,         &TMoveBase::staticMetaObject, tr("Move Up/Down"),        "MoveTrack",            MoveTrackCommand, "", NO_X, USE_Y);
    add_function(&TTimeLineRulerView::staticMetaObject, &TMoveBase::staticMetaObject, tr("Move Marker"),         "TimeLineMoveMarker",   MoveMarkerCommand, "", USE_X, NO_Y);
    add_function(&TTimeLineMarkerView::staticMetaObject,&TMoveBase::staticMetaObject, tr("Move Marker"),         "MoveMarker",           MoveMarkerCommand, "", USE_X, NO_Y);

    add_function(&TSheet::staticMetaObject,         tr("Add Marker"),                       "SheetAddMarker",               AddMarkerCommand, "", NO_X, NO_Y, QVariantList() << MarkerLocation::CursorLocation);
    add_function(&TSheet::staticMetaObject,         tr("Add Marker at Playhead"),           "SheetAddMarkerAtPlayhead",     AddMarkerCommand, "", NO_X, NO_Y, QVariantList() << MarkerLocation::PlayCursor);
    add_function(&TSheet::staticMetaObject,         tr("Add Marker at Work Cursor"),        "SheetAddMarkerAtWorkCursor",   AddMarkerCommand, "", NO_X, NO_Y, QVariantList() << MarkerLocation::WorkCursor);
    add_function(&TTimeLineRuler::staticMetaObject, tr("Add Marker"),                       "TimeLineAddMarker",            AddMarkerCommand, "", NO_X, NO_Y, QVariantList() << MarkerLocation::CursorLocation);
    add_function(&TTimeLineRuler::staticMetaObject, tr("Add Marker at Playhead"),           "TimeLineAddMarkerAtPlayhead",  AddMarkerCommand, "", NO_X, NO_Y, QVariantList() << MarkerLocation::PlayCursor);
    add_function(&TTimeLineRuler::staticMetaObject, tr("Add Marker at Work Cursor"),        "TimeLineAddMarkerAtWorkCursor",AddMarkerCommand, "", NO_X, NO_Y, QVariantList() << MarkerLocation::WorkCursor);

    add_function(&TAudioClip::staticMetaObject,     tr("External Processing"),  "AudioClipExternalProcessing", AudioClipExternalProcessingCommand);

    add_function(&TAudioClip::staticMetaObject,     tr("(De)Select"),       "ClipSelectionSelect", ClipSelectionCommand, "", NO_X, NO_Y, QVariantList()<< "toggle_selected");
    add_function(&TAudioClipView::staticMetaObject, tr("Magnetic Cut"),     "CropClip",         CropClipCommand, "", USE_X, USE_Y);

    add_function(&TAudioClipView::staticMetaObject, tr("Adjust Length"),    "AudioClipFadeLength",  FadeRangeCommand, "", USE_X, NO_Y);
    add_function(&TAudioClipView::staticMetaObject, tr("Adjust Length Both"),    "AudioClipFadeLengthBoth",  FadeRangeCommand, "", USE_X, NO_Y, QVariantList() << "both");
    add_function(&TFadeCurveView::staticMetaObject, tr("Adjust Bend"),      "FadeCurveBend",    FadeCurveBendCommand, "", NO_X, USE_Y);
    add_function(&TFadeCurveView::staticMetaObject, tr("Adjust Strength"),  "FadeCurveStrenght",FadeCurveStrengthCommand, "", USE_X, NO_Y);

    add_function(&TAudioProcessingNode::staticMetaObject, tr("Gain Envelope"), "GainShowAutomation", GainShowAutomationCommand, "");

    add_function(&TAudioTrack::staticMetaObject,    tr("Import Audio"),     "ImportAudio",      ImportAudioCommand);

    add_function(&TAudioClip::staticMetaObject,     tr("Normalize Clip"),   "NormalizeClip",    NormalizeClipCommand);

    add_function(&TAudioClipView::staticMetaObject, tr("Copy"),             "CopyClip",         MoveClipCommand,    "", USE_X, USE_Y,   QVariantList() << "copy");
    add_function(&TAudioTrackView::staticMetaObject,tr("Fold Track"),       "FoldTrack",        MoveClipCommand,    "", USE_X, NO_Y,    QVariantList() << "fold_track");
    add_function(&TSheetView::staticMetaObject,     tr("Fold Sheet"),       "FoldSheet",        MoveClipCommand,    "", USE_X, USE_Y,   QVariantList() << "fold_sheet");
    add_function(&TTimeLineRulerView::staticMetaObject,   tr("Fold Markers"),     "FoldMarkers",      MoveClipCommand,    "", USE_X, NO_Y,    QVariantList() << "fold_markers");

    add_function(&TAudioClipView::staticMetaObject, tr("Move Edge"),        "MoveClipEdge",     MoveEdgeCommand,    "", USE_X, NO_Y,    QVariantList() << "false");

    m->add_base_function(&TResetBase::staticMetaObject,  tr("Reset"),       "ResetBase");

    add_function(&TSheetView::staticMetaObject,     tr("Shuttle"),          "Shuttle",          ShuttleCommand,     "", USE_X, USE_Y);

    add_function(&TAudioClipView::staticMetaObject, tr("Split"),            "SplitClip",        SplitAudioClipCommand,   "", USE_X, NO_Y);
    add_function(&TAudioClipView::staticMetaObject,tr("Dual Trim"),         "AudioClipDualTrim",AudioClipDualTrimCommand,"", USE_X, NO_Y);

    add_function(&TTrack::staticMetaObject,         tr("Track Pan"),        "TrackPan",         TrackPanCommand);
    add_function(&TPanKnobView::staticMetaObject,   tr("Pan"),              "PanKnobPanorama",  TrackPanCommand);

    add_function(&TMainWindow::staticMetaObject,    tr("Set Play Position"),"TransportSetPosition", TransportSetPositionCommand, "", USE_X, NO_Y);

    m->add_base_function(&TToggleBypassBase::staticMetaObject,              tr("Toggle Bypass"),            "ToggleBypassBase");
    m->add_base_function(&TToggleVerticalBase::staticMetaObject,            tr("Toggle Vertical"),          "ToggleVerticalBase");

    add_function(&TSheetView::staticMetaObject,     tr("Move Work Cursor"), "WorkCursorMove",   WorkCursorMoveCommand);

    add_function(&TSheetView::staticMetaObject,     tr("Zoom"),             "Zoom",             ZoomCommand,        "", USE_X, NO_Y,    QVariantList() << "HJogZoom" << "1.2" << "0.2");

    // ----------------------------------------------------------------------------------------------------------------------- //

    m->add_function(&TAudioClip::staticMetaObject,      tr("Lock"),             "AudioClipLock",                "lock()");
    m->add_function(&TAudioClipView::staticMetaObject,      &TEditPropertiesBase::staticMetaObject,         "EditAudioClipProperties", "edit_properties()");
    m->add_function(&TAudioClipView::staticMetaObject,  tr("Reset Audio File"), "AudioClipSetAudioFile",     "set_audio_file()");

    m->add_function(&TAudioPlugin::staticMetaObject,        &TToggleBypassBase::staticMetaObject,           "PluginToggleBypass", "toggle_bypass()");
    m->add_function(&TAudioPluginView::staticMetaObject,    &TEditPropertiesBase::staticMetaObject,         "EditPluginProperties", "edit_properties()");

    m->add_function(&TAudioProcessingNode::staticMetaObject, tr("Mute"),    "Mute",                         "mute()");

    m->add_function(&TAudioTrack::staticMetaObject, tr("Record: On/Off"),   "AudioTrackToggleRecord",       "toggle_arm()");

    m->add_function(&TAudioTrackView::staticMetaObject, tr("Insert Silence"), "AudioTrackInsertSilence",    "insert_silence()");

    m->add_function(&CropClip::staticMetaObject,    tr("Adjust Left"),      "CropClipAdjustLeft",           "adjust_left()");
    m->add_function(&CropClip::staticMetaObject,    tr("Adjust Right"),     "CropClipAdjustRight",          "adjust_right()");

    m->add_function(&CorrelationMeterView::staticMetaObject, tr("Toggle display range"), "CorrelationMeterToggleDisplayRange", "set_mode()");

    m->add_function(&TCurveView::staticMetaObject,   tr("New Node"),         "AddCurveNode",                 "add_node()");
    m->add_function(&TCurveView::staticMetaObject,   tr("Select All Nodes"), "CurveSelectAllNodes",          "toggle_select_all_nodes()");
    m->add_function(&TCurveView::staticMetaObject,   tr("Select Node"),      "CurveSelectNode",              "select_lazy_selected_node()");

    m->add_function(&TFadeCurve::staticMetaObject,  tr("Cycle Shape"),      "FadeCurveCycleShape",          "set_mode()");
    m->add_function(&TFadeCurve::staticMetaObject,  &TToggleBypassBase::staticMetaObject,                   "FadeCurveToggleBypass", "toggle_bypass()");
    m->add_function(&TFadeCurve::staticMetaObject,  tr("Toggle Raster"),    "FadeCurveToggleRaster",        "toggle_raster()");
    m->add_function(&TFadeCurveView::staticMetaObject, tr("Select Preset"), "FadeSelectPreset",             "select_fade_shape()");

    m->add_function(&TFadeRangeCommand::staticMetaObject, &TResetBase::staticMetaObject, "FadeResetLength",         "reset_length()");

    m->add_function(&TFaderGroupCommand::staticMetaObject, tr("Increase"),   "GainIncrease",                 "increase_gain()");
    m->add_function(&TFaderGroupCommand::staticMetaObject, tr("Decrease"),   "GainDecrease",                 "decrease_gain()");
    m->add_function(&TFaderGroupCommand::staticMetaObject, tr("Input dB value"), "GainNumericalInput",       "numerical_input()");
    m->add_function(&TFaderGroupCommand::staticMetaObject, tr("To Selection: On/Off"), "GainToggleSelection",    "toggle_primary_gain_only()");
    m->add_function(&TFaderGroupCommand::staticMetaObject, &TResetBase::staticMetaObject, "GainReset",       "reset_gain()");

    m->add_function(&TMainWindow::staticMetaObject, tr("Context Menu"),         "ShowContextMenu",          "show_context_menu()");
    m->add_function(&TMainWindow::staticMetaObject, tr("New Track Dialog"),     "ShowNewTrackDialog",       "show_newtrack_dialog()");
    m->add_function(&TMainWindow::staticMetaObject, tr("Activate Track Finder"), "MainWindowActivateTrackFinder", "show_track_finder()");
    m->add_function(&TMainWindow::staticMetaObject, tr("Browse to first Track in current View"), "MainWindowNavigateToFirstTrack", "browse_to_first_track_in_active_sheet()");
    m->add_function(&TMainWindow::staticMetaObject, tr("Browse to last Track in current View"), "MainWindowNavigateToLastTrack", "browse_to_last_track_in_active_sheet()");
    m->add_function(&TMainWindow::staticMetaObject, tr("Full Screen"),          "MainWindowShowFullScreen",     "full_screen()");
    m->add_function(&TMainWindow::staticMetaObject, tr("Show Project Management Dialog"), "MainWindowShowProjectManagementDialog", "show_project_manager_dialog()");
    m->add_function(&TMainWindow::staticMetaObject, tr("Show Help"), "ShowHelp", "quick_start()");

    m->add_function(&MoveClip::staticMetaObject, &TToggleVerticalBase::staticMetaObject, "MoveClipToggleVerticalOnly", "toggle_vertical_only()");

    // m->add_function(&TMoveCommand::staticMetaObject, tr("Move Left"),       "MoveCommandLeft",              "move_left()");
    // m->add_function(&TMoveCommand::staticMetaObject, tr("Move Right"),      "MoveCommandRight",             "move_right()");
    m->add_function(&TMoveCommand::staticMetaObject, tr("Move Up"),         "MoveCommandUp",                "move_up()");
    m->add_function(&TMoveCommand::staticMetaObject, tr("Move Down"),       "MoveCommandDown",              "move_down()");
    m->add_function(&TMoveCommand::staticMetaObject, tr("Move Faster"),     "MoveCommandFaster",            "move_faster()");
    m->add_function(&TMoveCommand::staticMetaObject, tr("Move Slower"),     "MoveCommandSlower",            "move_slower()");
    m->add_function(&TMoveCommand::staticMetaObject, tr("Moving Speed"),    "MoveCommandSpeed",             "numerical_input()");
    m->add_function(&TMoveCommand::staticMetaObject, tr("Snap On/Off"),     "MoveCommandToggleSnap",        "toggle_snap_on_off()");

    // m->add_function(&TCommandController::staticMetaObject, tr("Move Left"),       "MoveCommandLeft",              "move_left()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Move Right"),      "MoveCommandRight",             "move_right()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Move Up"),         "MoveCommandUp",                "move_up()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Move Down"),       "MoveCommandDown",              "move_down()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Move Faster"),     "MoveCommandFaster",            "move_faster()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Move Slower"),     "MoveCommandSlower",            "move_slower()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Moving Speed"),    "MoveCommandSpeed",             "numerical_input()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Snap On/Off"),     "MoveCommandToggleSnap",        "toggle_snap_on_off()");


    m->add_function(&MoveCurveNode::staticMetaObject, &TToggleVerticalBase::staticMetaObject, "MoveCurveNodeToggleVerticalOnly", "toggle_vertical_only()");

    m->add_function(&MoveTrack::staticMetaObject,   tr("Move Down"),        "MoveTrackDown",                "move_down()");
    m->add_function(&MoveTrack::staticMetaObject,   tr("To Top"),           "MoveTrackToTop",               "to_top()");
    m->add_function(&MoveTrack::staticMetaObject,   tr("To Bottom"),        "MoveTrackToBottom",            "to_bottom()");

    m->add_function(&PlayHeadMove::staticMetaObject, tr("To Start"),        "PlayHeadMoveToStart",          "move_to_start()");
    m->add_function(&PlayHeadMove::staticMetaObject, tr("To Work Cursor"),  "PlayHeadMoveToWorkCursor",     "move_to_work_cursor()");

    m->add_function(&TProjectManager::staticMetaObject, tr("Save Project"), "ProjectSave",                  "save_project()");

    m->add_function(&TSheetView::staticMetaObject,  &TEditPropertiesBase::staticMetaObject, "EditSongProperties",           "edit_properties()");
    m->add_function(&TSheetView::staticMetaObject,  tr("Up"),               "ViewScrollUp",                 "scroll_up()");
    m->add_function(&TSheetView::staticMetaObject,  tr("Down"),             "ViewScrollDown",               "scroll_down()");
    m->add_function(&TSheetView::staticMetaObject,  tr("Set"),              "WorkCursorTouch",              "touch()");
    m->add_function(&TSheetView::staticMetaObject,  tr("To start"),         "WorkCursorToStart",            "goto_begin()");
    m->add_function(&TSheetView::staticMetaObject,  tr("To end"),           "WorkCursorToEnd",              "goto_end()");
    m->add_function(&TSheetView::staticMetaObject,  tr("Set"),              "SheetSetPlayPosition",         "touch_play_cursor()");
    m->add_function(&TSheetView::staticMetaObject,  tr("Center"),           "SheetCenterPlayhead",          "center_playhead()");
    m->add_function(&TSheetView::staticMetaObject,  tr("One Layer Up"),     "NavigateToUpperContext",       "to_upper_context_level()");
    m->add_function(&TSheetView::staticMetaObject,  tr("One Layer Down"),   "NavigateToLowerContext",       "to_lower_context_level()");
    m->add_function(&TSheetView::staticMetaObject,  tr("To Timeline"),      "NavigateToTimeLine",           "browse_to_time_line()");

    m->add_function(&TShortCutManager::staticMetaObject, tr("Export keymap"), "ExportShortcutMap",          "export_keymap()");

    m->add_function(&SpectralMeterView::staticMetaObject, &TEditPropertiesBase::staticMetaObject,   "EditSpectralMeterProperties",      "edit_properties()");
    m->add_function(&SpectralMeterView::staticMetaObject, tr("Reset average curve"),                "SpectralMeterResetAverageCurve",   "reset()");
    m->add_function(&SpectralMeterView::staticMetaObject, tr("Toggle average curve"),               "SpectralMeterToggleDisplayRange",  "set_mode()");

    m->add_function(&TTimeLineRulerView::staticMetaObject, tr("Playhead to Marker"),              "TimeLinePlayheadToMarker",         "playhead_to_marker()");
    m->add_function(&TTimeLineRulerView::staticMetaObject, tr("Edit Markers"),                    "TimeLineShowMarkerDialog",         "show_marker_dialog()");

    m->add_function(&TTrack::staticMetaObject,      tr("Solo"),             "Solo",                         "solo()");
    m->add_function(&TTrackView::staticMetaObject,   tr("Add new Plugin"),   "TrackAddPlugin",               "add_new_plugin()");

    m->add_function(&TPanKnobView::staticMetaObject, tr("Pan to Left"),     "PanKnobPanLeft",               "pan_left()");
    m->add_function(&TPanKnobView::staticMetaObject, tr("Pan to Right"),    "PanKnobPanRight",              "pan_right()");
    m->add_function(&TrackPan::staticMetaObject,     tr("Pan to Left"),     "TrackPanLeft",                 "pan_left()");
    m->add_function(&TrackPan::staticMetaObject,     tr("Pan to Right"),    "TrackPanRight",                "pan_right()");
    m->add_function(&TrackPan::staticMetaObject, &TResetBase::staticMetaObject, "TrackPanReset",            "reset_pan()");

    m->add_function(&TrackPanelGain::staticMetaObject, tr("Increase"),      "TrackPanelGainIncrement",      "gain_increment()");
    m->add_function(&TrackPanelGain::staticMetaObject, tr("Decrease"),      "TrackPanelGainDecrement",      "gain_decrement()");

    m->add_function(&TrackPanelLed::staticMetaObject,   tr("Toggle On/Off"), "PanelLedToggle",              "toggle()");

    m->add_function(&TTransport::staticMetaObject,  tr("Play (Start/Stop)"),"TransportPlayStartStop",       "start_transport()");
    m->add_function(&TTransport::staticMetaObject,  tr("Start Recording"),  "TransportSetRecordingPlayStart","set_recordable_and_start_transport()");
    m->add_function(&TTransport::staticMetaObject,  tr("To start"),         "TransportToStart",             "to_start()");
    m->add_function(&TTransport::staticMetaObject,  tr("To end"),           "TransportToEnd",               "to_end()");

    m->add_function(&WorkCursorMove::staticMetaObject, tr("To Playhead"),   "WorkCursorMoveToPlayhead",     "move_to_play_cursor()");
    m->add_function(&WorkCursorMove::staticMetaObject, tr("To Start"),      "WorkCursorMoveToStart",        "move_to_start()");

    m->add_function(&TZoomCommand::staticMetaObject,        tr("In"),               "ZoomIn",                       "hzoom_in()");
    m->add_function(&TZoomCommand::staticMetaObject,        tr("Out"),              "ZoomOut",                      "hzoom_out()");
    m->add_function(&TZoomCommand::staticMetaObject,        tr("Track Vertical Zoom In"),   "ZoomTrackVerticalIn",  "track_vzoom_in()");
    m->add_function(&TZoomCommand::staticMetaObject,        tr("Track Vertical Zoom Out"),  "ZoomTrackVerticalOut", "track_vzoom_out()");
    m->add_function(&TZoomCommand::staticMetaObject,        tr("Track Height"),             "ZoomNumericalInput",   "numerical_input()");
    m->add_function(&TZoomCommand::staticMetaObject,        tr("Expand/Collapse Tracks"),   "ZoomToggleExpandAllTracks", "toggle_expand_all_tracks()");
    m->add_function(&TZoomCommand::staticMetaObject, &TToggleVerticalBase::staticMetaObject, "ZoomToggleVerticalHorizontal", "toggle_vertical_horizontal_jog_zoom()");
}


TShortCutFunction* TraversoCommands::add_function(const QMetaObject *metaObject,
                                                  const QMetaObject *basedMetaObject,
                                                  const QString &description,
                                                  const char *commandName,
                                                  TraversoCommand command,
                                                  const char *slotSignature,
                                                  bool useX,
                                                  bool useY,
                                                  const QVariantList &args)
{
    auto function = m_manager->add_function(metaObject, description, commandName, slotSignature);

    if (!function) {
        PERROR(QString("TraversoCommands::add_function: Could not register function for %1").arg(commandName));
        return nullptr;
    }

    function->set_plugin_name("TraversoCommands");

    if (basedMetaObject) {
        function->set_base_metaobject(basedMetaObject);
    }
    function->set_use_x(useX);
    function->set_use_y(useY);
    function->set_arguments(args);

    m_dict.insert(function->get_command_name(), command);

    return function;

}

TCommand* TraversoCommands::create(QObject* obj, const QString& commandName, QVariantList arguments)
{
    TraversoCommand command = static_cast<TraversoCommand>(m_dict.value(commandName, NoCommand));

    switch (command) {
    case AddMarkerCommand:
    {
        TTimeLineRuler* timeLineRuler = qobject_cast<TTimeLineRuler*>(obj);
        TSession* session;
        if (timeLineRuler) {
            session = timeLineRuler->get_sheet();
        } else {
            session = qobject_cast<TSheet*>(obj);
            Q_ASSERT(session);
            timeLineRuler = session->get_timeline_ruler();
        }

        Q_ASSERT(timeLineRuler);

        Q_ASSERT(arguments.size() == 1);
        MarkerLocation markerLocation = static_cast<MarkerLocation>(arguments.at(0).toInt());
        switch (markerLocation) {
        case CursorLocation:
            return timeLineRuler->add_marker_at(cpointer().on_first_input_event_timeref_location());
        case PlayCursor:
            return timeLineRuler->add_marker_at(session->get_transport_location());
        case WorkCursor:
            return timeLineRuler->add_marker_at(session->get_work_location());
        default: return nullptr;
        }
        return nullptr;
    }
    case TransportSetPositionCommand:
    {
        TProject* project = pm().get_project();
        if (!project) {
            return ied().failure();
        }

        TSheet* sheet = project->get_active_sheet();
        if (!sheet) {
            return ied().failure();
        }

        TSheetWidget* widget = TMainWindow::instance()->getCurrentSheetWidget();
        if (widget)
        {
            return new PlayHeadMove(widget->get_sheetview());

        }
        return ied().failure();

    }
    case GainCommand:
    {
        TContextItem* contextItem = qobject_cast<TContextItem*>(obj);
        TAudioProcessingNode* audioProcessingNode = nullptr;
        Q_ASSERT(contextItem);

        if (contextItem->metaObject() == &TrackPanelGain::staticMetaObject) {
            contextItem = contextItem->get_related_context_item();
        } else if (TAudioClipView* view = qobject_cast<TAudioClipView*>(contextItem)) {
            contextItem = view->get_related_context_item();
        } else if (TTrackView* view = qobject_cast<TTrackView*>(contextItem)) {
            contextItem = view->get_related_context_item();
        }

        audioProcessingNode = qobject_cast<TAudioProcessingNode*>(contextItem);


        if (!audioProcessingNode) {
            PERROR("TraversoCommands: Supplied object was not a TAudioProcessingNode, "
                   "GainCommand only works with TAudioProcessingNode objects!!");
            return nullptr;
        }

        auto group = new TFaderGroupCommand(contextItem);

        TAudioClip* clip = qobject_cast<TAudioClip*>(contextItem);
        if (clip && clip->is_selected()) {

            QList<TAudioClip* > selection;
            clip->get_sheet()->get_audioclip_manager()->get_selected_clips(selection);

            // always the contextitem first so it will be the primary gain object
            group->add_audio_processing_node(audioProcessingNode, arguments);

            for(auto node : selection) {
                // only add the context item once
                if (node == audioProcessingNode) {
                    continue;
                }
                group->add_audio_processing_node(node, arguments);
            }
        } else {
            group->add_audio_processing_node(audioProcessingNode, arguments);
        }


        return group;
    }

    case TrackPanCommand:
    {
        TTrack* track = qobject_cast<TTrack*>(obj);
        if (! track) {
            TPanKnobView* knob = qobject_cast<TPanKnobView*>(obj);
            if(knob) {
                track = knob->get_track();
            }
            if (!track) {
                PERROR("TraversoCommands: Supplied QObject was not a Track! "
                       "TrackPanCommand needs a Track as argument");
                return nullptr;
            }
        }
        return new TrackPan(track, arguments);
    }

    case ImportAudioCommand:
    {
        TAudioTrack* track = qobject_cast<TAudioTrack*>(obj);
        Q_ASSERT(track);

        auto audioFileImportCommand = new TAudioFileImportCommand(track);
        audioFileImportCommand->set_track(track);
        return audioFileImportCommand;
    }

    case InsertSilenceCommand:
    {
        TAudioTrack* track = qobject_cast<TAudioTrack*>(obj);
        Q_ASSERT(track);

        TTimeRef length(10*TTimeRef::UNIVERSAL_SAMPLE_RATE);
        auto audioFileImportCommand = new TAudioFileImportCommand(track);
        audioFileImportCommand->set_track(track);
        audioFileImportCommand->set_length(length);
        audioFileImportCommand->set_silent(true);
        return audioFileImportCommand;
    }

    case AddNewAudioTrackCommand:
    {
        TSheet* sheet = qobject_cast<TSheet*>(obj);
        Q_ASSERT(sheet);
        return sheet->add_track(new TAudioTrack(sheet, "Unnamed", TAudioTrack::INITIAL_HEIGHT));
    }

    case RemoveClipCommand:
    {
        TAudioClip* audioClip = qobject_cast<TAudioClip*>(obj);
        CommandGroup* group = new CommandGroup(audioClip->get_sheet(), tr("Remove Clip"));
        Q_ASSERT(audioClip);
        QList<TAudioClip*> selectedClips;
        if (audioClip->is_selected()) {
             audioClip->get_sheet()->get_audioclip_manager()->get_selected_clips(selectedClips);
            group->setText(tr("Remove Selected Clips"));
        } else {
            selectedClips.append(audioClip);
        }

        for(TAudioClip* clip : std::as_const(selectedClips)) {
            TAudioClipAddRemoveSpec spec;
            spec.set_clip(clip);
            spec.set_is_historable(false);
            spec.set_is_move(false);
            group->add_command(clip->get_track()->remove_clip(spec));
        }

        return group;
    }

    case RemoveTrackCommand:
    {
        TTrack* track = qobject_cast<TTrack*>(obj);
        if (!track) {
            PERROR("TraversoCommands: Supplied QObject was not a Track! "
                   "RemoveTrackCommand needs a Track as argument");
            return nullptr;
        }

        TSession* activeSession = pm().get_project()->get_current_session();
        if (!activeSession) {
            // this is rather impossible!!
            tInformUser().information(tr("Removing Track %1, but no active (Work) Sheet ??").arg(track->get_name()));
            return nullptr;
        }

        if (track == activeSession->get_master_out_bus_track()) {
            tInformUser().information(tr("It is not possible to remove the Master Out track!"));
            return nullptr;
        }
        return activeSession->remove_track(track);
    }

    case RemovePluginCommand:
    {
        TAudioPluginView* audioPluginView = qobject_cast<TAudioPluginView*>(obj);
        Q_ASSERT(audioPluginView);
        return audioPluginView->remove_plugin();
    }

    case RemoveCurveNodeCommmand:
    {
        TCurveView* curveView = qobject_cast<TCurveView*>(obj);
        Q_ASSERT(curveView);
        return curveView->remove_node();
    }
    case RemoveTimeLineRulerMarkerCommand:
    {
        TTimeLineRulerView* timeLineRulerView = qobject_cast<TTimeLineRulerView*>(obj);
        Q_ASSERT(timeLineRulerView);
        return timeLineRulerView->remove_marker();
    }
    case AudioClipExternalProcessingCommand:
    {
        TAudioClip* audioClip = qobject_cast<TAudioClip*>(obj);
        Q_ASSERT(audioClip);
        return new AudioClipExternalProcessing(audioClip);
    }

    case ClipSelectionCommand:
    {
        TSheet* sheet = qobject_cast<TSheet*>(obj);
        if (sheet) {
            QString action;
            if (!arguments.empty()) {
                action = arguments.at(0).toString();
                if (action == "select_all_clips") {
                    return sheet->get_audioclip_manager()->select_all_clips();
                }
            }
        }
        TAudioClip* clip = qobject_cast<TAudioClip*>(obj);
        if (!clip) {
            PERROR("TraversoCommands: Supplied QObject was not an AudioClip! "
                   "ClipSelectionCommand needs an AudioClip as argument");
            return nullptr;
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
        TViewItem* viewItem = qobject_cast<TViewItem*>(obj);
        Q_ASSERT(viewItem);
        return new MoveClip(viewItem, arguments);
    }

    case MoveTrackCommand:
    {
        TTrackView* trackView = qobject_cast<TTrackView*>(obj);
        Q_ASSERT(trackView);
        return new MoveTrack(trackView);
    }

    case MovePluginCommand:
    {
        TAudioPluginView* audioPluginView = qobject_cast<TAudioPluginView*>(obj);
        Q_ASSERT(audioPluginView);
        return new MovePlugin(audioPluginView);
    }

    case MoveEdgeCommand:
    {
        TAudioClipView* audioClipView = qobject_cast<TAudioClipView*>(obj);
        Q_ASSERT(audioClipView);

        if (audioClipView->is_left_from_center(cpointer().on_first_input_event_scene_x())) {
            return new MoveEdge(audioClipView, audioClipView->get_sheetview(), "set_left_edge");
        } else {
            return new MoveEdge(audioClipView, audioClipView->get_sheetview(), "set_right_edge");
        }
    }

    case AudioClipDualTrimCommand:
    {
        TAudioClipView* audioClipView = qobject_cast<TAudioClipView*>(obj);
        Q_ASSERT(audioClipView);

        return new TAudioClipDualTrimCommand(audioClipView->get_sheetview(), audioClipView->get_clip()->get_track());
    }

    case CropClipCommand:
    {
        TAudioClipView* audioClipView = qobject_cast<TAudioClipView*>(obj);
        Q_ASSERT(audioClipView);
        return new CropClip(audioClipView);
    }

    case ArmTracksCommand:
    {
        TSheetView* sheetView = qobject_cast<TSheetView*>(obj);
        Q_ASSERT(sheetView);
        return new ArmTracks(sheetView);
    }

    case ZoomCommand:
    {
        TSheetView* sheetView = qobject_cast<TSheetView*>(obj);
        Q_ASSERT(sheetView);
        return new TZoomCommand(sheetView, arguments);
    }

    case WorkCursorMoveCommand:
    {
        TSheetView* sheetView = qobject_cast<TSheetView*>(obj);
        Q_ASSERT(sheetView);
        return new WorkCursorMove(sheetView);
    }

    case MoveCurveNodesCommand:
    {
        TCurveView* curveView = qobject_cast<TCurveView*>(obj);
        Q_ASSERT(curveView);
        return curveView->drag_node();
    }

    case ArrowKeyBrowserCommand:
    {
        TSheetView* sheetView = qobject_cast<TSheetView*>(obj);
        Q_ASSERT(sheetView);
        return new ArrowKeyBrowser(sheetView);
    }

    case ShuttleCommand:
    {
        TSheetView* sheetView = qobject_cast<TSheetView*>(obj);
        Q_ASSERT(sheetView);
        return new TMoveCommand(sheetView, nullptr, "");
    }

    case NormalizeClipCommand:
    {
        TAudioClip* audioClip = qobject_cast<TAudioClip*>(obj);
        Q_ASSERT(audioClip);

        bool ok;
        double requestedNormFactor = QInputDialog::getDouble(0, tr("Normalization"),
                                                             tr("Set Normalization level:"), 0.0, -120, 0, 1, &ok);

        if (!ok) {
            return nullptr;
        }

        if (audioClip->is_selected()) {
            QList<TAudioClip* > selection;
            audioClip->get_sheet()->get_audioclip_manager()->get_selected_clips(selection);

            CommandGroup* group = new CommandGroup(audioClip, tr("Normalize Selected Clips"));

            for(TAudioClip* selectedClip : selection) {
                group->add_command(new PCommand(selectedClip, "set_gain",
                                                selectedClip->calculate_normalization_factor(requestedNormFactor),
                                                selectedClip->get_gain(),
                                                tr("AudioClip: Normalize")));
            }

            return group;
        }

        return new PCommand(audioClip, "set_gain", audioClip->calculate_normalization_factor(requestedNormFactor), audioClip->get_gain(), tr("AudioClip: Normalize"));
    }
    case MoveMarkerCommand:
    {
        if (auto view = qobject_cast<TTimeLineRulerView*>(obj)) {
            auto markerView = view->get_soft_selected_marker_view();
            if (!markerView) {
                return ied().failure();
            }
            return new MoveMarker(markerView, view->get_sheetview()->timeref_scalefactor, tr("Move Marker"));
        }

        if (auto markerView = qobject_cast<TTimeLineMarkerView*>(obj)) {
            return new MoveMarker(markerView, markerView->get_sheetview()->timeref_scalefactor, tr("Move Marker"));
        }

        return ied().failure();
    }
    case FadeRangeCommand:
    {
        TAudioClipView* audioClipView = qobject_cast<TAudioClipView*>(obj);
        Q_ASSERT(audioClipView);

        TAudioClip* clip = audioClipView->get_clip();
        if (arguments.size() > 0 && arguments.at(0).toString() == "both") {
            return new TFadeRangeCommand(clip, clip->get_fade_in(), clip->get_fade_out(), audioClipView->get_sheetview()->timeref_scalefactor);
        }

        if (audioClipView->is_left_from_center(cpointer().on_first_input_event_scene_x())) {
            return new TFadeRangeCommand(clip, clip->get_fade_in(), nullptr, audioClipView->get_sheetview()->timeref_scalefactor);
        }

        return new TFadeRangeCommand(clip, nullptr, clip->get_fade_out(), audioClipView->get_sheetview()->timeref_scalefactor);
    }
    case FadeCurveBendCommand:
    {
        TFadeCurveView* fadeCurveView = qobject_cast<TFadeCurveView*>(obj);
        Q_ASSERT(fadeCurveView);
        return new FadeBend(fadeCurveView);
    }
    case FadeCurveStrengthCommand:
    {
        TFadeCurveView* fadeCurveView = qobject_cast<TFadeCurveView*>(obj);
        Q_ASSERT(fadeCurveView);
        return new FadeStrength(fadeCurveView);
    }
    case GainShowAutomationCommand:
    {
        TAudioClip* audioClip = qobject_cast<TAudioClip*>(obj);
        if (audioClip) {
            audioClip->get_track()->toggle_show_clip_volume_automation();
            return ied().succes();
        }
        TTrack* track = qobject_cast<TTrack*>(obj);
        if (track) {
            track->toggle_show_gain_automation_curve();
            return ied().succes();
        }
        return ied().failure();
    }

    case NoCommand:
        PERROR(QString("My dictionary does not know this command: %1").arg(commandName));
        break;
    }

    return ied().did_not_implement();
}

TCommandController *TraversoCommands::create_controller(QObject *obj, const QString &commandName, QVariantList arguments)
{
    TraversoCommand command = static_cast<TraversoCommand>(m_dict.value(commandName, NoCommand));

    switch (command) {
    case SplitAudioClipCommand:
    {
        TAudioClipView* audioClipView = qobject_cast<TAudioClipView*>(obj);
        Q_ASSERT(audioClipView);

        TAudioClip* clip = audioClipView->get_clip();
        Q_ASSERT(clip);

        TSheet* sheet = clip->get_sheet();
        Q_ASSERT(sheet);

        auto* splitController = new TSplitEntityController(clip, sheet);

        audioClipView->connect_interaction_controller(splitController);

        return splitController;
    }

    }

    return nullptr;

}

// eof
