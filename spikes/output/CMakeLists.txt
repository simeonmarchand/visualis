cmake_minimum_required(VERSION 3.21)
project(visualis_output_spike LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Gui GuiPrivate Quick Multimedia)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(visualis-output-spike MACOSX_BUNDLE WIN32
    src/main.cpp
)

qt_add_qml_module(visualis-output-spike
    URI Visualis.Spike
    VERSION 1.0
    QML_FILES
        qml/Operator.qml
        qml/AudienceOutput.qml
        qml/StageOutput.qml
        qml/LyricsLayer.qml
    SOURCES
        src/eventlog.h src/eventlog.cpp
        src/livestate.h src/livestate.cpp
        src/framestats.h src/framestats.cpp
        src/videoengine.h src/videoengine.cpp
        src/outputcontroller.h src/outputcontroller.cpp
        src/scriptrunner.h src/scriptrunner.cpp
        src/macwindow.h
)

if(APPLE)
    target_sources(visualis-output-spike PRIVATE src/macwindow.mm)
    target_link_libraries(visualis-output-spike PRIVATE "-framework AppKit")
else()
    target_sources(visualis-output-spike PRIVATE src/macwindow_other.cpp)
endif()

# The generated QML type registrations include the headers by bare name.
target_include_directories(visualis-output-spike PRIVATE src)
# GuiPrivate is how Qt ships the QRhi API (rhi/qrhi.h); its compatibility promise is weaker than public API.
target_link_libraries(visualis-output-spike PRIVATE Qt6::Quick Qt6::Multimedia Qt6::GuiPrivate)

# Test tool: a software display for hot-plug and multi-display tests without hardware (tools/virtual-display.m).
if(APPLE)
    enable_language(OBJC)
    add_executable(virtual-display tools/virtual-display.m)
    target_compile_options(virtual-display PRIVATE -fobjc-arc)
    target_link_libraries(virtual-display PRIVATE "-framework Foundation" "-framework CoreGraphics")
endif()
