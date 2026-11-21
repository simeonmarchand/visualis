# Visualis core: data model, parsing, storage. Qt Core and Qt SQL only, no UI, so it's
# testable on its own.
qt_add_library(visualis_core STATIC
    library/library.h library/library.cpp
)
target_include_directories(visualis_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(visualis_core PUBLIC Qt6::Core Qt6::Sql)
