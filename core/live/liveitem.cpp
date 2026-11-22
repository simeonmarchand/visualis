#include "live/liveitem.h"

#include "song/labels.h"
#include "song/songlimits.h"

#include <algorithm>

namespace visualis {

namespace {

// This service's order for a song, from its Planning Center labels (#55). Labels match
// groups forgivingly (matchSequence: "Pre Chorus 1" finds "PreChorus"). A lyric-less label
// the song has no group for (Intro, Turnaround, Ending) gets a blank group of its own, added
// to `song`, so the operator still lands on it; other unmatched labels are left out. Empty
// when no label names one of the song's own groups.
QList<QUuid> customSteps(Song &song, const QUuid &itemId, const QStringList &labels)
{
    const SequenceMatch match = matchSequence(labels, song.groups);
    QList<QUuid> steps;
    bool any = false;
    QList<Group> blanks;
    for (int i = 0; i < labels.size(); ++i) {
        if (!match.steps[i].isNull()) {
            steps << match.steps[i];
            any = true;
        } else if (isLyricless(labels[i])) {
            // Named from the item and label, so it's the same id each time the service loads.
            const QUuid id = QUuid::createUuidV5(itemId, labels[i].trimmed().toLower());
            if (std::none_of(blanks.cbegin(), blanks.cend(), [&](const Group &g) { return g.id == id; }))
                blanks << Group{id, labels[i].trimmed(), {}, {QString()}};
            steps << id;
        }
    }
    if (!any)
        return {};
    song.groups << blanks;
    return steps;
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
        if (const QList<QUuid> custom = customSteps(live.song, item.id, item.customSequence); !custom.isEmpty()) {
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
