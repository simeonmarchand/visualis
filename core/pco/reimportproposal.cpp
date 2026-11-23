#include "pco/reimportproposal.h"

#include "song/songstore.h"

#include <QHash>
#include <algorithm>

namespace visualis {

ReimportProposal proposeReimport(const Service &local, const PlanImport &fresh, const QSet<QString> &addBack,
                                 SongStore &songs)
{
    ReimportProposal p;
    p.merge = mergeReimport(local, fresh.service, ReimportOptions{addBack, fresh.unreadable, fresh.unreadableWithoutId});
    p.links = linkSongs(p.merge.service, songs, &p.linked, &p.merge.linkable);
    p.changes = p.merge.changes;
    QHash<QUuid, qsizetype> at;   // each item's place, for the lines' order
    for (qsizetype i = 0; i < p.merge.service.items.size(); ++i)
        at.insert(p.merge.service.items[i].id, i);
    for (const SongLinkReport::Linked &l : std::as_const(p.links.items)) {
        p.changes << ReimportChange{ReimportChange::Kind::Linked, l.itemId, p.merge.service.items[at[l.itemId]].title, {},
                                    l.byTitle ? QStringLiteral("linked to “%1”, matched by title").arg(l.songTitle)
                                              : QStringLiteral("linked to “%1”").arg(l.songTitle),
                                    {}};
    }
    // In the service's order; lines about no one item last.
    const qsizetype last = p.merge.service.items.size();
    std::stable_sort(p.changes.begin(), p.changes.end(), [&](const ReimportChange &a, const ReimportChange &b) {
        return at.value(a.itemId, last) < at.value(b.itemId, last);
    });
    return p;
}

} // namespace visualis
