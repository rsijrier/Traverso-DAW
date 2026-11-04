# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the TraversoDAW::Float comparisons
add_executable(traverso_float_test test_float.cpp)

target_link_libraries(traverso_float_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core)

add_test(NAME float_compare COMMAND traverso_float_test)

# Headless test of the log: lock-free records from several threads, written
# out in time order, dropped records counted, deferred formatting
add_executable(traverso_log_test test_log.cpp)
target_link_libraries(traverso_log_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core)
add_test(NAME log COMMAND traverso_log_test)

# Headless test of TTimeRef conversions
add_executable(traverso_timeref_test test_timeref.cpp)
target_link_libraries(traverso_timeref_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core)
add_test(NAME timeref COMMAND traverso_timeref_test)

# Headless test of TObjectId: conversions by the compiler, accepted values,
# the same ids for the same steps, numbers increasing in creation order
add_executable(traverso_object_id_test test_object_id.cpp)
target_link_libraries(traverso_object_id_test PRIVATE traverso_compiler_settings traversosupport Qt6::Core)
add_test(NAME object_id COMMAND traverso_object_id_test)
