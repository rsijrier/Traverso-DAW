# Define the static library for managing audio file input and output processes
add_library(traversoaudiofileio STATIC
    decode/TAudioDecoder.h decode/TAudioDecoder.cpp
    decode/SFAudioReader.cpp
    TAudioFileError.h
    TAudioIOStats.h
    TAudioReader.h TAudioReader.cpp
    decode/WPAudioReader.cpp
    decode/MadAudioReader.cpp
    decode/MpgAudioReader.cpp
    decode/TScenarioDecoder.h decode/TScenarioDecoder.cpp
    encode/TAudioEncoder.h encode/TAudioEncoder.cpp
    encode/SFAudioWriter.cpp
    encode/WPAudioWriter.cpp
    TAudioWriter.h TAudioWriter.cpp
    TAudioEncodeSettings.h
)

# Apply shared compilation variables and parameters from the interface target
target_link_libraries(traversoaudiofileio PRIVATE traverso_compiler_settings)

# Include optional implementation files depending on PkgConfig validation states
if(TARGET PkgConfig::FAAD2)
    target_sources(traversoaudiofileio PRIVATE decode/FaadAudioReader.cpp decode/minimp4.h)
endif()

if(TARGET PkgConfig::FAAC)
    target_sources(traversoaudiofileio PRIVATE encode/FaacAudioWriter.cpp)
endif()

# What the rest of Traverso reads and writes audio files with: TAudioReader
# and TAudioWriter (with TAudioEncodeSettings, and dsp's TAudioBuffer), in this
# directory. The decoders and encoders per library are the library's own.
# It never includes headers of libraries it does not link.
target_include_directories(traversoaudiofileio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/decode
        ${CMAKE_CURRENT_SOURCE_DIR}/encode
)

# Direct dependencies only, all private: consumers link what they use
# themselves. Resampling and dithering come from dsp.
target_link_libraries(traversoaudiofileio
    PRIVATE
        traversosupport
        traversodsp
        traversobyteio
        Qt6::Core
        PkgConfig::SNDFILE
        PkgConfig::WAVPACK
        PkgConfig::MAD
        PkgConfig::MPG123
)

if(TARGET PkgConfig::FAAD2)
    target_link_libraries(traversoaudiofileio PRIVATE PkgConfig::FAAD2)
endif()

if(TARGET PkgConfig::FAAC)
    target_link_libraries(traversoaudiofileio PRIVATE PkgConfig::FAAC)
endif()
