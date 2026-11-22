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
        qml/PlanningCenterDialog.qml
        qml/PlanPickerDialog.qml
        qml/ReimportDialog.qml
        qml/SongListPane.qml
        qml/ServicePane.qml
        qml/NameDialog.qml
        qml/PasteSongDialog.qml
        qml/ImportReportDialog.qml
        qml/MediaPane.qml
        qml/MediaDeleteDialog.qml
        qml/SlideGrid.qml
        qml/SlideTile.qml
        qml/SongEditorWindow.qml
    SOURCES
        output/outputcontroller.h output/outputcontroller.cpp
        output/platformdisplay.h
        output/latencyprobe.h output/latencyprobe.cpp
        video/videoengine.h video/videoengine.cpp
        live/livestateqml.h
        live/commandsqml.h live/commandsqml.cpp
        live/recovery.h live/recovery.cpp
        pco/credentialstore.h
        pco/planningcenter.h pco/planningcenter.cpp
        pco/pcoclient.h pco/pcoclient.cpp
        pco/planimporter.h pco/planimporter.cpp
        library/songlibrary.h library/songlibrary.cpp
        library/songeditor.h library/songeditor.cpp
        library/servicelist.h library/servicelist.cpp
        library/medialibrary.h library/medialibrary.cpp
        media/mediaprobe.h media/mediaprobe.cpp
)
# The generated QML type registrations include headers by file name alone.
target_include_directories(visualis_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/output
    ${CMAKE_CURRENT_SOURCE_DIR}/video ${CMAKE_CURRENT_SOURCE_DIR}/live
    ${CMAKE_CURRENT_SOURCE_DIR}/library ${CMAKE_CURRENT_SOURCE_DIR}/pco)
# GuiPrivate for QRhi: the video hub's own GPU context (ADR 0001, kept in videoengine.cpp).
target_link_libraries(visualis_ui PRIVATE Qt6::Quick Qt6::Multimedia Qt6::Network Qt6::GuiPrivate visualis_core)
if(APPLE)
    enable_language(OBJCXX)
    target_sources(visualis_ui PRIVATE output/platformdisplay_mac.mm pco/credentialstore_mac.cpp media/mediaprobe_mac.mm)
    target_link_libraries(visualis_ui PRIVATE "-framework AppKit" "-framework IOKit" "-framework Security"
        "-framework AVFoundation" "-framework CoreMedia" "-framework VideoToolbox")
    # The probe hands AVFoundation objects between blocks; ARC keeps them alive.
    set_source_files_properties(media/mediaprobe_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
else()
    target_sources(visualis_ui PRIVATE output/platformdisplay_other.cpp pco/credentialstore_other.cpp
        media/mediaprobe_other.cpp)
endif()

qt_add_executable(Visualis MACOSX_BUNDLE WIN32 main.cpp)
set_target_properties(Visualis PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.visualis.Visualis
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)
target_link_libraries(Visualis PRIVATE Qt6::Quick Qt6::Multimedia visualis_ui visualis_uiplugin visualis_core)
target_include_directories(Visualis PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
