// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QCoreApplication>
#include <QHash>

#include <array>

// Every feature that can be bound to a key. The name is what users see and
// edit in the keymap files, so it must never change; code only uses the id.
enum class TActionId : int {
    ArrowKeyBrowserStartUp,
    ArrowKeyBrowserStartDown,
    ArrowKeyBrowserStartLeft,
    ArrowKeyBrowserStartRight,
    ArrowKeyBrowserUp,
    ArrowKeyBrowserDown,
    ArrowKeyBrowserLeft,
    ArrowKeyBrowserRight,
    RemoveClip,
    RemovePlugin,
    RemoveCurveNode,
    RemoveTrack,
    RemoveTimeLineRulerMarker,
    EditTrackProperties,
    AudioClipGain,
    AudioTrackGain,
    BusTrackGain,
    MoveClip,
    MovePlugin,
    MoveCurveNodes,
    MoveTrack,
    TimeLineMoveMarker,
    MoveMarker,
    SheetAddMarker,
    SheetAddMarkerAtPlayhead,
    SheetAddMarkerAtWorkCursor,
    TimeLineAddMarker,
    TimeLineAddMarkerAtPlayhead,
    TimeLineAddMarkerAtWorkCursor,
    AudioClipExternalProcessing,
    ClipSelectionSelect,
    CropClip,
    AudioClipFadeLength,
    AudioClipFadeLengthBoth,
    FadeCurveBend,
    FadeCurveStrenght,
    GainShowAutomation,
    ImportAudio,
    NormalizeClip,
    CopyClip,
    FoldTrack,
    FoldSheet,
    FoldMarkers,
    MoveClipEdge,
    Shuttle,
    SplitClip,
    AudioClipDualTrim,
    TrackPan,
    PanKnobPanorama,
    TransportSetPosition,
    WorkCursorMove,
    Zoom,
    AudioClipLock,
    EditAudioClipProperties,
    AudioClipSetAudioFile,
    PluginToggleBypass,
    EditPluginProperties,
    Mute,
    AudioTrackToggleRecord,
    AudioTrackInsertSilence,
    CropClipAdjustLeft,
    CropClipAdjustRight,
    CorrelationMeterToggleDisplayRange,
    AddCurveNode,
    CurveSelectAllNodes,
    CurveSelectNode,
    FadeCurveCycleShape,
    FadeCurveToggleBypass,
    FadeCurveToggleRaster,
    FadeSelectPreset,
    FadeResetLength,
    GainIncrease,
    GainDecrease,
    GainNumericalInput,
    GainToggleSelection,
    GainReset,
    ShowContextMenu,
    ShowNewTrackDialog,
    MainWindowActivateTrackFinder,
    MainWindowNavigateToFirstTrack,
    MainWindowNavigateToLastTrack,
    MainWindowShowFullScreen,
    MainWindowShowProjectManagementDialog,
    ShowHelp,
    MoveClipToggleVerticalOnly,
    MoveCommandUp,
    MoveCommandDown,
    MoveCommandFaster,
    MoveCommandSlower,
    MoveCommandSpeed,
    MoveCommandToggleSnap,
    MoveCurveNodeToggleVerticalOnly,
    MoveTrackUp,
    MoveTrackDown,
    MoveTrackToTop,
    MoveTrackToBottom,
    PlayHeadMoveToStart,
    PlayHeadMoveToWorkCursor,
    ProjectSave,
    EditSongProperties,
    ViewScrollUp,
    ViewScrollDown,
    WorkCursorTouch,
    WorkCursorToStart,
    WorkCursorToEnd,
    SheetSetPlayPosition,
    SheetCenterPlayhead,
    NavigateToUpperContext,
    NavigateToLowerContext,
    NavigateToTimeLine,
    ExportShortcutMap,
    EditSpectralMeterProperties,
    SpectralMeterResetAverageCurve,
    SpectralMeterToggleDisplayRange,
    TimeLinePlayheadToMarker,
    TimeLineShowMarkerDialog,
    Solo,
    TrackAddPlugin,
    PanKnobPanLeft,
    PanKnobPanRight,
    TrackPanLeft,
    TrackPanRight,
    TrackPanReset,
    TrackPanelGainIncrement,
    TrackPanelGainDecrement,
    PanelLedToggle,
    TransportPlayStartStop,
    TransportSetRecordingPlayStart,
    TransportToStart,
    TransportToEnd,
    WorkCursorMoveToPlayhead,
    WorkCursorMoveToStart,
    ZoomIn,
    ZoomOut,
    ZoomTrackVerticalIn,
    ZoomTrackVerticalOut,
    ZoomNumericalInput,
    ZoomToggleExpandAllTracks,
    ZoomToggleVerticalHorizontal,
    FadeLength,
    ZoomVertical,
    AcceptHoldCommand,
    RejectHoldCommand,
    Count
};

inline size_t qHash(TActionId id, size_t seed = 0) noexcept
{
    return qHash(static_cast<int>(id), seed);
}

struct TAction
{
    TActionId   id;
    const char* name;         // the name in the keymap files
    const char* description;  // translated with translated_description()

    QString translated_description() const
    {
        return QCoreApplication::translate("TraversoCommands", description);
    }
};

