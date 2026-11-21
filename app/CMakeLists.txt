# The UI is a QML module library so tests can load it without the app executable.
qt_add_library(visualis_ui STATIC)
qt_add_qml_module(visualis_ui
    URI Visualis.Ui
    VERSION 1.0
    # Qt's QML tooling (qmllint) expects the build folder to match the module name.
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Visualis/Ui
    QML_FILES
        qml/Main.qml
)
target_link_libraries(visualis_ui PRIVATE Qt6::Quick visualis_core)

qt_add_executable(Visualis MACOSX_BUNDLE WIN32 main.cpp)
set_target_properties(Visualis PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.visualis.Visualis
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)
target_link_libraries(Visualis PRIVATE Qt6::Quick visualis_ui visualis_uiplugin visualis_core)
