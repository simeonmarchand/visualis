# Visualis core: data model, parsing, storage. Qt Core and Qt SQL only, no UI, so it's
# testable on its own.
qt_add_library(visualis_core STATIC
    library/library.h library/library.cpp
    song/song.h song/song.cpp
    song/labels.h song/labels.cpp
    song/lyricspaste.h song/lyricspaste.cpp
    song/slidelist.h song/slidelist.cpp
    song/songlimits.h song/songlimits.cpp
    song/songmatch.h song/songmatch.cpp
    song/songimport.h song/songimport.cpp
    song/songjson.h song/songjson.cpp
    song/songtext.h song/songtext.cpp
    import/protobuf.h import/protobuf.cpp
    import/rtf.h import/rtf.cpp
    import/propresenter.h import/propresenter.cpp
    song/songstore.h song/songstore.cpp
    template/template.h template/template.cpp
    template/templatejson.h template/templatejson.cpp
    template/templatestore.h template/templatestore.cpp
    live/liveitem.h live/liveitem.cpp
    live/livestate.h live/livestate.cpp
    live/commands.h live/commands.cpp
    live/livejournal.h live/livejournal.cpp
    service/service.h service/service.cpp
    service/servicestore.h service/servicestore.cpp
    service/servicejson.h service/servicejson.cpp
    pco/pcoauth.h pco/pcoauth.cpp
    pco/pcotypes.h
    pco/pcojson.h pco/pcojson.cpp
    pco/planimport.h pco/planimport.cpp
    pco/reimport.h pco/reimport.cpp
    pco/reimportproposal.h pco/reimportproposal.cpp
    pco/songlink.h pco/songlink.cpp
    scripture/reference.h scripture/reference.cpp
    output/displayplacement.h output/displayplacement.cpp
    output/displaysettle.h output/displaysettle.cpp
    video/framewatchdog.h video/framewatchdog.cpp
)
target_include_directories(visualis_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(visualis_core PUBLIC Qt6::Core Qt6::Sql)