struct TActions
{
    static constexpr TAction ArrowKeyBrowserStartUp                {TActionId::ArrowKeyBrowserStartUp, "ArrowKeyBrowserStartUp", ""};
    static constexpr TAction ArrowKeyBrowserStartDown              {TActionId::ArrowKeyBrowserStartDown, "ArrowKeyBrowserStartDown", ""};
    static constexpr TAction ArrowKeyBrowserStartLeft              {TActionId::ArrowKeyBrowserStartLeft, "ArrowKeyBrowserStartLeft", ""};
    static constexpr TAction ArrowKeyBrowserStartRight             {TActionId::ArrowKeyBrowserStartRight, "ArrowKeyBrowserStartRight", ""};
    static constexpr TAction ArrowKeyBrowserUp                     {TActionId::ArrowKeyBrowserUp, "ArrowKeyBrowserUp", ""};
    static constexpr TAction ArrowKeyBrowserDown                   {TActionId::ArrowKeyBrowserDown, "ArrowKeyBrowserDown", ""};
    static constexpr TAction ArrowKeyBrowserLeft                   {TActionId::ArrowKeyBrowserLeft, "ArrowKeyBrowserLeft", ""};
    static constexpr TAction ArrowKeyBrowserRight                  {TActionId::ArrowKeyBrowserRight, "ArrowKeyBrowserRight", ""};
    static constexpr TAction RemoveClip                            {TActionId::RemoveClip, "RemoveClip", QT_TRANSLATE_NOOP("TraversoCommands", "Remove AudioClip")};
    static constexpr TAction RemovePlugin                          {TActionId::RemovePlugin, "RemovePlugin", QT_TRANSLATE_NOOP("TraversoCommands", "Remove Plugin")};
    static constexpr TAction RemoveCurveNode                       {TActionId::RemoveCurveNode, "RemoveCurveNode", QT_TRANSLATE_NOOP("TraversoCommands", "Remove Node(s)")};
    static constexpr TAction RemoveTrack                           {TActionId::RemoveTrack, "RemoveTrack", QT_TRANSLATE_NOOP("TraversoCommands", "Remove Track")};
    static constexpr TAction RemoveTimeLineRulerMarker             {TActionId::RemoveTimeLineRulerMarker, "RemoveTimeLineRulerMarker", QT_TRANSLATE_NOOP("TraversoCommands", "Remove Marker")};
    static constexpr TAction EditTrackProperties                   {TActionId::EditTrackProperties, "EditTrackProperties", ""};
    static constexpr TAction AudioClipGain                         {TActionId::AudioClipGain, "AudioClipGain", ""};
    static constexpr TAction AudioTrackGain                        {TActionId::AudioTrackGain, "AudioTrackGain", ""};
    static constexpr TAction BusTrackGain                          {TActionId::BusTrackGain, "BusTrackGain", ""};
    static constexpr TAction MoveClip                              {TActionId::MoveClip, "MoveClip", ""};
    static constexpr TAction MovePlugin                            {TActionId::MovePlugin, "MovePlugin", ""};
    static constexpr TAction MoveCurveNodes                        {TActionId::MoveCurveNodes, "MoveCurveNodes", QT_TRANSLATE_NOOP("TraversoCommands", "Move Curve Node(s)")};
    static constexpr TAction MoveTrack                             {TActionId::MoveTrack, "MoveTrack", QT_TRANSLATE_NOOP("TraversoCommands", "Move Up/Down")};
    static constexpr TAction TimeLineMoveMarker                    {TActionId::TimeLineMoveMarker, "TimeLineMoveMarker", QT_TRANSLATE_NOOP("TraversoCommands", "Move Marker")};
    static constexpr TAction MoveMarker                            {TActionId::MoveMarker, "MoveMarker", QT_TRANSLATE_NOOP("TraversoCommands", "Move Marker")};
    static constexpr TAction SheetAddMarker                        {TActionId::SheetAddMarker, "SheetAddMarker", QT_TRANSLATE_NOOP("TraversoCommands", "Add Marker")};
    static constexpr TAction SheetAddMarkerAtPlayhead              {TActionId::SheetAddMarkerAtPlayhead, "SheetAddMarkerAtPlayhead", QT_TRANSLATE_NOOP("TraversoCommands", "Add Marker at Playhead")};
    static constexpr TAction SheetAddMarkerAtWorkCursor            {TActionId::SheetAddMarkerAtWorkCursor, "SheetAddMarkerAtWorkCursor", QT_TRANSLATE_NOOP("TraversoCommands", "Add Marker at Work Cursor")};
    static constexpr TAction TimeLineAddMarker                     {TActionId::TimeLineAddMarker, "TimeLineAddMarker", QT_TRANSLATE_NOOP("TraversoCommands", "Add Marker")};
    static constexpr TAction TimeLineAddMarkerAtPlayhead           {TActionId::TimeLineAddMarkerAtPlayhead, "TimeLineAddMarkerAtPlayhead", QT_TRANSLATE_NOOP("TraversoCommands", "Add Marker at Playhead")};
    static constexpr TAction TimeLineAddMarkerAtWorkCursor         {TActionId::TimeLineAddMarkerAtWorkCursor, "TimeLineAddMarkerAtWorkCursor", QT_TRANSLATE_NOOP("TraversoCommands", "Add Marker at Work Cursor")};
    static constexpr TAction AudioClipExternalProcessing           {TActionId::AudioClipExternalProcessing, "AudioClipExternalProcessing", QT_TRANSLATE_NOOP("TraversoCommands", "External Processing")};
    static constexpr TAction ClipSelectionSelect                   {TActionId::ClipSelectionSelect, "ClipSelectionSelect", QT_TRANSLATE_NOOP("TraversoCommands", "(De)Select")};
    static constexpr TAction CropClip                              {TActionId::CropClip, "CropClip", QT_TRANSLATE_NOOP("TraversoCommands", "Magnetic Cut")};
    static constexpr TAction AudioClipFadeLength                   {TActionId::AudioClipFadeLength, "AudioClipFadeLength", QT_TRANSLATE_NOOP("TraversoCommands", "Adjust Length")};
    static constexpr TAction AudioClipFadeLengthBoth               {TActionId::AudioClipFadeLengthBoth, "AudioClipFadeLengthBoth", QT_TRANSLATE_NOOP("TraversoCommands", "Adjust Length Both")};
    static constexpr TAction FadeCurveBend                         {TActionId::FadeCurveBend, "FadeCurveBend", QT_TRANSLATE_NOOP("TraversoCommands", "Adjust Bend")};
    static constexpr TAction FadeCurveStrenght                     {TActionId::FadeCurveStrenght, "FadeCurveStrenght", QT_TRANSLATE_NOOP("TraversoCommands", "Adjust Strength")};
    static constexpr TAction GainShowAutomation                    {TActionId::GainShowAutomation, "GainShowAutomation", QT_TRANSLATE_NOOP("TraversoCommands", "Gain Envelope")};
    static constexpr TAction ImportAudio                           {TActionId::ImportAudio, "ImportAudio", QT_TRANSLATE_NOOP("TraversoCommands", "Import Audio")};
    static constexpr TAction NormalizeClip                         {TActionId::NormalizeClip, "NormalizeClip", QT_TRANSLATE_NOOP("TraversoCommands", "Normalize Clip")};
    static constexpr TAction CopyClip                              {TActionId::CopyClip, "CopyClip", QT_TRANSLATE_NOOP("TraversoCommands", "Copy")};
    static constexpr TAction FoldTrack                             {TActionId::FoldTrack, "FoldTrack", QT_TRANSLATE_NOOP("TraversoCommands", "Fold Track")};
    static constexpr TAction FoldSheet                             {TActionId::FoldSheet, "FoldSheet", QT_TRANSLATE_NOOP("TraversoCommands", "Fold Sheet")};
    static constexpr TAction FoldMarkers                           {TActionId::FoldMarkers, "FoldMarkers", QT_TRANSLATE_NOOP("TraversoCommands", "Fold Markers")};
    static constexpr TAction MoveClipEdge                          {TActionId::MoveClipEdge, "MoveClipEdge", QT_TRANSLATE_NOOP("TraversoCommands", "Move Edge")};
    static constexpr TAction Shuttle                               {TActionId::Shuttle, "Shuttle", QT_TRANSLATE_NOOP("TraversoCommands", "Shuttle")};
    static constexpr TAction SplitClip                             {TActionId::SplitClip, "SplitClip", QT_TRANSLATE_NOOP("TraversoCommands", "Split")};
    static constexpr TAction AudioClipDualTrim                     {TActionId::AudioClipDualTrim, "AudioClipDualTrim", QT_TRANSLATE_NOOP("TraversoCommands", "Dual Trim")};
    static constexpr TAction TrackPan                              {TActionId::TrackPan, "TrackPan", QT_TRANSLATE_NOOP("TraversoCommands", "Track Pan")};
    static constexpr TAction PanKnobPanorama                       {TActionId::PanKnobPanorama, "PanKnobPanorama", QT_TRANSLATE_NOOP("TraversoCommands", "Pan")};
    static constexpr TAction TransportSetPosition                  {TActionId::TransportSetPosition, "TransportSetPosition", QT_TRANSLATE_NOOP("TraversoCommands", "Set Play Position")};
    static constexpr TAction WorkCursorMove                        {TActionId::WorkCursorMove, "WorkCursorMove", QT_TRANSLATE_NOOP("TraversoCommands", "Move Work Cursor")};
    static constexpr TAction Zoom                                  {TActionId::Zoom, "Zoom", QT_TRANSLATE_NOOP("TraversoCommands", "Zoom")};
    static constexpr TAction AudioClipLock                         {TActionId::AudioClipLock, "AudioClipLock", QT_TRANSLATE_NOOP("TraversoCommands", "Lock")};
    static constexpr TAction EditAudioClipProperties               {TActionId::EditAudioClipProperties, "EditAudioClipProperties", ""};
    static constexpr TAction AudioClipSetAudioFile                 {TActionId::AudioClipSetAudioFile, "AudioClipSetAudioFile", QT_TRANSLATE_NOOP("TraversoCommands", "Reset Audio File")};
    static constexpr TAction PluginToggleBypass                    {TActionId::PluginToggleBypass, "PluginToggleBypass", ""};
    static constexpr TAction EditPluginProperties                  {TActionId::EditPluginProperties, "EditPluginProperties", ""};
    static constexpr TAction Mute                                  {TActionId::Mute, "Mute", QT_TRANSLATE_NOOP("TraversoCommands", "Mute")};
    static constexpr TAction AudioTrackToggleRecord                {TActionId::AudioTrackToggleRecord, "AudioTrackToggleRecord", QT_TRANSLATE_NOOP("TraversoCommands", "Record: On/Off")};
    static constexpr TAction AudioTrackInsertSilence               {TActionId::AudioTrackInsertSilence, "AudioTrackInsertSilence", QT_TRANSLATE_NOOP("TraversoCommands", "Insert Silence")};
    static constexpr TAction CropClipAdjustLeft                    {TActionId::CropClipAdjustLeft, "CropClipAdjustLeft", QT_TRANSLATE_NOOP("TraversoCommands", "Adjust Left")};
    static constexpr TAction CropClipAdjustRight                   {TActionId::CropClipAdjustRight, "CropClipAdjustRight", QT_TRANSLATE_NOOP("TraversoCommands", "Adjust Right")};
    static constexpr TAction CorrelationMeterToggleDisplayRange    {TActionId::CorrelationMeterToggleDisplayRange, "CorrelationMeterToggleDisplayRange", QT_TRANSLATE_NOOP("TraversoCommands", "Toggle display range")};
    static constexpr TAction AddCurveNode                          {TActionId::AddCurveNode, "AddCurveNode", QT_TRANSLATE_NOOP("TraversoCommands", "New Node")};
    static constexpr TAction CurveSelectAllNodes                   {TActionId::CurveSelectAllNodes, "CurveSelectAllNodes", QT_TRANSLATE_NOOP("TraversoCommands", "Select All Nodes")};
    static constexpr TAction CurveSelectNode                       {TActionId::CurveSelectNode, "CurveSelectNode", QT_TRANSLATE_NOOP("TraversoCommands", "Select Node")};
    static constexpr TAction FadeCurveCycleShape                   {TActionId::FadeCurveCycleShape, "FadeCurveCycleShape", QT_TRANSLATE_NOOP("TraversoCommands", "Cycle Shape")};
    static constexpr TAction FadeCurveToggleBypass                 {TActionId::FadeCurveToggleBypass, "FadeCurveToggleBypass", ""};
    static constexpr TAction FadeCurveToggleRaster                 {TActionId::FadeCurveToggleRaster, "FadeCurveToggleRaster", QT_TRANSLATE_NOOP("TraversoCommands", "Toggle Raster")};
    static constexpr TAction FadeSelectPreset                      {TActionId::FadeSelectPreset, "FadeSelectPreset", QT_TRANSLATE_NOOP("TraversoCommands", "Select Preset")};
    static constexpr TAction FadeResetLength                       {TActionId::FadeResetLength, "FadeResetLength", ""};
    static constexpr TAction GainIncrease                          {TActionId::GainIncrease, "GainIncrease", QT_TRANSLATE_NOOP("TraversoCommands", "Increase")};
    static constexpr TAction GainDecrease                          {TActionId::GainDecrease, "GainDecrease", QT_TRANSLATE_NOOP("TraversoCommands", "Decrease")};
    static constexpr TAction GainNumericalInput                    {TActionId::GainNumericalInput, "GainNumericalInput", QT_TRANSLATE_NOOP("TraversoCommands", "Input dB value")};
    static constexpr TAction GainToggleSelection                   {TActionId::GainToggleSelection, "GainToggleSelection", QT_TRANSLATE_NOOP("TraversoCommands", "To Selection: On/Off")};
    static constexpr TAction GainReset                             {TActionId::GainReset, "GainReset", ""};
    static constexpr TAction ShowContextMenu                       {TActionId::ShowContextMenu, "ShowContextMenu", QT_TRANSLATE_NOOP("TraversoCommands", "Context Menu")};
    static constexpr TAction ShowNewTrackDialog                    {TActionId::ShowNewTrackDialog, "ShowNewTrackDialog", QT_TRANSLATE_NOOP("TraversoCommands", "New Track Dialog")};
    static constexpr TAction MainWindowActivateTrackFinder         {TActionId::MainWindowActivateTrackFinder, "MainWindowActivateTrackFinder", QT_TRANSLATE_NOOP("TraversoCommands", "Activate Track Finder")};
    static constexpr TAction MainWindowNavigateToFirstTrack        {TActionId::MainWindowNavigateToFirstTrack, "MainWindowNavigateToFirstTrack", QT_TRANSLATE_NOOP("TraversoCommands", "Browse to first Track in current View")};
    static constexpr TAction MainWindowNavigateToLastTrack         {TActionId::MainWindowNavigateToLastTrack, "MainWindowNavigateToLastTrack", QT_TRANSLATE_NOOP("TraversoCommands", "Browse to last Track in current View")};
    static constexpr TAction MainWindowShowFullScreen              {TActionId::MainWindowShowFullScreen, "MainWindowShowFullScreen", QT_TRANSLATE_NOOP("TraversoCommands", "Full Screen")};
    static constexpr TAction MainWindowShowProjectManagementDialog {TActionId::MainWindowShowProjectManagementDialog, "MainWindowShowProjectManagementDialog", QT_TRANSLATE_NOOP("TraversoCommands", "Show Project Management Dialog")};
    static constexpr TAction ShowHelp                              {TActionId::ShowHelp, "ShowHelp", QT_TRANSLATE_NOOP("TraversoCommands", "Show Help")};
    static constexpr TAction MoveClipToggleVerticalOnly            {TActionId::MoveClipToggleVerticalOnly, "MoveClipToggleVerticalOnly", ""};
    static constexpr TAction MoveCommandUp                         {TActionId::MoveCommandUp, "MoveCommandUp", QT_TRANSLATE_NOOP("TraversoCommands", "Move Up")};
    static constexpr TAction MoveCommandDown                       {TActionId::MoveCommandDown, "MoveCommandDown", QT_TRANSLATE_NOOP("TraversoCommands", "Move Down")};
    static constexpr TAction MoveCommandFaster                     {TActionId::MoveCommandFaster, "MoveCommandFaster", QT_TRANSLATE_NOOP("TraversoCommands", "Move Faster")};
    static constexpr TAction MoveCommandSlower                     {TActionId::MoveCommandSlower, "MoveCommandSlower", QT_TRANSLATE_NOOP("TraversoCommands", "Move Slower")};
    static constexpr TAction MoveCommandSpeed                      {TActionId::MoveCommandSpeed, "MoveCommandSpeed", QT_TRANSLATE_NOOP("TraversoCommands", "Moving Speed")};
    static constexpr TAction MoveCommandToggleSnap                 {TActionId::MoveCommandToggleSnap, "MoveCommandToggleSnap", QT_TRANSLATE_NOOP("TraversoCommands", "Snap On/Off")};
    static constexpr TAction MoveCurveNodeToggleVerticalOnly       {TActionId::MoveCurveNodeToggleVerticalOnly, "MoveCurveNodeToggleVerticalOnly", ""};
    static constexpr TAction MoveTrackUp                           {TActionId::MoveTrackUp, "MoveTrackUp", QT_TRANSLATE_NOOP("TraversoCommands", "Move Up")};
    static constexpr TAction MoveTrackDown                         {TActionId::MoveTrackDown, "MoveTrackDown", QT_TRANSLATE_NOOP("TraversoCommands", "Move Down")};
    static constexpr TAction MoveTrackToTop                        {TActionId::MoveTrackToTop, "MoveTrackToTop", QT_TRANSLATE_NOOP("TraversoCommands", "To Top")};
    static constexpr TAction MoveTrackToBottom                     {TActionId::MoveTrackToBottom, "MoveTrackToBottom", QT_TRANSLATE_NOOP("TraversoCommands", "To Bottom")};
    static constexpr TAction PlayHeadMoveToStart                   {TActionId::PlayHeadMoveToStart, "PlayHeadMoveToStart", QT_TRANSLATE_NOOP("TraversoCommands", "To Start")};
    static constexpr TAction PlayHeadMoveToWorkCursor              {TActionId::PlayHeadMoveToWorkCursor, "PlayHeadMoveToWorkCursor", QT_TRANSLATE_NOOP("TraversoCommands", "To Work Cursor")};
    static constexpr TAction ProjectSave                           {TActionId::ProjectSave, "ProjectSave", QT_TRANSLATE_NOOP("TraversoCommands", "Save Project")};
    static constexpr TAction EditSongProperties                    {TActionId::EditSongProperties, "EditSongProperties", ""};
    static constexpr TAction ViewScrollUp                          {TActionId::ViewScrollUp, "ViewScrollUp", QT_TRANSLATE_NOOP("TraversoCommands", "Up")};
    static constexpr TAction ViewScrollDown                        {TActionId::ViewScrollDown, "ViewScrollDown", QT_TRANSLATE_NOOP("TraversoCommands", "Down")};
    static constexpr TAction WorkCursorTouch                       {TActionId::WorkCursorTouch, "WorkCursorTouch", QT_TRANSLATE_NOOP("TraversoCommands", "Set")};
    static constexpr TAction WorkCursorToStart                     {TActionId::WorkCursorToStart, "WorkCursorToStart", QT_TRANSLATE_NOOP("TraversoCommands", "To start")};
    static constexpr TAction WorkCursorToEnd                       {TActionId::WorkCursorToEnd, "WorkCursorToEnd", QT_TRANSLATE_NOOP("TraversoCommands", "To end")};
    static constexpr TAction SheetSetPlayPosition                  {TActionId::SheetSetPlayPosition, "SheetSetPlayPosition", QT_TRANSLATE_NOOP("TraversoCommands", "Set")};
    static constexpr TAction SheetCenterPlayhead                   {TActionId::SheetCenterPlayhead, "SheetCenterPlayhead", QT_TRANSLATE_NOOP("TraversoCommands", "Center")};
    static constexpr TAction NavigateToUpperContext                {TActionId::NavigateToUpperContext, "NavigateToUpperContext", QT_TRANSLATE_NOOP("TraversoCommands", "One Layer Up")};
    static constexpr TAction NavigateToLowerContext                {TActionId::NavigateToLowerContext, "NavigateToLowerContext", QT_TRANSLATE_NOOP("TraversoCommands", "One Layer Down")};
    static constexpr TAction NavigateToTimeLine                    {TActionId::NavigateToTimeLine, "NavigateToTimeLine", QT_TRANSLATE_NOOP("TraversoCommands", "To Timeline")};
    static constexpr TAction ExportShortcutMap                     {TActionId::ExportShortcutMap, "ExportShortcutMap", QT_TRANSLATE_NOOP("TraversoCommands", "Export keymap")};
    static constexpr TAction EditSpectralMeterProperties           {TActionId::EditSpectralMeterProperties, "EditSpectralMeterProperties", ""};
    static constexpr TAction SpectralMeterResetAverageCurve        {TActionId::SpectralMeterResetAverageCurve, "SpectralMeterResetAverageCurve", QT_TRANSLATE_NOOP("TraversoCommands", "Reset average curve")};
    static constexpr TAction SpectralMeterToggleDisplayRange       {TActionId::SpectralMeterToggleDisplayRange, "SpectralMeterToggleDisplayRange", QT_TRANSLATE_NOOP("TraversoCommands", "Toggle average curve")};
    static constexpr TAction TimeLinePlayheadToMarker              {TActionId::TimeLinePlayheadToMarker, "TimeLinePlayheadToMarker", QT_TRANSLATE_NOOP("TraversoCommands", "Playhead to Marker")};
    static constexpr TAction TimeLineShowMarkerDialog              {TActionId::TimeLineShowMarkerDialog, "TimeLineShowMarkerDialog", QT_TRANSLATE_NOOP("TraversoCommands", "Edit Markers")};
    static constexpr TAction Solo                                  {TActionId::Solo, "Solo", QT_TRANSLATE_NOOP("TraversoCommands", "Solo")};
    static constexpr TAction TrackAddPlugin                        {TActionId::TrackAddPlugin, "TrackAddPlugin", QT_TRANSLATE_NOOP("TraversoCommands", "Add new Plugin")};
    static constexpr TAction PanKnobPanLeft                        {TActionId::PanKnobPanLeft, "PanKnobPanLeft", QT_TRANSLATE_NOOP("TraversoCommands", "Pan to Left")};
    static constexpr TAction PanKnobPanRight                       {TActionId::PanKnobPanRight, "PanKnobPanRight", QT_TRANSLATE_NOOP("TraversoCommands", "Pan to Right")};
    static constexpr TAction TrackPanLeft                          {TActionId::TrackPanLeft, "TrackPanLeft", QT_TRANSLATE_NOOP("TraversoCommands", "Pan to Left")};
    static constexpr TAction TrackPanRight                         {TActionId::TrackPanRight, "TrackPanRight", QT_TRANSLATE_NOOP("TraversoCommands", "Pan to Right")};
    static constexpr TAction TrackPanReset                         {TActionId::TrackPanReset, "TrackPanReset", ""};
    static constexpr TAction TrackPanelGainIncrement               {TActionId::TrackPanelGainIncrement, "TrackPanelGainIncrement", QT_TRANSLATE_NOOP("TraversoCommands", "Increase")};
    static constexpr TAction TrackPanelGainDecrement               {TActionId::TrackPanelGainDecrement, "TrackPanelGainDecrement", QT_TRANSLATE_NOOP("TraversoCommands", "Decrease")};
    static constexpr TAction PanelLedToggle                        {TActionId::PanelLedToggle, "PanelLedToggle", QT_TRANSLATE_NOOP("TraversoCommands", "Toggle On/Off")};
    static constexpr TAction TransportPlayStartStop                {TActionId::TransportPlayStartStop, "TransportPlayStartStop", QT_TRANSLATE_NOOP("TraversoCommands", "Play (Start/Stop)")};
    static constexpr TAction TransportSetRecordingPlayStart        {TActionId::TransportSetRecordingPlayStart, "TransportSetRecordingPlayStart", QT_TRANSLATE_NOOP("TraversoCommands", "Start Recording")};
    static constexpr TAction TransportToStart                      {TActionId::TransportToStart, "TransportToStart", QT_TRANSLATE_NOOP("TraversoCommands", "To start")};
    static constexpr TAction TransportToEnd                        {TActionId::TransportToEnd, "TransportToEnd", QT_TRANSLATE_NOOP("TraversoCommands", "To end")};
    static constexpr TAction WorkCursorMoveToPlayhead              {TActionId::WorkCursorMoveToPlayhead, "WorkCursorMoveToPlayhead", QT_TRANSLATE_NOOP("TraversoCommands", "To Playhead")};
    static constexpr TAction WorkCursorMoveToStart                 {TActionId::WorkCursorMoveToStart, "WorkCursorMoveToStart", QT_TRANSLATE_NOOP("TraversoCommands", "To Start")};
    static constexpr TAction ZoomIn                                {TActionId::ZoomIn, "ZoomIn", QT_TRANSLATE_NOOP("TraversoCommands", "In")};
    static constexpr TAction ZoomOut                               {TActionId::ZoomOut, "ZoomOut", QT_TRANSLATE_NOOP("TraversoCommands", "Out")};
    static constexpr TAction ZoomTrackVerticalIn                   {TActionId::ZoomTrackVerticalIn, "ZoomTrackVerticalIn", QT_TRANSLATE_NOOP("TraversoCommands", "Track Vertical Zoom In")};
    static constexpr TAction ZoomTrackVerticalOut                  {TActionId::ZoomTrackVerticalOut, "ZoomTrackVerticalOut", QT_TRANSLATE_NOOP("TraversoCommands", "Track Vertical Zoom Out")};
    static constexpr TAction ZoomNumericalInput                    {TActionId::ZoomNumericalInput, "ZoomNumericalInput", QT_TRANSLATE_NOOP("TraversoCommands", "Track Height")};
    static constexpr TAction ZoomToggleExpandAllTracks             {TActionId::ZoomToggleExpandAllTracks, "ZoomToggleExpandAllTracks", QT_TRANSLATE_NOOP("TraversoCommands", "Expand/Collapse Tracks")};
    static constexpr TAction ZoomToggleVerticalHorizontal          {TActionId::ZoomToggleVerticalHorizontal, "ZoomToggleVerticalHorizontal", ""};
    static constexpr TAction FadeLength                            {TActionId::FadeLength, "FadeLength", QT_TRANSLATE_NOOP("TraversoCommands", "Length")};
    static constexpr TAction ZoomVertical                          {TActionId::ZoomVertical, "ZoomVertical", QT_TRANSLATE_NOOP("TraversoCommands", "Zoom Vertical")};
    static constexpr TAction AcceptHoldCommand                     {TActionId::AcceptHoldCommand, "AcceptHoldCommand", QT_TRANSLATE_NOOP("TraversoCommands", "Accept")};
    static constexpr TAction RejectHoldCommand                     {TActionId::RejectHoldCommand, "RejectHoldCommand", QT_TRANSLATE_NOOP("TraversoCommands", "Reject")};

