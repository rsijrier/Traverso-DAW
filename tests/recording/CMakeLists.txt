# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the recording latency compensation
add_executable(traverso_recording_latency_test test_recording_latency.cpp)
target_include_directories(traverso_recording_latency_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_recording_latency_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core)
add_test(NAME recording_latency COMMAND traverso_recording_latency_test)

# Headless test of a recording session's undo step: first redo, undo and
# redo, the discard when it leaves the stack undone, the explicit discard
add_executable(traverso_recording_command_test test_recording_command.cpp ${CMAKE_SOURCE_DIR}/src/core/TRecordingCommand.cpp)
target_include_directories(traverso_recording_command_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_recording_command_test PRIVATE traverso_compiler_settings Qt6::Gui Qt6::Core)
add_test(NAME recording_command COMMAND traverso_recording_command_test)
