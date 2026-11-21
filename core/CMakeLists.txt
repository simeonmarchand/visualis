# Visualis core: data model, parsing, storage. Qt Core and Qt SQL only, no UI, so it's
# testable on its own.
qt_add_library(visualis_core STATIC
    library/library.h library/library.cpp
    song/song.h song/song.cpp
    song/labels.h song/labels.cpp
    song/lyricspaste.h song/lyricspaste.cpp
    song/slidelist.h song/slidelist.cpp
    song/songjson.h song/songjson.cpp
    import/protobuf.h import/protobuf.cpp
    import/rtf.h import/rtf.cpp
    import/propresenter.h import/propresenter.cpp
    song/songstore.h song/songstore.cpp
    live/livestate.h live/livestate.cpp
    live/commands.h live/commands.cpp
    output/displayplacement.h output/displayplacement.cpp
    output/displaysettle.h output/displaysettle.cpp
    video/framewatchdog.h video/framewatchdog.cpp
)
target_include_directories(visualis_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(visualis_core PUBLIC Qt6::Core Qt6::Sql)