    static constexpr std::array All{
        ArrowKeyBrowserStartUp,
        ArrowKeyBrowserStartDown,
        ArrowKeyBrowserStartLeft,
        ArrowKeyBrowserStartRight,
        ArrowKeyBrowserUp,
        ArrowKeyBrowserDown,
        ArrowKeyBrowserLeft,
        ArrowKeyBrowserRight,
        RemoveClip,
        RemovePlugin,
        RemoveCurveNode,
        RemoveTrack,
        RemoveTimeLineRulerMarker,
        EditTrackProperties,
        AudioClipGain,
        AudioTrackGain,
        BusTrackGain,
        MoveClip,
        MovePlugin,
        MoveCurveNodes,
        MoveTrack,
        TimeLineMoveMarker,
        MoveMarker,
        SheetAddMarker,
        SheetAddMarkerAtPlayhead,
        SheetAddMarkerAtWorkCursor,
        TimeLineAddMarker,
        TimeLineAddMarkerAtPlayhead,
        TimeLineAddMarkerAtWorkCursor,
        AudioClipExternalProcessing,
        ClipSelectionSelect,
        CropClip,
        AudioClipFadeLength,
        AudioClipFadeLengthBoth,
        FadeCurveBend,
        FadeCurveStrenght,
        GainShowAutomation,
        ImportAudio,
        NormalizeClip,
        CopyClip,
        FoldTrack,
        FoldSheet,
        FoldMarkers,
        MoveClipEdge,
        Shuttle,
        SplitClip,
        AudioClipDualTrim,
        TrackPan,
        PanKnobPanorama,
        TransportSetPosition,
        WorkCursorMove,
        Zoom,
        AudioClipLock,
        EditAudioClipProperties,
        AudioClipSetAudioFile,
        PluginToggleBypass,
        EditPluginProperties,
        Mute,
        AudioTrackToggleRecord,
        AudioTrackInsertSilence,
        CropClipAdjustLeft,
        CropClipAdjustRight,
        CorrelationMeterToggleDisplayRange,
        AddCurveNode,
        CurveSelectAllNodes,
        CurveSelectNode,
        FadeCurveCycleShape,
        FadeCurveToggleBypass,
        FadeCurveToggleRaster,
        FadeSelectPreset,
        FadeResetLength,
        GainIncrease,
        GainDecrease,
        GainNumericalInput,
        GainToggleSelection,
        GainReset,
        ShowContextMenu,
        ShowNewTrackDialog,
        MainWindowActivateTrackFinder,
        MainWindowNavigateToFirstTrack,
        MainWindowNavigateToLastTrack,
        MainWindowShowFullScreen,
        MainWindowShowProjectManagementDialog,
        ShowHelp,
        MoveClipToggleVerticalOnly,
        MoveCommandUp,
        MoveCommandDown,
        MoveCommandFaster,
        MoveCommandSlower,
        MoveCommandSpeed,
        MoveCommandToggleSnap,
        MoveCurveNodeToggleVerticalOnly,
        MoveTrackUp,
        MoveTrackDown,
        MoveTrackToTop,
        MoveTrackToBottom,
        PlayHeadMoveToStart,
        PlayHeadMoveToWorkCursor,
        ProjectSave,
        EditSongProperties,
        ViewScrollUp,
        ViewScrollDown,
        WorkCursorTouch,
        WorkCursorToStart,
        WorkCursorToEnd,
        SheetSetPlayPosition,
        SheetCenterPlayhead,
        NavigateToUpperContext,
        NavigateToLowerContext,
        NavigateToTimeLine,
        ExportShortcutMap,
        EditSpectralMeterProperties,
        SpectralMeterResetAverageCurve,
        SpectralMeterToggleDisplayRange,
        TimeLinePlayheadToMarker,
        TimeLineShowMarkerDialog,
        Solo,
        TrackAddPlugin,
        PanKnobPanLeft,
        PanKnobPanRight,
        TrackPanLeft,
        TrackPanRight,
        TrackPanReset,
        TrackPanelGainIncrement,
        TrackPanelGainDecrement,
        PanelLedToggle,
        TransportPlayStartStop,
        TransportSetRecordingPlayStart,
        TransportToStart,
        TransportToEnd,
        WorkCursorMoveToPlayhead,
        WorkCursorMoveToStart,
        ZoomIn,
        ZoomOut,
        ZoomTrackVerticalIn,
        ZoomTrackVerticalOut,
        ZoomNumericalInput,
        ZoomToggleExpandAllTracks,
        ZoomToggleVerticalHorizontal,
        FadeLength,
        ZoomVertical,
        AcceptHoldCommand,
        RejectHoldCommand,
    };
};

