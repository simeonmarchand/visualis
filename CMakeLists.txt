cmake_minimum_required(VERSION 3.21)
project(visualis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Sql Test)
qt_standard_project_setup(REQUIRES 6.8)

enable_testing()

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(tests)
if(APPLE)
    add_subdirectory(tools)
endif()

# spikes/ are throwaway prototypes with their own builds; they're not part of the app.
