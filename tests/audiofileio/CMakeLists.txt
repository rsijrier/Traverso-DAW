# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Headless test of SFAudioWriter's header: a file still being written (after
# a kill or a crash) reads in full
add_executable(traverso_sf_writer_header_test test_sf_writer_header.cpp)
target_link_libraries(traverso_sf_writer_header_test PRIVATE traverso_compiler_settings traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core)
add_test(NAME sf_writer_header COMMAND traverso_sf_writer_header_test)

# Headless test of WavPack with any channel count: the channel mask written,
# every channel read back
add_executable(traverso_wavpack_channels_test test_wavpack_channels.cpp)
target_link_libraries(traverso_wavpack_channels_test PRIVATE traverso_compiler_settings traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core PkgConfig::WAVPACK)
add_test(NAME wavpack_channels COMMAND traverso_wavpack_channels_test)

# Headless test of the audio decoders: every format they serve, read from the
# start, after a seek and across the end of the file
add_executable(traverso_audio_decoders_test test_audio_decoders.cpp)
target_link_libraries(traverso_audio_decoders_test PRIVATE traverso_compiler_settings traverso_test_common traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core
                      PkgConfig::SNDFILE PkgConfig::MPG123)
add_test(NAME audio_decoders COMMAND traverso_audio_decoders_test)

# Headless test of the scenario decoder: every signal against its reference,
# the exact ramp, what it refuses, rates, one frame and many channels
add_executable(traverso_scenario_decoder_test test_scenario_decoder.cpp)
target_link_libraries(traverso_scenario_decoder_test PRIVATE traverso_compiler_settings traverso_test_common traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core)
add_test(NAME scenario_decoder COMMAND traverso_scenario_decoder_test)

# Headless test of TAudioReader with damaged files (scenarios with faults):
# read and seek errors, audio that ends early, NaN and inf, at the file's
# rate and resampled
add_executable(traverso_reader_faults_test test_reader_faults.cpp)
target_link_libraries(traverso_reader_faults_test PRIVATE traverso_compiler_settings traverso_test_common traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core)
add_test(NAME reader_faults COMMAND traverso_reader_faults_test)

# Headless test of why the library says a call failed: NotFound and
# CannotOpen at open, the writer's open, close and write after close
add_executable(traverso_audio_file_errors_test test_audio_file_errors.cpp)
target_link_libraries(traverso_audio_file_errors_test PRIVATE traverso_compiler_settings traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core)
add_test(NAME audio_file_errors COMMAND traverso_audio_file_errors_test)

# Headless round trip of every writer and reader: every writer x data format
# x 1, 2 and 6 channels written and read back (task T44)
add_executable(traverso_audio_round_trip_test test_audio_round_trip.cpp)
target_link_libraries(traverso_audio_round_trip_test PRIVATE traverso_compiler_settings traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core)
add_test(NAME audio_round_trip COMMAND traverso_audio_round_trip_test)

# Headless test of TAudioReader filling a reused TAudioBuffer: a 4-channel
# file after a stereo one keeps its channels apart
add_executable(traverso_reader_buffer_reuse_test test_reader_buffer_reuse.cpp)
target_link_libraries(traverso_reader_buffer_reuse_test PRIVATE traverso_compiler_settings traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core)
add_test(NAME reader_buffer_reuse COMMAND traverso_reader_buffer_reuse_test)

# Headless test of TByteSource: what a read gives and how it is counted,
# every format through a source in memory (the same samples, open, seek and
# stream apart, a read that fails), WavPack's correction file
add_executable(traverso_audio_byte_source_test test_audio_byte_source.cpp)
target_link_libraries(traverso_audio_byte_source_test PRIVATE traverso_compiler_settings traversoaudiofileio traversobyteio traversodsp traversosupport Qt6::Core
                      PkgConfig::SNDFILE)
add_test(NAME audio_byte_source COMMAND traverso_audio_byte_source_test)
