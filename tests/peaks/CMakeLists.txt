# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of the peak pyramid, the peak file, its stream and the
# waveform columns, against brute force over the samples
add_executable(traverso_peak_test test_peaks.cpp)
target_link_libraries(traverso_peak_test PRIVATE traverso_compiler_settings traverso_test_common traversowaveform traversosupport Qt6::Core)
add_test(NAME peaks COMMAND traverso_peak_test)

# Headless test of the peak build in steps: the same file as appending at
# once, sources that end early, fail or are cancelled
add_executable(traverso_peak_build_test test_peak_build.cpp)
target_link_libraries(traverso_peak_build_test PRIVATE traverso_compiler_settings traverso_test_common traversowaveform traversoaudiofileio traversobyteio traversosupport Qt6::Core)
add_test(NAME peak_build COMMAND traverso_peak_build_test)

# Headless test of the peak cache: the key, marking use, the files in use,
# the eviction; the format version, the source time tolerance, a recording
# written where its waveform looks
add_executable(traverso_peak_cache_test test_peak_cache.cpp)
target_link_libraries(traverso_peak_cache_test PRIVATE traverso_compiler_settings traversowaveform traversosupport Qt6::Core)
add_test(NAME peak_cache COMMAND traverso_peak_cache_test)

# The chunked pyramid against a separate pass over level 0, byte for byte
add_executable(traverso_peak_chunks_test test_peak_chunks.cpp)
target_link_libraries(traverso_peak_chunks_test PRIVATE traverso_compiler_settings traverso_test_common traversowaveform traversosupport Qt6::Core)
add_test(NAME peak_chunks COMMAND traverso_peak_chunks_test)

# The power-of-two grid and the device columns of a raster view, against the
# exact query and the samples
add_executable(traverso_waveform_grid_test test_waveform_grid.cpp)
target_link_libraries(traverso_waveform_grid_test PRIVATE traverso_compiler_settings traverso_test_common traversowaveform traversosupport Qt6::Core)
add_test(NAME waveform_grid COMMAND traverso_waveform_grid_test)

# Benchmark of the column query and the envelope painting, not part of ctest.
# Build it in Release, with -DBUILD_PEAK_BENCHMARK=ON.
if(BUILD_PEAK_BENCHMARK)
    add_executable(traverso_peak_benchmark benchmark_peaks.cpp)
    target_link_libraries(traverso_peak_benchmark PRIVATE traverso_compiler_settings traversowaveform traversosupport Qt6::Core Qt6::Gui)
endif()

# Measurements on real audio files (doc/design/peaks-v6-report.md), not part
# of ctest. Build it in Release.
add_executable(traverso_peak_measure EXCLUDE_FROM_ALL measure_peaks.cpp)
target_link_libraries(traverso_peak_measure PRIVATE traverso_compiler_settings traversowaveform traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core)
