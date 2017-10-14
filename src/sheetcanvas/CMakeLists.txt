INCLUDE_DIRECTORIES(
${CMAKE_SOURCE_DIR}/src/commands
${CMAKE_SOURCE_DIR}/src/common
${CMAKE_SOURCE_DIR}/src/core
${CMAKE_SOURCE_DIR}/src/engine
${CMAKE_SOURCE_DIR}/src/plugins
${CMAKE_SOURCE_DIR}/src/plugins/native
${CMAKE_SOURCE_DIR}/src/plugins/LV2
${CMAKE_SOURCE_DIR}/src/3rdparty/slv2
${CMAKE_SOURCE_DIR}/src/traverso
${CMAKE_SOURCE_DIR}/src/traverso/dialogs
)

SET(TRAVERSO_SONGCANVAS_SOURCES
AudioClipView.cpp
AudioTrackView.cpp
ClipsViewPort.cpp
Cursors.cpp
CurveNodeView.cpp
CurveView.cpp
FadeCurveView.cpp
MarkerView.cpp
LineView.cpp
PluginChainView.cpp
PluginView.cpp
PositionIndicator.cpp
TrackView.cpp
TTrackLaneView.cpp
SheetView.cpp
SheetWidget.cpp
TBusTrackView.cpp
TimeLineView.cpp
TimeLineViewPort.cpp
TrackPanelView.cpp
TrackPanelViewPort.cpp
ViewItem.cpp
ViewPort.cpp
dialogs/AudioClipEditDialog.cpp
dialogs/TTrackManagerDialog.cpp
VUMeterView.cpp
TCanvasCursor.cpp
TKnobView.cpp
)


SET(TRAVERSO_SONGCANVAS_LIBRARY "traversosheetcanvas")

ADD_LIBRARY(${TRAVERSO_SONGCANVAS_LIBRARY} STATIC 
	${TRAVERSO_SONGCANVAS_SOURCES} 
)

IF(USE_PCH)
    ADD_DEPENDENCIES(traversosheetcanvas precompiled_headers)
ENDIF(USE_PCH)
