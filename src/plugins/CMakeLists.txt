INCLUDE_DIRECTORIES(
${CMAKE_SOURCE_DIR}/src/common
${CMAKE_SOURCE_DIR}/src/core
${CMAKE_SOURCE_DIR}/src/engine
${CMAKE_SOURCE_DIR}/src/commands
${CMAKE_SOURCE_DIR}/src/plugins/native
${CMAKE_SOURCE_DIR}/src/plugins/LV2
)

IF(USE_INTERNAL_SLV2_LIB)
	INCLUDE_DIRECTORIES(
	${CMAKE_SOURCE_DIR}/src/3rdparty/slv2
	)
ENDIF(USE_INTERNAL_SLV2_LIB)

SET(TRAVERSO_PLUGINS_SOURCES
PluginChain.cpp
Plugin.cpp
PluginManager.cpp
PluginSlider.cpp
native/CorrelationMeter.cpp
native/SpectralMeter.cpp
native/GainEnvelope.cpp
PluginPropertiesDialog.cpp
)

IF(HAVE_LILV)
	SET(TRAVERSO_PLUGINS_SOURCES
	${TRAVERSO_PLUGINS_SOURCES}
	LV2/LV2Plugin.cpp
	)
	SET(TRAVERSO_PLUGINS_MOC_CLASSES
	LV2/LV2Plugin.h
	)
ENDIF(HAVE_LILV)

SET(TRAVERSO_PLUGINS_LIBRARY "traversoplugins")


ADD_LIBRARY(${TRAVERSO_PLUGINS_LIBRARY} STATIC ${TRAVERSO_PLUGINS_SOURCES})

IF(USE_PCH)
    ADD_DEPENDENCIES(traversoplugins precompiled_headers)
ENDIF(USE_PCH)
