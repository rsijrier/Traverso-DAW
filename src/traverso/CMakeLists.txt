# Define the graphical user interface source files for the main application executable.
set(TRAVERSO_GUI_SOURCES
    Main.cpp
    TMainWindow.cpp
    Traverso.cpp
    dialogs/settings/Pages.cpp
    dialogs/settings/SettingsDialog.cpp
    dialogs/project/ProjectManagerDialog.cpp
    dialogs/InsertSilenceDialog.cpp
    dialogs/MarkerDialog.cpp
    dialogs/project/NewSheetDialog.cpp
    dialogs/project/NewProjectDialog.cpp
    dialogs/project/OpenProjectDialog.cpp
    dialogs/project/NewTrackDialog.cpp
    dialogs/project/ImportClipsDialog.cpp
    dialogs/RestoreProjectBackupDialog.cpp
    dialogs/PluginSelectorDialog.cpp
    dialogs/ProjectConverterDialog.cpp
    dialogs/ExportDialog.cpp
    dialogs/CDWritingDialog.cpp
    dialogs/ThemeModifierDialog.cpp
    dialogs/TTrackSelector.cpp
    dialogs/TShortcutEditorDialog.cpp
    widgets/TAudioBusVUMonitorWidget.cpp
    widgets/CorrelationMeterWidget.cpp
    widgets/ExportFormatOptionsWidget.cpp
    widgets/InfoWidgets.cpp
    widgets/MessageWidget.cpp
    widgets/MeterWidget.cpp
    widgets/ResourcesWidget.cpp
    widgets/SpectralMeterWidget.cpp
    widgets/SpectralMeterConfigWidget.cpp
    widgets/SpectralMeterView.cpp
    widgets/CorrelationMeterView.cpp
    widgets/MeterView.cpp
    widgets/TransportConsoleWidget.cpp
    widgets/TVUMeterWidget.cpp
    widgets/WelcomeWidget.cpp
    widgets/TSessionTabWidget.cpp
    widgets/TContextHelpWidget.cpp
)

# Define the primary application executable target and embed the Qt resource files
add_executable(traverso
    ${TRAVERSO_GUI_SOURCES}
    ../../resources/traverso.qrc
)

# Inherit global compiler configurations, flags, and C++17 standards
target_link_libraries(traverso PRIVATE traverso_compiler_settings)

# Configure compiler include path mappings for the local directory scope
target_include_directories(traverso PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Link internal static sub-libraries and fundamental application dependencies smoothly
target_link_libraries(traverso
    PRIVATE
        traversocore
        traversoaudioruntime
        traversoaudiofileio
        traversobyteio
        traversoplugins
        traversocommands
        tcp_traversocommands
        traversosheetcanvas
        Qt6::Widgets
        Qt6::Xml
        Qt6::Svg
        Qt6::Core
)

# System-level dependencies required for building on particular OS environments
if(WIN32)
    target_link_libraries(traverso PRIVATE mp3lame brotlidec brotlicommon jbig lerc bz2 lzma deflate zstd tiff mpg123 opus winmm soxr samplerate)
elseif(APPLE)
    target_link_libraries(traverso PRIVATE "-framework CoreFoundation" "-framework Accelerate" "-framework ApplicationServices" dl)
else()
    target_link_libraries(traverso PRIVATE dl)
endif()

# Installation target rule utilizing standard GNU directory variables
install(TARGETS traverso RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ---------------------------------------------------------------------------
# Desktop Integration & Desktop Menu Icons
# ---------------------------------------------------------------------------

# Target the resources directory relative to the current directory scope to ensure clean resolution
set(RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../resources")

# Install the upstream application menu entry file to the correct system location
install(FILES ${RESOURCE_DIR}/traverso.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

# Install standard application icons across core resolution profiles
install(FILES ${RESOURCE_DIR}/images/icons/64x64/traverso.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/64x64/apps
)
install(FILES ${RESOURCE_DIR}/images/icons/32x32/traverso.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps
)
install(FILES ${RESOURCE_DIR}/images/icons/16x16/traverso.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/16x16/apps
)
