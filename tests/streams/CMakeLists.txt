# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the stream status (core, header only): when a stream needs
# work from the scheduler
add_executable(traverso_stream_status_test test_stream_status.cpp)
target_include_directories(traverso_stream_status_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_stream_status_test PRIVATE traverso_compiler_settings Qt6::Core)
add_test(NAME stream_status COMMAND traverso_stream_status_test)

# Headless test of the seeks of a stream reader on the real scheduler: the
# stream's own sources, not traversocore (test_stream_seek.cpp has the little
# it needs of it)
add_executable(traverso_stream_seek_test test_stream_seek.cpp
               ${CMAKE_SOURCE_DIR}/src/core/TBufferedAudioStream.h ${CMAKE_SOURCE_DIR}/src/core/TBufferedAudioStream.cpp
               ${CMAKE_SOURCE_DIR}/src/core/TBufferedAudioStreamReader.h ${CMAKE_SOURCE_DIR}/src/core/TBufferedAudioStreamReader.cpp)
get_target_property(TRAVERSO_CORE_INCLUDES traversocore INCLUDE_DIRECTORIES)
target_include_directories(traverso_stream_seek_test PRIVATE ${TRAVERSO_CORE_INCLUDES})
target_link_libraries(traverso_stream_seek_test PRIVATE traverso_compiler_settings traversoioscheduler traversoaudiofileio traversobyteio traversoaudioruntime traversowaveform traversodsp traversosupport Qt6::Core Qt6::Gui Qt6::Xml)
add_test(NAME stream_seek COMMAND traverso_stream_seek_test)
