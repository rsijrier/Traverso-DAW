# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the project file conversion from version 3 to 4. The
# converter is pure DOM work; the test compiles it and needs support for the
# ids, not traversocore
add_executable(traverso_project_file_test test_project_file_3_to_4.cpp
               ${CMAKE_SOURCE_DIR}/src/core/TProjectFileConversion3To4.cpp)
target_include_directories(traverso_project_file_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_project_file_test PRIVATE traverso_compiler_settings traverso_test_common
                      traversosupport Qt6::Core Qt6::Xml)
add_test(NAME project_file_3_to_4 COMMAND traverso_project_file_test)
