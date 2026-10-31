# SPDX-FileCopyrightText: 2026 Remon Sijrier
# SPDX-License-Identifier: GPL-2.0-or-later

# The waveform data of an audio file: made (TPeakStream, TPeakBuildTask),
# stored (TPeakPyramid, TPeakFile, TPeakCache) and asked for by the views
# (TWaveformColumns). Peak files are its storage. It knows no project, clip
# or engine: TWaveform in core connects a clip to it.
add_library(traversowaveform STATIC
    TPeakPyramid.h TPeakPyramid.cpp
    TPeakFile.h TPeakFile.cpp
    TPeakCache.h TPeakCache.cpp
    TPeakStream.h TPeakStream.cpp
    TPeakBuildTask.h TPeakBuildTask.cpp
    TWaveformColumns.h TWaveformColumns.cpp
)

target_link_libraries(traversowaveform PRIVATE traverso_compiler_settings)

# Only its own directory: it never includes headers of libraries it does
# not link
target_include_directories(traversowaveform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Direct dependencies only, all private: consumers link what they use
# themselves. A build decodes with audiofileio, whose buffers come from dsp.
target_link_libraries(traversowaveform
    PRIVATE
        traversosupport
        traversodsp
        traversoaudiofileio
        traversobyteio
        Qt6::Core
)
