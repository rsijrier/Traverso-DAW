ADD_SUBDIRECTORY(plugins)

INCLUDE_DIRECTORIES(
${CMAKE_SOURCE_DIR}/src/commands
${CMAKE_SOURCE_DIR}/src/common
${CMAKE_SOURCE_DIR}/src/core
${CMAKE_SOURCE_DIR}/src/traverso
${CMAKE_SOURCE_DIR}/src/sheetcanvas
${CMAKE_SOURCE_DIR}/src/plugins
${CMAKE_SOURCE_DIR}/src/plugins/native
)

SET(TRAVERSO_COMMANDS_SOURCES
ArrowKeyBrowser.cpp
AudioClipExternalProcessing.cpp
AddRemove.cpp
ClipSelection.cpp
CommandGroup.cpp
Crop.cpp
ExternalProcessingDialog.cpp
Fade.cpp
Gain.cpp
TGainGroupCommand.cpp
Import.cpp
MoveClip.cpp
MoveCurveNode.cpp
MoveEdge.cpp
MoveMarker.cpp
MovePlugin.cpp
MoveTrack.cpp
PCommand.cpp
SplitClip.cpp
TrackPan.cpp
Zoom.cpp
Scroll.cpp
Shuttle.cpp
ArmTracks.cpp
PlayHeadMove.cpp
WorkCursorMove.cpp
RemoveClip.cpp
MoveCommand.cpp
)

SET(TRAVERSO_COMMANDS_UI_FILES
ui/ExternalProcessingDialog.ui
)

SET(TRAVERSO_COMMANDS_LIBRARY "traversocommands")

ADD_LIBRARY(${TRAVERSO_COMMANDS_LIBRARY} STATIC  ${TRAVERSO_COMMANDS_SOURCES})

TARGET_LINK_LIBRARIES(traversocommands
	traversocore
)

IF(USE_PCH)
    ADD_DEPENDENCIES(traversocommands precompiled_headers)
ENDIF(USE_PCH)
