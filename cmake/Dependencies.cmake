# ---------------------------------------------------------------------------
# Dependency Resolution via PkgConfig and Native Modules
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)

# Locate mandatory core dependencies and generate modern imported targets
pkg_check_modules(SOXR REQUIRED IMPORTED_TARGET soxr)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile>=1.1.0)
pkg_check_modules(WAVPACK REQUIRED IMPORTED_TARGET wavpack>=5.6.0)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3>=3.0.0)

# Locate mandatory audio decoders and processing libraries via pkg-config
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
pkg_check_modules(MPG123 REQUIRED IMPORTED_TARGET libmpg123>=1.33)
# FFmpeg 8.0
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat>=62 libavcodec>=62 libavutil>=60)

# Locate optional decoders and encoders
if(WANT_M4A_DECODE)
    pkg_check_modules(FAAD2 IMPORTED_TARGET faad2)
    if(FAAD2_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE M4A_DECODE_SUPPORT)
    endif()
endif()

if(WANT_M4A_ENCODE)
    pkg_check_modules(FAAC IMPORTED_TARGET faac)
    if(FAAC_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE M4A_ENCODE_SUPPORT)
    endif()
endif()

# Locate optional hardware backends and plugin formats
if(UNIX AND WANT_ALSA)
    pkg_check_modules(ALSA IMPORTED_TARGET alsa>=1.0.0)
    if(ALSA_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE ALSA_SUPPORT)
    endif()
endif()

if(UNIX AND WANT_JACK)
    pkg_check_modules(JACK IMPORTED_TARGET jack>=0.100)
    if(JACK_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE JACK_SUPPORT)
    endif()
endif()

if(UNIX AND WANT_PIPEWIRE)
    pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
    if(PIPEWIRE_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE PIPEWIRE_SUPPORT)
    endif()
endif()

if(WANT_PORTAUDIO)
    pkg_check_modules(PORTAUDIO IMPORTED_TARGET portaudio-2.0>=19)
    if(PORTAUDIO_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE PORTAUDIO_SUPPORT)
    endif()
endif()

# CoreAudio is a native macOS system framework, so no pkg-config probe is required
if(APPLE AND WANT_COREAUDIO)
    target_compile_definitions(traverso_compiler_settings INTERFACE COREAUDIO_SUPPORT)
endif()

if(WANT_LV2)
    pkg_check_modules(LIBLILV IMPORTED_TARGET lilv-0>=0.4.4)
    if(LIBLILV_FOUND)
        target_compile_definitions(traverso_compiler_settings INTERFACE LV2_SUPPORT)
    endif()
endif()

# Locate the required Qt6 framework components
find_package(Qt6 COMPONENTS Core Widgets Xml Concurrent Svg REQUIRED)
