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

#include <QMetaEnum>

#include <QInputDialog>
#include <QStringList>

#include <QFileDialog>
#include <QFileInfo>
#include "TContextPointer.h"
#include "TPositionIndicator.h"
#include "TAddClipsController.h"
#include "TAudioClipFactory.h"
#include "TRemoveEntitiesController.h"
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

    m->add_group(TActionGroups::TNavigationBaseUp, &TNavigationBaseUp::staticMetaObject);
    m->add_group(TActionGroups::TNavigationBaseDown, &TNavigationBaseDown::staticMetaObject);
    m->add_group(TActionGroups::TNavigationBaseLeft, &TNavigationBaseLeft::staticMetaObject);
    m->add_group(TActionGroups::TNavigationBaseRight, &TNavigationBaseRight::staticMetaObject);

    // Hold Commands need 1 key to start, and then additional keys for specific functionality
    // For features like navigating with arrow keys this is less productive, so we register the navigation
    // shortcut function to start autorepeating on itself.
    // We have to register the navigation keys twice, one to start the hold command and one for the
    // the autorepeat slot to get invoked.
    TShortCutFunction* shortCutFunction{nullptr};
    shortCutFunction = add_function(&TSheetView::staticMetaObject, TActions::ArrowKeyBrowserStartUp, TActionGroups::TNavigationBaseUp, ArrowKeyBrowserCommand, NO_X, USE_Y);
    Q_ASSERT(shortCutFunction);
    shortCutFunction->set_auto_repeats_on_itself(true);

    shortCutFunction = add_function(&TSheetView::staticMetaObject, TActions::ArrowKeyBrowserStartDown, TActionGroups::TNavigationBaseDown, ArrowKeyBrowserCommand, NO_X, USE_Y);
    Q_ASSERT(shortCutFunction);
    shortCutFunction->set_auto_repeats_on_itself(true);

    shortCutFunction = add_function(&TSheetView::staticMetaObject, TActions::ArrowKeyBrowserStartLeft, TActionGroups::TNavigationBaseLeft, ArrowKeyBrowserCommand, USE_X, NO_Y);
    Q_ASSERT(shortCutFunction);
    shortCutFunction->set_auto_repeats_on_itself(true);

    shortCutFunction = add_function(&TSheetView::staticMetaObject, TActions::ArrowKeyBrowserStartRight, TActionGroups::TNavigationBaseRight, ArrowKeyBrowserCommand, USE_X, NO_Y);
    Q_ASSERT(shortCutFunction);
    shortCutFunction->set_auto_repeats_on_itself(true);

    // ArrowKeyBrowserCommand will create an ArrowKeyBrowser Command, and since that will start autorepeat the same key
    // we can now register a new shortcut function to dispatch on the ArrowKeyBrowser Command object
    m->add_function<ArrowKeyBrowser>(TActions::ArrowKeyBrowserUp, TActionGroups::TNavigationBaseUp, &ArrowKeyBrowser::up);
    m->add_function<ArrowKeyBrowser>(TActions::ArrowKeyBrowserDown, TActionGroups::TNavigationBaseDown, &ArrowKeyBrowser::down);
    m->add_function<ArrowKeyBrowser>(TActions::ArrowKeyBrowserLeft, TActionGroups::TNavigationBaseLeft, &ArrowKeyBrowser::left);
    m->add_function<ArrowKeyBrowser>(TActions::ArrowKeyBrowserRight, TActionGroups::TNavigationBaseRight, &ArrowKeyBrowser::right);

    m->add_group(TActionGroups::DeleteBase, &TDeleteBase::staticMetaObject);

    add_function(&TAudioClip::staticMetaObject, TActions::RemoveClip, TActionGroups::DeleteBase, RemoveClipCommand);
    add_function(&TAudioPluginView::staticMetaObject, TActions::RemovePlugin, TActionGroups::DeleteBase, RemovePluginCommand);
    add_function(&TCurveView::staticMetaObject, TActions::RemoveCurveNode, TActionGroups::DeleteBase, RemoveCurveNodeCommmand);
    add_function(&TTrack::staticMetaObject, TActions::RemoveTrack, TActionGroups::DeleteBase, RemoveTrackCommand);
    add_function(&TTimeLineRulerView::staticMetaObject, TActions::RemoveTimeLineRulerMarker, TActionGroups::DeleteBase, RemoveTimeLineRulerMarkerCommand);

    m->add_group(TActionGroups::EditPropertiesBase, &TEditPropertiesBase::staticMetaObject);

    m->add_function<TTrackView>(TActions::EditTrackProperties, TActionGroups::EditPropertiesBase, &TTrackView::edit_properties);

    m->add_group(TActionGroups::GainBase, &TGainBase::staticMetaObject);

    add_function(&TAudioClip::staticMetaObject, TActions::AudioClipGain, TActionGroups::GainBase, GainCommand);
    add_function(&TAudioTrack::staticMetaObject, TActions::AudioTrackGain, TActionGroups::GainBase, GainCommand);
    add_function(&TBusTrack::staticMetaObject, TActions::BusTrackGain, TActionGroups::GainBase, GainCommand);

    m->add_group(TActionGroups::MoveBase, &TMoveBase::staticMetaObject);

    add_function(&TAudioClipView::staticMetaObject, TActions::MoveClip, TActionGroups::MoveBase, MoveClipCommand, USE_X, USE_Y, QVariantList() << "move");
    add_function(&TAudioPluginView::staticMetaObject, TActions::MovePlugin, TActionGroups::MoveBase, MovePluginCommand, USE_X, NO_Y, QVariantList() << "false");
    add_function(&TCurveView::staticMetaObject, TActions::MoveCurveNodes, TActionGroups::MoveBase, MoveCurveNodesCommand, USE_X, USE_Y);
    add_function(&TTrackView::staticMetaObject, TActions::MoveTrack, TActionGroups::MoveBase, MoveTrackCommand, NO_X, USE_Y);
    add_function(&TTimeLineRulerView::staticMetaObject, TActions::TimeLineMoveMarker, TActionGroups::MoveBase, MoveMarkerCommand, USE_X, NO_Y);
    add_function(&TTimeLineMarkerView::staticMetaObject, TActions::MoveMarker, TActionGroups::MoveBase, MoveMarkerCommand, USE_X, NO_Y);

    add_function(&TSheet::staticMetaObject, TActions::SheetAddMarker, AddMarkerCommand, NO_X, NO_Y, QVariantList() << MarkerLocation::CursorLocation);
    add_function(&TSheet::staticMetaObject, TActions::SheetAddMarkerAtPlayhead, AddMarkerCommand, NO_X, NO_Y, QVariantList() << MarkerLocation::PlayCursor);
    add_function(&TSheet::staticMetaObject, TActions::SheetAddMarkerAtWorkCursor, AddMarkerCommand, NO_X, NO_Y, QVariantList() << MarkerLocation::WorkCursor);
    add_function(&TTimeLineRuler::staticMetaObject, TActions::TimeLineAddMarker, AddMarkerCommand, NO_X, NO_Y, QVariantList() << MarkerLocation::CursorLocation);
    add_function(&TTimeLineRuler::staticMetaObject, TActions::TimeLineAddMarkerAtPlayhead, AddMarkerCommand, NO_X, NO_Y, QVariantList() << MarkerLocation::PlayCursor);
    add_function(&TTimeLineRuler::staticMetaObject, TActions::TimeLineAddMarkerAtWorkCursor, AddMarkerCommand, NO_X, NO_Y, QVariantList() << MarkerLocation::WorkCursor);

    add_function(&TAudioClip::staticMetaObject, TActions::AudioClipExternalProcessing, AudioClipExternalProcessingCommand);

    add_function(&TAudioClip::staticMetaObject, TActions::ClipSelectionSelect, ClipSelectionCommand, NO_X, NO_Y, QVariantList()<< "toggle_selected");
    add_function(&TAudioClipView::staticMetaObject, TActions::CropClip, CropClipCommand, USE_X, USE_Y);

    add_function(&TAudioClipView::staticMetaObject, TActions::AudioClipFadeLength, FadeRangeCommand, USE_X, NO_Y);
    add_function(&TAudioClipView::staticMetaObject, TActions::AudioClipFadeLengthBoth, FadeRangeCommand, USE_X, NO_Y, QVariantList() << "both");
    add_function(&TFadeCurveView::staticMetaObject, TActions::FadeLength, FadeRangeCommand, USE_X, NO_Y);
    add_function(&TFadeCurveView::staticMetaObject, TActions::FadeCurveBend, FadeCurveBendCommand, NO_X, USE_Y);
    add_function(&TFadeCurveView::staticMetaObject, TActions::FadeCurveStrenght, FadeCurveStrengthCommand, USE_X, NO_Y);

    add_function(&TAudioProcessingNode::staticMetaObject, TActions::GainShowAutomation, GainShowAutomationCommand);

    add_function(&TAudioTrack::staticMetaObject, TActions::ImportAudio, ImportAudioCommand);

    add_function(&TAudioClip::staticMetaObject, TActions::NormalizeClip, NormalizeClipCommand);

    add_function(&TAudioClipView::staticMetaObject, TActions::CopyClip, MoveClipCommand, USE_X, USE_Y,   QVariantList() << "copy");
    add_function(&TAudioTrackView::staticMetaObject, TActions::FoldTrack, MoveClipCommand, USE_X, NO_Y,    QVariantList() << "fold_track");
    add_function(&TSheetView::staticMetaObject, TActions::FoldSheet, MoveClipCommand, USE_X, USE_Y,   QVariantList() << "fold_sheet");
    add_function(&TTimeLineRulerView::staticMetaObject, TActions::FoldMarkers, MoveClipCommand, USE_X, NO_Y,    QVariantList() << "fold_markers");

    add_function(&TAudioClipView::staticMetaObject, TActions::MoveClipEdge, MoveEdgeCommand, USE_X, NO_Y,    QVariantList() << "false");

    m->add_group(TActionGroups::ResetBase, &TResetBase::staticMetaObject);

    add_function(&TSheetView::staticMetaObject, TActions::Shuttle, ShuttleCommand, USE_X, USE_Y);

    add_function(&TAudioClipView::staticMetaObject, TActions::SplitClip, SplitAudioClipCommand, USE_X, NO_Y);
    add_function(&TAudioClipView::staticMetaObject, TActions::AudioClipDualTrim, AudioClipDualTrimCommand, USE_X, NO_Y);

    add_function(&TTrack::staticMetaObject, TActions::TrackPan, TrackPanCommand);
    add_function(&TPanKnobView::staticMetaObject, TActions::PanKnobPanorama, TrackPanCommand);

    add_function(&TMainWindow::staticMetaObject, TActions::TransportSetPosition, TransportSetPositionCommand, USE_X, NO_Y);

    m->add_group(TActionGroups::ToggleBypassBase, &TToggleBypassBase::staticMetaObject);
    m->add_group(TActionGroups::ToggleVerticalBase, &TToggleVerticalBase::staticMetaObject);

    add_function(&TSheetView::staticMetaObject, TActions::WorkCursorMove, WorkCursorMoveCommand);

    add_function(&TSheetView::staticMetaObject, TActions::Zoom, ZoomCommand, USE_X, NO_Y,    QVariantList() << "HJogZoom" << "1.2" << "0.2");
    add_function(&TSheetView::staticMetaObject, TActions::ZoomVertical, ZoomCommand, NO_X, USE_Y, QVariantList() << "VJogZoom" << "1.2" << "0.2");

    // ----------------------------------------------------------------------------------------------------------------------- //

    m->add_function<TAudioClip>(TActions::AudioClipLock, &TAudioClip::lock);
    m->add_function<TAudioClipView>(TActions::EditAudioClipProperties, TActionGroups::EditPropertiesBase, &TAudioClipView::edit_properties);
    m->add_function<TAudioClipView>(TActions::AudioClipSetAudioFile, &TAudioClipView::set_audio_file);

    m->add_function<TAudioPlugin>(TActions::PluginToggleBypass, TActionGroups::ToggleBypassBase, &TAudioPlugin::toggle_bypass);
    m->add_function<TAudioPluginView>(TActions::EditPluginProperties, TActionGroups::EditPropertiesBase, &TAudioPluginView::edit_properties);

    m->add_function<TAudioProcessingNode>(TActions::Mute, &TAudioProcessingNode::mute, TDispatchRule::AlwaysSafeDuringHold);

    m->add_function<TAudioTrack>(TActions::AudioTrackToggleRecord, &TAudioTrack::toggle_arm, TDispatchRule::AlwaysSafeDuringHold);

    m->add_function<TAudioTrackView>(TActions::AudioTrackInsertSilence, &TAudioTrackView::insert_silence);

    m->add_function<CropClip>(TActions::CropClipAdjustLeft, &CropClip::adjust_left);
    m->add_function<CropClip>(TActions::CropClipAdjustRight, &CropClip::adjust_right);

    m->add_function<CorrelationMeterView>(TActions::CorrelationMeterToggleDisplayRange, &CorrelationMeterView::set_mode);

    m->add_function<TCurveView>(TActions::AddCurveNode, &TCurveView::add_node);
    m->add_function<TCurveView>(TActions::CurveSelectAllNodes, &TCurveView::toggle_select_all_nodes);
    m->add_function<TCurveView>(TActions::CurveSelectNode, &TCurveView::select_lazy_selected_node);

    m->add_function<TFadeCurve>(TActions::FadeCurveCycleShape, &TFadeCurve::set_mode, TDispatchRule::AlwaysSafeDuringHold);
    m->add_function<TFadeCurve>(TActions::FadeCurveToggleBypass, TActionGroups::ToggleBypassBase, &TFadeCurve::toggle_bypass, TDispatchRule::AlwaysSafeDuringHold);
    m->add_function<TFadeCurve>(TActions::FadeCurveToggleRaster, &TFadeCurve::toggle_raster, TDispatchRule::AlwaysSafeDuringHold);
    m->add_function<TFadeCurveView>(TActions::FadeSelectPreset, &TFadeCurveView::select_fade_shape);

    m->add_function<TFadeRangeCommand>(TActions::FadeResetLength, TActionGroups::ResetBase, &TFadeRangeCommand::reset_length);

    m->add_function<TFaderGroupCommand>(TActions::GainIncrease, &TFaderGroupCommand::increase_gain);
    m->add_function<TFaderGroupCommand>(TActions::GainDecrease, &TFaderGroupCommand::decrease_gain);
    m->add_function<TFaderGroupCommand>(TActions::GainNumericalInput, &TFaderGroupCommand::numerical_input);
    m->add_function<TFaderGroupCommand>(TActions::GainToggleSelection, &TFaderGroupCommand::toggle_primary_gain_only);
    m->add_function<TFaderGroupCommand>(TActions::GainReset, TActionGroups::ResetBase, &TFaderGroupCommand::reset_gain);

    m->add_function<TMainWindow>(TActions::ShowContextMenu, &TMainWindow::show_context_menu, TDispatchRule::AlwaysSafeDuringHold);
    m->add_function<TMainWindow>(TActions::ShowNewTrackDialog, &TMainWindow::show_newtrack_dialog);
    m->add_function<TMainWindow>(TActions::MainWindowActivateTrackFinder, &TMainWindow::show_track_finder);
    m->add_function<TMainWindow>(TActions::MainWindowNavigateToFirstTrack, &TMainWindow::browse_to_first_track_in_active_sheet);
    m->add_function<TMainWindow>(TActions::MainWindowNavigateToLastTrack, &TMainWindow::browse_to_last_track_in_active_sheet);
    m->add_function<TMainWindow>(TActions::MainWindowShowFullScreen, &TMainWindow::full_screen);
    m->add_function<TMainWindow>(TActions::MainWindowShowProjectManagementDialog, &TMainWindow::show_project_manager_dialog);
    m->add_function<TMainWindow>(TActions::ShowHelp, &TMainWindow::quick_start);

    // Shown in the context menu of every hold command
    m->add_dispatcher_function(&TCommand::staticMetaObject, TActions::AcceptHoldCommand);
    m->add_dispatcher_function(&TCommand::staticMetaObject, TActions::RejectHoldCommand);

    m->add_function<MoveClip>(TActions::MoveClipToggleVerticalOnly, TActionGroups::ToggleVerticalBase, &MoveClip::toggle_vertical_only);

    // m->add_function(&TMoveCommand::staticMetaObject, tr("Move Left"),       "MoveCommandLeft",              "move_left()");
    // m->add_function(&TMoveCommand::staticMetaObject, tr("Move Right"),      "MoveCommandRight",             "move_right()");
    m->add_function<TMoveCommand>(TActions::MoveCommandUp, &TMoveCommand::move_up);
    m->add_function<TMoveCommand>(TActions::MoveCommandDown, &TMoveCommand::move_down);
    m->add_function<TMoveCommand>(TActions::MoveCommandFaster, &TMoveCommand::move_faster);
    m->add_function<TMoveCommand>(TActions::MoveCommandSlower, &TMoveCommand::move_slower);
    m->add_function<TMoveCommand>(TActions::MoveCommandSpeed, &TMoveCommand::numerical_input);
    m->add_function<TMoveCommand>(TActions::MoveCommandToggleSnap, &TMoveCommand::toggle_snap_on_off);

    // m->add_function(&TCommandController::staticMetaObject, tr("Move Left"),       "MoveCommandLeft",              "move_left()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Move Right"),      "MoveCommandRight",             "move_right()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Move Up"),         "MoveCommandUp",                "move_up()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Move Down"),       "MoveCommandDown",              "move_down()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Move Faster"),     "MoveCommandFaster",            "move_faster()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Move Slower"),     "MoveCommandSlower",            "move_slower()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Moving Speed"),    "MoveCommandSpeed",             "numerical_input()");
    // m->add_function(&TCommandController::staticMetaObject, tr("Snap On/Off"),     "MoveCommandToggleSnap",        "toggle_snap_on_off()");


    m->add_function<MoveCurveNode>(TActions::MoveCurveNodeToggleVerticalOnly, TActionGroups::ToggleVerticalBase, &MoveCurveNode::toggle_vertical_only);

    m->add_function<MoveTrack>(TActions::MoveTrackUp, &MoveTrack::move_up);
    m->add_function<MoveTrack>(TActions::MoveTrackDown, &MoveTrack::move_down);
    m->add_function<MoveTrack>(TActions::MoveTrackToTop, &MoveTrack::to_top);
    m->add_function<MoveTrack>(TActions::MoveTrackToBottom, &MoveTrack::to_bottom);

    m->add_function<PlayHeadMove>(TActions::PlayHeadMoveToStart, &PlayHeadMove::move_to_start);
    m->add_function<PlayHeadMove>(TActions::PlayHeadMoveToWorkCursor, &PlayHeadMove::move_to_work_cursor);

    m->add_function<TProjectManager>(TActions::ProjectSave, &TProjectManager::save_project, TDispatchRule::AlwaysSafeDuringHold);

    m->add_function<TSheetView>(TActions::EditSongProperties, TActionGroups::EditPropertiesBase, &TSheetView::edit_properties);
    m->add_function<TSheetView>(TActions::ViewScrollUp, &TSheetView::scroll_up);
    m->add_function<TSheetView>(TActions::ViewScrollDown, &TSheetView::scroll_down);
    m->add_function<TSheetView>(TActions::WorkCursorTouch, &TSheetView::touch);
    m->add_function<TSheetView>(TActions::WorkCursorToStart, &TSheetView::goto_begin);
    m->add_function<TSheetView>(TActions::WorkCursorToEnd, &TSheetView::goto_end);
    m->add_function<TSheetView>(TActions::SheetSetPlayPosition, &TSheetView::touch_play_cursor);
    m->add_function<TSheetView>(TActions::SheetCenterPlayhead, &TSheetView::center_playhead);
    m->add_function<TSheetView>(TActions::NavigateToUpperContext, &TSheetView::to_upper_context_level);
    m->add_function<TSheetView>(TActions::NavigateToLowerContext, &TSheetView::to_lower_context_level);
    m->add_function<TSheetView>(TActions::NavigateToTimeLine, &TSheetView::browse_to_time_line);

    m->add_function<TShortCutManager>(TActions::ExportShortcutMap, &TShortCutManager::export_keymap);

    m->add_function<SpectralMeterView>(TActions::EditSpectralMeterProperties, TActionGroups::EditPropertiesBase, &SpectralMeterView::edit_properties);
    m->add_function<SpectralMeterView>(TActions::SpectralMeterResetAverageCurve, &SpectralMeterView::reset);
    m->add_function<SpectralMeterView>(TActions::SpectralMeterToggleDisplayRange, &SpectralMeterView::set_mode);

    m->add_function<TTimeLineRulerView>(TActions::TimeLinePlayheadToMarker, &TTimeLineRulerView::playhead_to_marker);
    m->add_function<TTimeLineRulerView>(TActions::TimeLineShowMarkerDialog, &TTimeLineRulerView::show_marker_dialog);

    m->add_function<TTrack>(TActions::Solo, &TTrack::solo, TDispatchRule::AlwaysSafeDuringHold);
    m->add_function<TTrackView>(TActions::TrackAddPlugin, &TTrackView::add_new_plugin);

    m->add_function<TPanKnobView>(TActions::PanKnobPanLeft, &TPanKnobView::pan_left);
    m->add_function<TPanKnobView>(TActions::PanKnobPanRight, &TPanKnobView::pan_right);
    m->add_function<TrackPan>(TActions::TrackPanLeft, &TrackPan::pan_left);
    m->add_function<TrackPan>(TActions::TrackPanRight, &TrackPan::pan_right);
    m->add_function<TrackPan>(TActions::TrackPanReset, TActionGroups::ResetBase, &TrackPan::reset_pan);

    m->add_function<TrackPanelGain>(TActions::TrackPanelGainIncrement, &TrackPanelGain::gain_increment);
    m->add_function<TrackPanelGain>(TActions::TrackPanelGainDecrement, &TrackPanelGain::gain_decrement);

    m->add_function<TrackPanelLed>(TActions::PanelLedToggle, &TrackPanelLed::toggle);

    m->add_function<TTransport>(TActions::TransportPlayStartStop, &TTransport::start_transport, TDispatchRule::AlwaysSafeDuringHold);
    m->add_function<TTransport>(TActions::TransportSetRecordingPlayStart, &TTransport::set_recordable_and_start_transport, TDispatchRule::AlwaysSafeDuringHold);
    m->add_function<TTransport>(TActions::TransportToStart, &TTransport::to_start, TDispatchRule::AlwaysSafeDuringHold);
    m->add_function<TTransport>(TActions::TransportToEnd, &TTransport::to_end, TDispatchRule::AlwaysSafeDuringHold);

    m->add_function<WorkCursorMove>(TActions::WorkCursorMoveToPlayhead, &WorkCursorMove::move_to_play_cursor);
    m->add_function<WorkCursorMove>(TActions::WorkCursorMoveToStart, &WorkCursorMove::move_to_start);

    m->add_function<TZoomCommand>(TActions::ZoomIn, &TZoomCommand::hzoom_in);
    m->add_function<TZoomCommand>(TActions::ZoomOut, &TZoomCommand::hzoom_out);
    m->add_function<TZoomCommand>(TActions::ZoomTrackVerticalIn, &TZoomCommand::track_vzoom_in);
    m->add_function<TZoomCommand>(TActions::ZoomTrackVerticalOut, &TZoomCommand::track_vzoom_out);
    m->add_function<TZoomCommand>(TActions::ZoomNumericalInput, &TZoomCommand::numerical_input);
    m->add_function<TZoomCommand>(TActions::ZoomToggleExpandAllTracks, &TZoomCommand::toggle_expand_all_tracks);
    m->add_function<TZoomCommand>(TActions::ZoomToggleVerticalHorizontal, TActionGroups::ToggleVerticalBase, &TZoomCommand::toggle_vertical_horizontal_jog_zoom);
}