// Groups of functions that share keys, e.g. Delete for clips, tracks and
// markers. A function in a group uses the group's keys unless the user
// gives it its own. The name is the group's name in the keymap files.
enum class TActionGroupId : int {
    TNavigationBaseUp,
    TNavigationBaseDown,
    TNavigationBaseLeft,
    TNavigationBaseRight,
    DeleteBase,
    EditPropertiesBase,
    GainBase,
    MoveBase,
    ResetBase,
    ToggleBypassBase,
    ToggleVerticalBase,
    Count
};

struct TActionGroup
{
    TActionGroupId id;
    const char*    name;         // the name in the keymap files
    const char*    description;  // translated with translated_description()

    QString translated_description() const
    {
        return QCoreApplication::translate("TraversoCommands", description);
    }
};

struct TActionGroups
{
    static constexpr TActionGroup TNavigationBaseUp    {TActionGroupId::TNavigationBaseUp, "TNavigationBaseUp", QT_TRANSLATE_NOOP("TraversoCommands", "Navigate Up")};
    static constexpr TActionGroup TNavigationBaseDown  {TActionGroupId::TNavigationBaseDown, "TNavigationBaseDown", QT_TRANSLATE_NOOP("TraversoCommands", "Navigate Down")};
    static constexpr TActionGroup TNavigationBaseLeft  {TActionGroupId::TNavigationBaseLeft, "TNavigationBaseLeft", QT_TRANSLATE_NOOP("TraversoCommands", "Navigate Left")};
    static constexpr TActionGroup TNavigationBaseRight {TActionGroupId::TNavigationBaseRight, "TNavigationBaseRight", QT_TRANSLATE_NOOP("TraversoCommands", "Navigate Right")};
    static constexpr TActionGroup DeleteBase           {TActionGroupId::DeleteBase, "DeleteBase", QT_TRANSLATE_NOOP("TraversoCommands", "Delete")};
    static constexpr TActionGroup EditPropertiesBase   {TActionGroupId::EditPropertiesBase, "EditPropertiesBase", QT_TRANSLATE_NOOP("TraversoCommands", "Edit Properties")};
    static constexpr TActionGroup GainBase             {TActionGroupId::GainBase, "GainBase", QT_TRANSLATE_NOOP("TraversoCommands", "Gain")};
    static constexpr TActionGroup MoveBase             {TActionGroupId::MoveBase, "MoveBase", QT_TRANSLATE_NOOP("TraversoCommands", "Move")};
    static constexpr TActionGroup ResetBase            {TActionGroupId::ResetBase, "ResetBase", QT_TRANSLATE_NOOP("TraversoCommands", "Reset")};
    static constexpr TActionGroup ToggleBypassBase     {TActionGroupId::ToggleBypassBase, "ToggleBypassBase", QT_TRANSLATE_NOOP("TraversoCommands", "Toggle Bypass")};
    static constexpr TActionGroup ToggleVerticalBase   {TActionGroupId::ToggleVerticalBase, "ToggleVerticalBase", QT_TRANSLATE_NOOP("TraversoCommands", "Toggle Vertical")};

