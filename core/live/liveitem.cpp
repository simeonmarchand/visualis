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

// The slides with their content layer, in template `wanted` (null: the built-in), with the
// item's overrides and then each slide's applied.
void render(LiveItem &live, const TemplateSet &templates, const QUuid &wanted, const QJsonObject &itemOverrides)
{
    const QUuid id = wanted.isNull() ? builtInLyricsTemplateId() : wanted;
    const auto found = templates.constFind(id);
    // The text goes in "main": a template without one can't hold it.
    const bool usable = found != templates.cend()
        && std::any_of(found->regions.cbegin(), found->regions.cend(),
                       [](const TextRegion &r) { return r.id == QStringLiteral("main"); });
    const Template t = usable ? *found : builtInLyricsTemplate();
    QStringList warnings;
    if (!usable) {
        live.templateFellBack = true;
        warnings << (found == templates.cend()
                         ? QStringLiteral("Its template isn't in the library, so it shows with the built-in \"%1\" template.").arg(t.name)
                         : QStringLiteral("Its template \"%1\" has no lyrics region, so it shows with the built-in \"%2\" template.")
                               .arg(found->name, t.name));
    }
    live.tmpl = t;
    for (const SongSlide &s : songSlides(live.song, live.arrangement, &live.error)) {
        Content content;
        content.transition = t.transitions.content;
        const Group *group = live.song.group(s.groupId);
        const QList<QJsonObject> layers{itemOverrides, group ? group->slideOverride(s.indexInGroup) : QJsonObject()};
        for (const TextRegion &region : t.regions) {
            content.regions.insert(region.id, {region.id == QStringLiteral("main") ? s.text : QString(),
                                               effectiveStyle(t, region.id, layers, &warnings).value_or(region.style)});
        }
        live.slides << LiveSlide{s, content};
    }
    warnings.removeDuplicates();   // an item's bad override reads the same on every slide
    live.warnings << warnings;
}

} // namespace

QVariantMap Content::regionMap() const
{
    QVariantMap out;
    for (auto it = regions.cbegin(); it != regions.cend(); ++it)
        out.insert(it.key(), QVariant::fromValue(it.value()));
    return out;
}

QVariantList slideMaps(const QList<LiveSlide> &slides)
{
    QVariantList list;
    for (const LiveSlide &l : slides) {
        const SongSlide &s = l.slide;
        list.append(QVariantMap{{"text", s.text}, {"groupLabel", s.groupLabel}, {"groupColor", s.groupColor},
                                {"content", QVariant::fromValue(l.content)}});
    }
    return list;
}

LiveItem liveSong(const Song &song, const QUuid &arrangement, const TemplateSet &templates)
{
    LiveItem live;
    live.error = songLimitError(song);
    if (!live.error.isEmpty()) {
        live.song.title = song.title;   // for the refusal
        return live;
    }
    live.song = song;
    live.arrangement = arrangement;
    render(live, templates, song.defaultTemplate, {});
    return live;
}

LiveItem liveItem(const ServiceItem &item, const std::optional<Song> &song, const TemplateSet &templates)
{
    LiveItem live;
    live.source = item;
    switch (item.kind) {
    case ServiceItem::Kind::Song:
        if (!song)
            return live;
        // Before matching the custom sequence, which is labels x groups (#83).
        live.error = songLimitError(*song);
        if (live.error.isEmpty() && item.customSequence.size() > songlimits::kSteps)
            live.error = limitError(QStringLiteral("steps in the sequence"), item.customSequence.size(), songlimits::kSteps);
        if (!live.error.isEmpty()) {
            live.song.title = song->title;   // for the refusal
            return live;
        }
        live.song = *song;
        // Recovery passes the song this made last time. Its order for the day is rebuilt from
        // the custom sequence; without one (a journal from before #93) the one it has stays.
        if (!item.customSequence.isEmpty())
            live.song.arrangements.removeIf([&](const Arrangement &a) { return a.id == item.id; });
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
    // The item's choice, else (for a song) the song's default.
    QUuid wanted = overrideTemplateId(item.overrides);
    if (wanted.isNull() && item.kind == ServiceItem::Kind::Song)
        wanted = live.song.defaultTemplate;
    render(live, templates, wanted, item.overrides);
    return live;
}

} // namespace visualis
