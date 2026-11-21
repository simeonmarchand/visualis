set_source_files_properties(qml/LyricsTemplate.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# The UI is a QML module library so tests can load it without the app executable.
qt_add_library(visualis_ui STATIC)
qt_add_qml_module(visualis_ui
    URI Visualis.Ui
    VERSION 1.0
    # Qt's QML tooling (qmllint) expects the build folder to match the module name.
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Visualis/Ui
    QML_FILES
        qml/Main.qml
        qml/OutputWindow.qml
        qml/AudienceLyrics.qml
        qml/StageDisplay.qml
        qml/LyricsTemplate.qml
        qml/OutputSettings.qml
        qml/SongListPane.qml
        qml/PasteSongDialog.qml
        qml/SlideGrid.qml
        qml/SlideTile.qml
    SOURCES
        output/outputcontroller.h output/outputcontroller.cpp
        output/platformdisplay.h
        output/latencyprobe.h output/latencyprobe.cpp
        video/videoengine.h video/videoengine.cpp
        live/livestateqml.h
        live/commandsqml.h live/commandsqml.cpp
        library/songlibrary.h library/songlibrary.cpp
)
# The generated QML type registrations include headers by file name alone.
target_include_directories(visualis_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/output
    ${CMAKE_CURRENT_SOURCE_DIR}/video ${CMAKE_CURRENT_SOURCE_DIR}/live
    ${CMAKE_CURRENT_SOURCE_DIR}/library)
# GuiPrivate for QRhi: the video hub's own GPU context (ADR 0001, kept in videoengine.cpp).
target_link_libraries(visualis_ui PRIVATE Qt6::Quick Qt6::Multimedia Qt6::GuiPrivate visualis_core)
if(APPLE)
    enable_language(OBJCXX)
    target_sources(visualis_ui PRIVATE output/platformdisplay_mac.mm)
    target_link_libraries(visualis_ui PRIVATE "-framework AppKit")
else()
    target_sources(visualis_ui PRIVATE output/platformdisplay_other.cpp)
endif()

qt_add_executable(Visualis MACOSX_BUNDLE WIN32 main.cpp)
set_target_properties(Visualis PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.visualis.Visualis
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)
target_link_libraries(Visualis PRIVATE Qt6::Quick Qt6::Multimedia visualis_ui visualis_uiplugin visualis_core)
target_include_directories(Visualis PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
