INCLUDE_DIRECTORIES(
${CMAKE_SOURCE_DIR}/src/audiofileio/decode
${CMAKE_SOURCE_DIR}/src/audiofileio/encode
${CMAKE_SOURCE_DIR}/src/commands
${CMAKE_SOURCE_DIR}/src/common
${CMAKE_SOURCE_DIR}/src/engine
${CMAKE_SOURCE_DIR}/src/plugins
${CMAKE_SOURCE_DIR}/src/plugins/native
)

SET(TRAVERSO_CORE_SOURCES
${CMAKE_SOURCE_DIR}/src/common/Utils.cpp
${CMAKE_SOURCE_DIR}/src/common/Tsar.cpp
${CMAKE_SOURCE_DIR}/src/common/Debugger.cpp
${CMAKE_SOURCE_DIR}/src/common/Mixer.cpp
${CMAKE_SOURCE_DIR}/src/common/Resampler.cpp
${CMAKE_SOURCE_DIR}/src/common/TTimeRef.cpp
${CMAKE_SOURCE_DIR}/src/common/TTransportControl.cpp

AudioClip.cpp
AudioClipGroup.cpp
AudioClipManager.cpp
AudioFileCopyConvert.cpp
AudioFileMerger.cpp
AudioTrack.cpp
AudioSource.cpp
AbstractViewPort.cpp
TCommand.cpp
TConfig.cpp
TShortCutManager.cpp
ContextPointer.cpp
ContextItem.cpp
Curve.cpp
CurveNode.cpp
TCommandPlugin.h
DiskIO.cpp
TExportThread.cpp TExportThread.h
FadeCurve.cpp
FileHelpers.cpp
Information.cpp
TInputEventDispatcher.cpp
Peak.cpp
Project.cpp
ProjectManager.cpp
TAudioProcessingNode.cpp
ReadSource.cpp
ResourcesManager.cpp
TBusTrack.cpp
TSend.cpp
TSession.cpp
Sheet.cpp
Track.cpp
WriteSource.cpp
gdither.cpp
SnapList.cpp
TLocation.cpp
TTimeLineRuler.cpp
Marker.cpp
Themer.cpp
ProjectConverter.cpp
TGlobalContext.cpp
)

IF(UNIX)
	IF(HOST_SUPPORTS_SSE)
		IF(IS_ARCH_X86)
			SET(ASM_FILE ${CMAKE_SOURCE_DIR}/src/common/sse_functions.S)
		ENDIF(IS_ARCH_X86)
		IF(IS_ARCH_X86_64)
			SET(ASM_FILE ${CMAKE_SOURCE_DIR}/src/common/sse_functions_64bit.S)
		ENDIF(IS_ARCH_X86_64)
		
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sse_functions.o
			COMMAND as ${ASM_FILE} -o ${CMAKE_CURRENT_BINARY_DIR}/sse_functions.o
			DEPENDS ${ASM_FILE}
		)
		SET(SSE_OBJECT_FILE ${CMAKE_CURRENT_BINARY_DIR}/sse_functions.o)
	ENDIF(HOST_SUPPORTS_SSE)
ENDIF(UNIX)


SET(TRAVERSO_CORE_LIBRARY "traversocore")

ADD_LIBRARY(${TRAVERSO_CORE_LIBRARY} STATIC ${TRAVERSO_CORE_SOURCES} ${SSE_OBJECT_FILE}
	TShortCutFunction.h TShortCutFunction.cpp
	TShortCut.h TShortCut.cpp
	TExportSpecification.h TExportSpecification.cpp)

IF(USE_PCH)
    ADD_DEPENDENCIES(traversocore precompiled_headers)
ENDIF(USE_PCH)
