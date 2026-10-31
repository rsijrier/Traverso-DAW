# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# Shared building blocks without domain logic or runtime state, used by all
# Traverso libraries: time, ring buffers, debugging, the real-time markers
# and small utilities.
# Depends on Qt Core only.

add_library(traversosupport STATIC
    Debugger.h Debugger.cpp
    TClock.h
    TLog.h TLog.cpp
    TPlatform.h
    TRealTime.h
    Utils.h Utils.cpp
    TTimeRef.h TTimeRef.cpp
    TTypes.h
    defines.h
    TResamplerChoice.h
    RingBufferNPT.h
)

target_include_directories(traversosupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Private, as every link (CLAUDE.md): a target that uses this library's
# headers links Qt6::Core itself
target_link_libraries(traversosupport PRIVATE traverso_compiler_settings Qt6::Core)
