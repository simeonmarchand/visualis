#include "live/liveitem.h"

#include "song/songlimits.h"

namespace visualis {

namespace {

// The groups a custom sequence names, by label ignoring case. Labels that name no group
// are left out.
QList<QUuid> sequenceByLabel(const Song &song, const QStringList &labels)
{
    QList<QUuid> out;
    for (const QString &label : labels) {
        for (const Group &g : song.groups) {
            if (g.label.compare(label.trimmed(), Qt::CaseInsensitive) == 0) {
                out << g.id;
                break;
            }
        }
    }
    return out;
}

} // namespace

LiveItem liveItem(const ServiceItem &item, const std::optional<Song> &song)
{
    LiveItem live{item.id, {}, {}, {}};
    switch (item.kind) {
    case ServiceItem::Kind::Song:
        if (!song)
            return live;
        // Before matching the custom sequence, which is labels x groups (#83).
        live.error = songLimitError(*song);
        if (live.error.isEmpty() && item.customSequence.size() > songlimits::kSteps)
            live.error = limitError(QStringLiteral("steps in the sequence"), item.customSequence.size(), songlimits::kSteps);
        if (!live.error.isEmpty())
            return live;
        live.song = *song;
        live.arrangement = item.arrangement;
        if (const QList<QUuid> custom = sequenceByLabel(*song, item.customSequence); !custom.isEmpty()) {
            // The item's id names it: unique, and the same each time the service loads.
            live.song.arrangements << Arrangement{item.id, QStringLiteral("This service"), custom, {}};
            live.arrangement = item.id;
        }
        break;
    case ServiceItem::Kind::Text:
        live.song.title = item.title;
        live.song.groups = {Group{item.id, {}, {}, item.body.isEmpty() ? QStringList{} : QStringList{item.body}}};
        break;
    default:
        return live;
    }
    live.slides = songSlides(live.song, live.arrangement, &live.error);
    return live;
}

} // namespace visualis
