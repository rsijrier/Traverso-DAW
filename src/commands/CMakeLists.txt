# Define the static library that handles application command logic and event processing
add_library(traversocommands STATIC
    ArrowKeyBrowser.cpp
    AudioClipExternalProcessing.cpp
    TAddRemoveCommand.cpp
    ClipSelection.cpp
    CommandGroup.cpp
    Crop.cpp
    ExternalProcessingDialog.cpp
    Fade.cpp
    TFaderCommand.cpp
    TFaderGroupCommand.cpp
    TFadeRangeCommand.cpp
    MoveClip.cpp
    MoveCurveNode.cpp
    MoveEdge.cpp
    MoveMarker.cpp
    MovePlugin.cpp
    MoveTrack.cpp
    PCommand.cpp
    TAudioClipDualTrimCommand.cpp
    TrackPan.cpp
    TZoomCommand.cpp
    ArmTracks.cpp
    PlayHeadMove.cpp
    WorkCursorMove.cpp
    TMoveCommand.cpp
    TAddClipsController.h TAddClipsController.cpp
    TRemoveEntitiesController.h TRemoveEntitiesController.cpp
    TSplitEntityController.h TSplitEntityController.cpp
)

# Inherit unified build parameters and standard configurations
target_link_libraries(traversocommands PRIVATE traverso_compiler_settings)

# Configure include directory scopes for internal compilation and linking targets
target_include_directories(traversocommands
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/traverso
        ${CMAKE_SOURCE_DIR}/src/traverso/dialogs
        ${CMAKE_SOURCE_DIR}/src/sheetcanvas
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/audiofileio
)

# Link essential runtime libraries and external application frameworks
target_link_libraries(traversocommands
    PUBLIC
        Qt6::Core
        Qt6::Xml
    PRIVATE
        Qt6::Widgets
        Qt6::Gui
)

add_subdirectory(plugins)

