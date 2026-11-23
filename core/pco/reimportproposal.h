#pragma once

#include "pco/planimport.h"
#include "pco/reimport.h"
#include "pco/songlink.h"
#include "song/song.h"

#include <QList>
#include <QSet>
#include <QString>

namespace visualis {

class SongStore;

// What applying a re-import would do (#56): the merge, the library songs it links, and the
// lines the operator approves. Worked out again under the library's write lock before it's
// applied, and applied only if it's the same.
struct ReimportProposal {
    ReimportResult merge;
    SongLinkReport links;
    QList<Song> linked;               // library songs whose Planning Center link to save with the service
    QList<ReimportChange> changes;    // the merge's lines and a Linked line per link, in service order

    bool needsSave() const { return merge.needsSave || !links.items.isEmpty(); }
    QString summary() const { return reimportSummary(changes, needsSave()); }
    // The same service, links and lines: what the operator saw is what would be saved.
    bool operator==(const ReimportProposal &other) const
    {
        return merge.service == other.merge.service && linked == other.linked && changes == other.changes;
    }
};

// Merges `fresh` into `local`, adding back the deleted items in `addBack`, and links to the
// library only what came from Planning Center in this merge (ReimportResult::linkable).
// Reads `songs`; writes nothing.
ReimportProposal proposeReimport(const Service &local, const PlanImport &fresh, const QSet<QString> &addBack,
                                 SongStore &songs);

} // namespace visualis
