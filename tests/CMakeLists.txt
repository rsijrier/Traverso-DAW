# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# What the tests share, headers only: the signals and the seed of their own
# randomness (common/)
add_library(traverso_test_common INTERFACE)
target_include_directories(traverso_test_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)

add_subdirectory(support)
add_subdirectory(dsp)
add_subdirectory(audiofileio)
add_subdirectory(curves)
add_subdirectory(audioruntime)
add_subdirectory(peaks)
add_subdirectory(ioscheduler)
add_subdirectory(streams)
add_subdirectory(resampling)
add_subdirectory(recording)
add_subdirectory(routing)
add_subdirectory(core)

# Every key resolves to the same function as in the reference table, and the
# built-in keymaps have no new problems: the app itself, offscreen, with an
# empty configuration (tests/shortcut-parity/check.sh)
add_test(NAME shortcut_parity COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/shortcut-parity/check.sh $<TARGET_FILE:traverso>)

# Under ThreadSanitizer every test gets tests/tsan.supp (reports inside Qt and
# glib, which are not instrumented; see the file), and no debuginfod: a
# report's symbolizer waiting on a download stalls the audio thread long
# enough for its watchdog to abort the test. Setting a test property in
# another directory needs CMake 3.28.
if(TRAVERSO_TSAN)
    if(CMAKE_VERSION VERSION_LESS 3.28)
        message(FATAL_ERROR "TRAVERSO_TSAN tests need CMake 3.28 or later (test properties per directory)")
    endif()
    get_property(test_dirs DIRECTORY PROPERTY SUBDIRECTORIES)
    foreach(test_dir IN LISTS test_dirs)
        get_property(dir_tests DIRECTORY ${test_dir} PROPERTY TESTS)
        if(dir_tests)
            set_property(TEST ${dir_tests} DIRECTORY ${test_dir} APPEND PROPERTY ENVIRONMENT
                "TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan.supp" "DEBUGINFOD_URLS=")
        endif()
    endforeach()
endif()
