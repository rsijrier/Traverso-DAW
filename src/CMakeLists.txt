# Define the order in which child project folders are built.
# Library layers are processed first so that executable layers can link against them.
add_subdirectory(support)
add_subdirectory(dsp)
add_subdirectory(byteio)
add_subdirectory(core)
add_subdirectory(audiofileio)
add_subdirectory(waveform)
add_subdirectory(ioscheduler)
add_subdirectory(commands)
add_subdirectory(audioruntime)
add_subdirectory(plugins)
add_subdirectory(sheetcanvas)
add_subdirectory(traverso)
