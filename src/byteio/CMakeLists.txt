# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Byte IO: the bytes of files, their positions and the medium they are on,
# every read timed and counted (README.md). It knows nothing of what the
# bytes are; audiofileio builds on it, never the other way.
add_library(traversobyteio STATIC
    TByteSource.h TByteSource.cpp
    TReadsByOrigin.h
)

target_include_directories(traversobyteio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Direct dependencies only, all private
target_link_libraries(traversobyteio
    PRIVATE
        traverso_compiler_settings
        traversosupport
        Qt6::Core
)
