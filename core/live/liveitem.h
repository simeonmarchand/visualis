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

// The live background (GLOSSARY.md "Background layer"): a media asset, whether it's
// cleared, and the transition it arrived with, which clearing and showing it use too (#98).
struct BackgroundLayer {
    Q_GADGET
    Q_PROPERTY(QString assetId READ assetId)   // without braces; empty for none
    Q_PROPERTY(bool cleared MEMBER cleared)
    Q_PROPERTY(visualis::Transition transition MEMBER transition)

public:
    QUuid asset;   // null: no background
    bool cleared = false;
    Transition transition = Transitions{}.background;

    bool operator==(const BackgroundLayer &) const = default;

    QString assetId() const { return asset.isNull() ? QString() : asset.toString(QUuid::WithoutBraces); }
};

// A slide as it will show (GLOSSARY.md "LiveSlide"): its place in the song plus its layers.
struct LiveSlide {
    SongSlide slide;
    Content content;

    bool operator==(const LiveSlide &) const = default;
};

struct LiveItem {
    // The service item it came from, overrides and all; none for a song taken live from the library.
    std::optional<ServiceItem> source;
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

    QUuid id() const { return source ? source->id : QUuid(); }   // the service item's
    QUuid templateId() const { return tmpl.id; }
};

// For QML's slide grids and previews: each slide's text, group label and color, and content.
QVariantList slideMaps(const QList<LiveSlide> &slides);

// The template an item with template `wanted` (null: the built-in) renders with: that one, or
// the built-in when it isn't in `templates` or has no "main" region to hold the text, with
// `problem` saying why for the operator.
struct TemplateChoice {
    Template tmpl;
    QString problem;
};
TemplateChoice chooseTemplate(const TemplateSet &templates, const QUuid &wanted);

// `song` is the item's song from the library, for song items; without it a song item has
// no slides. `templates` must include the built-ins (templateSet()).
LiveItem liveItem(const ServiceItem &item, const std::optional<Song> &song, const TemplateSet &templates);
// A song taken live from the library, not from a service.
LiveItem liveSong(const Song &song, const QUuid &arrangement, const TemplateSet &templates);

} // namespace visualis
