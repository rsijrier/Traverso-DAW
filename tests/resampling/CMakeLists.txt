# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the resampling reader: sweeps read at another rate, the
# way the stream reader reads them, must stay where they belong
add_executable(traverso_resample_reader_test test_resample_reader.cpp sweeps.h)
target_link_libraries(traverso_resample_reader_test PRIVATE traverso_compiler_settings traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core)
# Optimised also in a debug build: unoptimised, the correlations of the
# analysis take most of its time (every quality of both backends)
target_compile_options(traverso_resample_reader_test PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-O2>)
add_test(NAME resample_reader COMMAND traverso_resample_reader_test)

# Headless test of the resampling writer: sweeps written at another rate,
# all of them in the file where they belong, the last one too
add_executable(traverso_resample_writer_test test_resample_writer.cpp sweeps.h)
target_link_libraries(traverso_resample_writer_test PRIVATE traverso_compiler_settings traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core)
add_test(NAME resample_writer COMMAND traverso_resample_writer_test)

# Headless test of the resampler's qualities: each one is what its backend
# means by it, the best first
add_executable(traverso_resampler_quality_test test_resampler_quality.cpp)
target_link_libraries(traverso_resampler_quality_test PRIVATE traverso_compiler_settings traversodsp traversosupport Qt6::Core)
add_test(NAME resampler_quality COMMAND traverso_resampler_quality_test)