TShortCutFunction* TraversoCommands::add_function(const QMetaObject *metaObject,
                                                  const TAction &action,
                                                  const TActionGroup *group,
                                                  TraversoCommand command,
                                                  bool useX,
                                                  bool useY,
                                                  const QVariantList &args)
{
    auto function = m_manager->add_function(metaObject, action);

    if (!function) {
        PERROR(QString("TraversoCommands::add_function: Could not register function for %1").arg(action.name));
        return nullptr;
    }

    function->set_plugin_name("TraversoCommands");

    if (group) {
        function->set_group(group);
    }
    function->set_use_x(useX);
    function->set_use_y(useY);
    function->set_arguments(args);

    m_dict.insert(action.id, command);

    return function;

}

QStringList TraversoCommands::unregistered_commands() const
{
    QStringList unregistered;
    const QMetaEnum commands = QMetaEnum::fromType<TraversoCommand>();
    for (int i = 0; i < commands.keyCount(); ++i) {
        int command = commands.value(i);
        if (command != NoCommand && !m_dict.values().contains(command)) {
            unregistered << commands.key(i);
        }
    }
    return unregistered;
}

TCommand* TraversoCommands::create(QObject* obj, TActionId action, QVariantList arguments)
{
    TraversoCommand command = static_cast<TraversoCommand>(m_dict.value(action, NoCommand));

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

    case AddNewAudioTrackCommand:
    {
        TSheet* sheet = qobject_cast<TSheet*>(obj);
        Q_ASSERT(sheet);
        return sheet->add_track(new TAudioTrack(sheet, "Unnamed", TAudioTrack::INITIAL_HEIGHT));
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
        // The length of the fade the cursor is on
        if (TFadeCurveView* fadeCurveView = qobject_cast<TFadeCurveView*>(obj)) {
            TFadeCurve* fade = fadeCurveView->get_fade();
            bool fadeIn = fade->get_fade_type() == TFadeCurve::FadeIn;
            return new TFadeRangeCommand(fadeCurveView->get_audio_clip(), fadeIn ? fade : nullptr, fadeIn ? nullptr : fade,
                                         fadeCurveView->get_sheetview()->timeref_scalefactor);
        }

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
        PERROR(QString("My dictionary does not know this command: %1").arg(TActions::All[static_cast<int>(action)].name));
        break;
    }

    return ied().did_not_implement();
}

