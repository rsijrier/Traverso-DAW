# ---------------------------------------------------------------------------
# Specialized Compiler and Linker Flags (Aligned with Debian Sid Production)
# ---------------------------------------------------------------------------

# Probe for Large File Support flags via system configuration
find_program(GETCONF_EXECUTABLE getconf)
if(GETCONF_EXECUTABLE)
    execute_process(
        COMMAND ${GETCONF_EXECUTABLE} LFS_CFLAGS
        OUTPUT_VARIABLE LFS_FLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

# Base warnings and Large File Support (Always active across all build profiles)
target_compile_options(traverso_compiler_settings INTERFACE -Wall -Wextra -Wno-unused-local-typedefs ${LFS_FLAGS})

# ThreadSanitizer for everything built with these settings, the tests
# included (the peak tests run threads against the lock-free peak code).
# For a CI job or by hand, in a build directory of its own. Qt is not
# instrumented: a report whose racing accesses are both operator new/delete
# inside libQt6Core (around QThread and its queues) is Qt's own, not ours. No
# suppression file: every thread starts in Qt, so race:libQt6Core hides all
# races and called_from_lib leaves the threads with ignores on.
option(TRAVERSO_TSAN "Build with ThreadSanitizer" OFF)
# One sanitizer per build: ThreadSanitizer does not combine with the
# RealtimeSanitizer or AddressSanitizer (each in a build directory of its own)
set(TRAVERSO_SANITIZER_COUNT 0)
foreach(sanitizer TRAVERSO_TSAN ENABLE_RTSAN ENABLE_ASAN)
    if(${sanitizer})
        math(EXPR TRAVERSO_SANITIZER_COUNT "${TRAVERSO_SANITIZER_COUNT} + 1")
    endif()
endforeach()
if(TRAVERSO_SANITIZER_COUNT GREATER 1)
    message(FATAL_ERROR "TRAVERSO_TSAN, ENABLE_RTSAN and ENABLE_ASAN: one per build directory")
endif()
if(TRAVERSO_TSAN)
    target_compile_options(traverso_compiler_settings INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(traverso_compiler_settings INTERFACE -fsanitize=thread)
endif()

# The real-time requirement (doc/design/realtime-audio-path.md): Clang checks
# every function marked T_REALTIME_SAFE (TRealTime.h). Warnings while the
# audio processing path is being marked; an error once it is clean.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(traverso_compiler_settings INTERFACE -Wfunction-effects)
endif()

# RealtimeSanitizer (Clang 20 and later, development only): while a
# T_REALTIME_SAFE function runs, an allocation, a lock, a sleep or a blocking
# call anywhere below it is reported with a stack trace. In a build
# directory of its own; run the headless tests on the null driver.
option(ENABLE_RTSAN "Build with RealtimeSanitizer (Clang 20 and later)" OFF)
if(ENABLE_RTSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20)
        message(FATAL_ERROR "ENABLE_RTSAN needs Clang 20 or later")
    endif()
    target_compile_options(traverso_compiler_settings INTERFACE -fsanitize=realtime -fno-omit-frame-pointer)
    target_link_options(traverso_compiler_settings INTERFACE -fsanitize=realtime)
endif()

# ---------------------------------------------------------------------------
# Debian Sid Production Hardening and LTO Optimization (STRICTLY RELEASE ONLY)
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE MATCHES "[Dd]ebug" AND NOT WANT_DEBUG)

    # 1. Debian Default Optimization Profile
    target_compile_options(traverso_compiler_settings INTERFACE -O2)

    # 2. Debian Security Hardening - Compiler Framework Guards
    target_compile_options(traverso_compiler_settings INTERFACE
        -g                          # Retain debug symbols for detached dbg symbol split-packages
        -fstack-protector-strong    # Proactive stack smashing protection (Debian packaging standard)
        -Wformat                    # Verify string format layouts for printf-style methods
        -Werror=format-security     # Reject dangerous format-string vulnerabilities at compile time
        -fPIE                       # Position Independent Executable to enable robust ASLR mitigation
    )

    # 3. Debian Security Hardening - Preprocessor Definitions
    target_compile_definitions(traverso_compiler_settings INTERFACE
        _FORTIFY_SOURCE=3           # Heavy buffer boundary runtime validation (GCC 14/15 default)
    )

    # 4. Debian Security Hardening - Linker Security Guards
    # -z relro / -z now are ELF linker options; ld64 on macOS rejects them
    # (Xcode already maps __DATA_CONST read-only in the default layout).
    if(NOT APPLE)
        target_link_options(traverso_compiler_settings INTERFACE
            "LINKER:-z,relro"           # Enforce Read-Only Relocations to secure GOT assignment scopes
            "LINKER:-z,now"             # Trigger immediate binding to eliminate runtime GOT exploitation vectors
            "-pie"                      # Enforce dynamic address space randomization execution mapping
        )
    endif()

    # 5. Link Time Optimization (LTO)
    include(CheckIPOSupported)

    check_ipo_supported(RESULT lto_supported OUTPUT error)
    if(lto_supported)
        # Actively enforce LTO parallel multi-core link bindings (matching -flto=auto)
        set_target_properties(traverso_compiler_settings PROPERTIES
            INTERFACE_INTERPROCEDURAL_OPTIMIZATION TRUE
        )
        # Apply the -ffat-lto-objects flag required by Debian packaging guidelines
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(traverso_compiler_settings INTERFACE -ffat-lto-objects)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Optional AddressSanitizer Allocation Tracking (Development Diagnostics Only)
# ---------------------------------------------------------------------------
if(ENABLE_ASAN)
    target_compile_options(traverso_compiler_settings INTERFACE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(traverso_compiler_settings INTERFACE -fsanitize=address)
endif()
