# Visualis core: data model, parsing, storage. Qt Core only, no UI, so it's testable on its own.
# Header-only until the first sources land (#11, #12).
add_library(visualis_core INTERFACE)
target_include_directories(visualis_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(visualis_core INTERFACE Qt6::Core)
