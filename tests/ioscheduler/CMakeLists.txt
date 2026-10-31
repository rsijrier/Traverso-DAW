# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the scheduler's planner: order, hysteresis, seeks, the
# free threads and the idle budget. No core; support and Qt Core for the
# stream state's header (TIOStream.h, its time types).
add_executable(traverso_io_planner_test test_io_planner.cpp)
target_link_libraries(traverso_io_planner_test PRIVATE traverso_compiler_settings traversoioscheduler traversosupport Qt6::Core)
add_test(NAME io_planner COMMAND traverso_io_planner_test)

# Headless test of the scheduler with streams and tasks of its own: rounds
# without a driver, a stream removed while rounds run on the scheduler
# thread, the end with tasks running, a seek and a new rate. No core: the
# library and what its headers include (support, Qt Core).
add_executable(traverso_io_scheduler_test test_io_scheduler.cpp)
target_link_libraries(traverso_io_scheduler_test PRIVATE traverso_compiler_settings traversoioscheduler traversosupport Qt6::Core)
add_test(NAME io_scheduler COMMAND traverso_io_scheduler_test)

# A measurement, not part of ctest (dispatcher-conclusie.md, acceptance):
# the peak builds of a directory of audio files as idle work. Build it in
# Release.
add_executable(traverso_measure_idle_builds EXCLUDE_FROM_ALL measure_idle_builds.cpp)
target_link_libraries(traverso_measure_idle_builds PRIVATE traverso_compiler_settings traversoioscheduler traversowaveform traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core)
