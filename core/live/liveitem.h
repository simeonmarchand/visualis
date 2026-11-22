#pragma once

#include "service/service.h"
#include "song/slidelist.h"
#include "song/song.h"

#include <optional>

namespace visualis {

// A service item as LiveState steps through it. Everything that shows becomes a song, so
// the outputs, the journal, and recovery handle one shape:
//  - a song item is its song, with this service's arrangement, or its custom sequence
//    (Planning Center's order for the day) added as an arrangement of its own;
//  - a text item is a one-slide song titled as the item (an empty body is a blank slide);
//  - headers, placeholders, and kinds that can't show yet have no slides, and Next and
//    Previous skip them;
//  - a song over the size limits (songlimits.h) has no slides either, and `error` says why.
struct LiveItem {
    QUuid id;            // the service item's
    Song song;
    QUuid arrangement;
    QList<SongSlide> slides;
    QString error;

    bool operator==(const LiveItem &) const = default;
};

// `song` is the item's song from the library, for song items; without it a song item has
// no slides.
LiveItem liveItem(const ServiceItem &item, const std::optional<Song> &song);

} // namespace visualis
