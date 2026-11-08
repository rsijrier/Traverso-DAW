# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the feedback-loop check of the routing (T64). The graph is
# header-only in core; the test needs only its directory, not traversocore,
# and support for the ids
add_executable(traverso_routing_graph_test test_routing_graph.cpp)
target_include_directories(traverso_routing_graph_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_routing_graph_test PRIVATE traverso_compiler_settings traverso_test_common traversosupport Qt6::Core)
add_test(NAME routing_graph COMMAND traverso_routing_graph_test)
