#pragma once

#include "service/service.h"
#include "song/slidelist.h"
#include "song/song.h"
#include "template/template.h"

#include <QHash>
#include <QObject>
#include <QVariantMap>

#include <optional>

namespace visualis {

// A service item as LiveState steps through it. Everything that shows becomes a song, so
// the outputs, the journal, and recovery handle one shape:
//  - a song item is its song, with this service's arrangement, or its custom sequence
//    (Planning Center's order for the day, matched to groups by label) added as an
//    arrangement of its own, with a blank group for each lyric-less step the song has no
//    group for (Intro, Ending);
//  - a text item is a one-slide song titled as the item (an empty body is a blank slide);
//  - headers, placeholders, and kinds that can't show yet have no slides, and Next and
//    Previous skip them;
//  - a song over the size limits (songlimits.h) has no slides either, and `error` says why.
// One region of a slide's content: its text and its style with overrides applied.
struct ContentRegion {
    Q_GADGET
    Q_PROPERTY(QString text MEMBER text)
    Q_PROPERTY(visualis::RegionStyle style MEMBER style)

public:
    QString text;
    RegionStyle style;

    bool operator==(const ContentRegion &) const = default;
};

// A slide's text layer (GLOSSARY.md "Content layer"): every region of the item's template,
// by region id, with the text in "main", and the transition it changes with.
struct Content {
    Q_GADGET
    // For QML: content.regions.main.style.fontFamily.
    Q_PROPERTY(QVariantMap regions READ regionMap)
    Q_PROPERTY(visualis::Transition transition MEMBER transition)

public:
    QHash<QString, ContentRegion> regions;
    Transition transition;

    bool operator==(const Content &) const = default;

    QVariantMap regionMap() const;
};

// A slide as it will show (GLOSSARY.md "LiveSlide"): its place in the song plus its layers.
struct LiveSlide {
    SongSlide slide;
    Content content;

    bool operator==(const LiveSlide &) const = default;
};

struct LiveItem {
    QUuid id;            // the service item's; null for a song taken live from the library
    ServiceItem source;  // the service item it came from, overrides and all; empty for a library song
    Song song;
    QUuid arrangement;
    QList<LiveSlide> slides;
    QString error;
    Template tmpl;                 // the template it renders with, kept for the journal
    bool templateFellBack = false; // the one it asked for wasn't available
    // What went wrong rendering it (a missing template, bad override values), each once,
    // for the operator. The item still shows.
    QStringList warnings;

    bool operator==(const LiveItem &) const = default;

    QUuid templateId() const { return tmpl.id; }
};

// `song` is the item's song from the library, for song items; without it a song item has
// no slides. `templates` must include the built-ins (templateSet()).
LiveItem liveItem(const ServiceItem &item, const std::optional<Song> &song, const TemplateSet &templates);
// A song taken live from the library, not from a service.
LiveItem liveSong(const Song &song, const QUuid &arrangement, const TemplateSet &templates);

} // namespace visualis