    static constexpr std::array All{
        TNavigationBaseUp,
        TNavigationBaseDown,
        TNavigationBaseLeft,
        TNavigationBaseRight,
        DeleteBase,
        EditPropertiesBase,
        GainBase,
        MoveBase,
        ResetBase,
        ToggleBypassBase,
        ToggleVerticalBase,
    };
};

// Compares two names, stopping at the first difference, so checking all
// pairs stays well within the compilers' constexpr evaluation limits
constexpr bool t_names_are_equal(const char* left, const char* right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

// Every id has exactly one entry, in id order, and every name is unique
template <class Entry, size_t N>
constexpr bool t_entries_are_consistent(const std::array<Entry, N>& entries, size_t count)
{
    if (N != count) {
        return false;
    }
    for (size_t i = 0; i < N; ++i) {
        if (static_cast<size_t>(entries[i].id) != i) {
            return false;
        }
        for (size_t j = i + 1; j < N; ++j) {
            if (t_names_are_equal(entries[i].name, entries[j].name)) {
                return false;
            }
        }
    }
    return true;
}

static_assert(t_entries_are_consistent(TActions::All, static_cast<size_t>(TActionId::Count)),
              "TActions::All must list every TActionId once, in order, with unique names");
static_assert(t_entries_are_consistent(TActionGroups::All, static_cast<size_t>(TActionGroupId::Count)),
              "TActionGroups::All must list every TActionGroupId once, in order, with unique names");
