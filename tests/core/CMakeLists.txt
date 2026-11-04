# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of TObjectPool, header only: core's other parts are not linked
add_executable(traverso_object_pool_test test_object_pool.cpp)
target_include_directories(traverso_object_pool_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_object_pool_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core)
add_test(NAME object_pool COMMAND traverso_object_pool_test)

# Headless test of TObjectFactory, header only: make, load, reload and the
# validation of loaded ids
add_executable(traverso_object_factory_test test_object_factory.cpp)
target_include_directories(traverso_object_factory_test PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_link_libraries(traverso_object_factory_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core Qt6::Xml)
add_test(NAME object_factory COMMAND traverso_object_factory_test)