TCommandController *TraversoCommands::create_controller(QObject *obj, TActionId action, QVariantList arguments)
{
    TraversoCommand command = static_cast<TraversoCommand>(m_dict.value(action, NoCommand));

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


    case ImportAudioCommand:
    {
        TAudioTrack* track = qobject_cast<TAudioTrack*>(obj);
        Q_ASSERT(track);

        return new TAddClipsController(track, track->get_sheet(),
            [track](QString* error) -> QList<TAudioClip*> {
                TProject* project = pm().get_project();
                QString activeFilter = TraversoCommands::tr("Audio files (*.wav *.flac *.ogg *.mp3 *.wv *.w64 *.m4a *.aac)");
                QString fileName = QFileDialog::getOpenFileName(TMainWindow::instance(),
                                                                TraversoCommands::tr("Import audio source"),
                                                                project->get_import_dir(),
                                                                TraversoCommands::tr("All files (*)") + ";;" + activeFilter,
                                                                &activeFilter);
                if (fileName.isEmpty()) {
                    return {};
                }

                project->set_import_dir(QFileInfo(fileName).absolutePath());

                // Appended to the end of the track
                TAudioClip* clip = TAudioClipFactory::create_from_file(track, fileName, track->get_end_location(), error);
                if (!clip) {
                    return {};
                }
                return {clip};
            },
            tr("Import Audio File"));
    }

    case RemoveClipCommand:
    {
        TAudioClip* audioClip = qobject_cast<TAudioClip*>(obj);
        Q_ASSERT(audioClip);

        TSheet* sheet = audioClip->get_sheet();
        Q_ASSERT(sheet);

        return new TRemoveEntitiesController(audioClip, sheet->get_selection_service(),
            [sheet](const QList<TTimeLineEntity*>& entities, bool removingSelection) {
                QList<TAudioClip*> clips;
                for (TTimeLineEntity* entity : entities) {
                    if (auto* clip = dynamic_cast<TAudioClip*>(entity)) {
                        clips.append(clip);
                    }
                }
                return std::unique_ptr<QUndoCommand>(sheet->create_command_mutate_clips(
                    {}, clips,
                    removingSelection ? TraversoCommands::tr("Remove Selected Clips") : TraversoCommands::tr("Remove Clip")));
            });
    }

    }

    return nullptr;

}

// eof
