# Define the central data structures and engine synchronization library.
# TCommandPlugin.h is explicitly included in the sources list to force
# Qt's AUTOMOC to parse its meta-object macros and generate the required vtable symbols.
add_library(traversocore STATIC
    TAudioClip.cpp
    TAudioClipGroup.cpp
    TAudioClipManager.cpp
    TAudioClipFactory.h TAudioClipFactory.cpp
    TAudioFileCopyConvert.cpp
    TAudioFileMerger.cpp
    TAudioTrack.cpp
    TBufferedAudioStream.cpp
    TCommand.cpp
    TConfig.cpp
    TShortCutManager.cpp
    TCommandController.h TCommandController.cpp
    TContextPointer.cpp
    TContextItem.cpp
    TCurve.cpp
    TCurveNode.cpp
    TCurveSnapshot.h TCurveSnapshot.cpp
    TFadeShape.h TFadeShape.cpp
    TFadeCurve.cpp
    FileHelpers.cpp
    TInformUser.cpp
    TInputEventDispatcher.cpp
    TPeakBuildService.h TPeakBuildService.cpp
    TWaveform.h TWaveform.cpp
    TProject.cpp
    TProjectManager.cpp
    TAudioProcessingNode.cpp
    TBufferedAudioStreamReader.cpp
    TResamplerSettings.cpp
    TResourcesManager.cpp
    TRecordingLatency.h
    TRecordingCommand.h TRecordingCommand.cpp
    TBusTrack.cpp
    TSend.cpp
    TRoutingGraph.h
    TObjectIdTags.h
    TObjectPool.h
    TObjectFactory.h
    TSession.cpp
    TSheet.cpp
    TTrack.cpp
    TBufferedAudioStreamWriter.cpp
    TSnapList.cpp
    TLocation.cpp
    TTimeLineRuler.cpp
    TTimeLineMarker.cpp
    TThemer.cpp
    TProjectConverter.cpp
    TGlobalContext.cpp
    TTransport.cpp
    TShortCutFunction.cpp
    TShortCut.cpp
    TExportSpecification.cpp
    TCommandPlugin.h
    TActions.h
    TUndoCommand.h
    TViewPortScroller.h TViewPortScroller.cpp
    TTimeLineEntity.h TTimeLineEntity.cpp
    TSelectionService.h TSelectionService.cpp
)

# Bind structural build directives and environmental variables
target_link_libraries(traversocore PRIVATE traverso_compiler_settings)

# Configure systemic directory mappings for internal header parsing structures
target_include_directories(traversocore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/commands
        ${CMAKE_SOURCE_DIR}/src/plugins
        ${CMAKE_SOURCE_DIR}/src/plugins/native
)

# Connect user-interface frameworks and resampler packages to the target
target_link_libraries(traversocore
    PUBLIC
        traversosupport
        traversodsp
        traversoaudioruntime
        traversoaudiofileio
        traversowaveform
        traversoioscheduler
        Qt6::Widgets
        Qt6::Gui
        Qt6::Xml
        Qt6::Core
        Qt6::Concurrent
)
# Its stream readers use byteio's types through audiofileio's headers
target_link_libraries(traversocore PRIVATE traversobyteio)
