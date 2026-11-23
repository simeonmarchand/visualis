#pragma once

#include "service/service.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace visualis {

// One line of the diff shown before a re-import is applied (#56).
struct ReimportChange {
    enum class Kind {
        Added,          // new in Planning Center
        Removed,        // gone from Planning Center: flagged, not deleted
        Restored,       // flagged before, back in the plan now
        Moved,          // Planning Center reordered it
        Changed,        // Planning Center changed `field`
        KeptEdit,       // Planning Center has something else for `field`, but the operator's is kept
        KeptPosition,   // Planning Center moved it, but so did the operator: theirs is kept
        Relink,         // Planning Center put another song here: the item needs a song again
        ChangedButDeleted,   // the operator deleted it, and Planning Center changed it since: not
                             // added back unless they ask (ReimportOptions::addBack)
        Unreadable,     // Planning Center's record couldn't be read: the item is left as it was
        Linked,         // a song placeholder the library is sure of, linked by applying (made by
                        // PlanImporter from ReimportResult::linkable, not by the merge)
    };
    Kind kind;
    QUuid itemId;       // the item in the merged service; null for a deleted item or the whole plan
    QString title;      // the item's title after the merge
    QString field;      // Changed and KeptEdit: "title", "sequence", "service position", "reference", "kind"
    QString detail;     // "Amazing Grace → Amazing Grace (Live)"
    QString pcoItemId;  // ChangedButDeleted, and Added for an item added back: its id in `addBack`

    bool operator==(const ReimportChange &) const = default;
};

// "1 added, 1 removed, 2 moved, 1 changed. Kept 2 of your edits." for these lines, or what to
// say when there are none.
QString reimportSummary(const QList<ReimportChange> &changes, bool needsSave);

struct ReimportOptions {
    // Planning Center ids of items the operator deleted and wants back (ChangedButDeleted).
    QSet<QString> addBack;
    // From PlanImport: items Planning Center listed but that couldn't be read whole, and
    // whether a record couldn't be read at all, so nothing missing can be taken as removed.
    QSet<QString> unreadable;
    bool unreadableWithoutId = false;
};

struct ReimportResult {
    Service service;                  // the merged service, not yet saved
    QList<ReimportChange> changes;    // in the merged service's order
    QStringList movedByOperator;      // Planning Center item ids kept where the operator put them
    // Items whose song placeholder came from Planning Center in this merge, so linking them
    // to the library undoes nothing of the operator's: new items, Relink, and items they left
    // as Planning Center had them. Nothing else may be linked automatically.
    QSet<QUuid> linkable;
    // Whether saving changes anything, shown or not: a refreshed snapshot, a forgotten
    // deleted item. lastImportedAt alone doesn't count.
    bool needsSave = false;

    bool upToDate() const { return changes.isEmpty() && !needsSave; }
    QString summary() const { return reimportSummary(changes, needsSave); }
};

// Merges a fresh import of a plan (serviceFromPlan) into the operator's copy of it (plan
// 5.3, #56). Pure: nothing is saved, so the diff can be shown first.
//
// Each item's importSnapshot is what Planning Center said last time. A field Planning
// Center changed since then takes its new value only if the operator left it alone;
// otherwise the operator's edit is kept and listed. What the operator added is never
// touched: slide splits, backgrounds and other overrides, what a filled placeholder shows,
// items they added. The one exception is a placeholder the operator linked to a library
// song when Planning Center put a different song in its place: it's a placeholder again
// (Relink), with its overrides kept. Any other choice of theirs is kept, and listed if
// Planning Center now has something else.
//
// What can't be compared is left alone: an item with no snapshot (imported before
// snapshots, or from a file) keeps its values, link and place, and differences are listed;
// an item Planning Center's answer had but couldn't be read is left exactly as it was; and
// if a record couldn't be read at all, nothing is taken as removed.
//
// Order: items the operator didn't move follow Planning Center's new order; an item the
// operator moved or added, that Planning Center removed, or that can't be compared, stays
// right after the item that was before it in the operator's list. So does an item whose
// place last time is unclear (an old snapshot with no `order`, tied with another). When a
// reorder could be either side's (a b became b a), the operator's is assumed, so it stands.
//
// The result depends only on the inputs (new items get ids made from the service's and
// Planning Center's), so a proposal can be computed again and compared.
//
// When the operator has several items with one Planning Center id (a copy), the one with
// the lowest item id is the Planning Center item. That's arbitrary, but it doesn't change
// when they're reordered; the others are the operator's own.
//
// An imported item the operator deleted (Service::removedPcoItems) stays deleted. If
// Planning Center changed it since, the diff says so (ChangedButDeleted), and it comes back
// only if its Planning Center id is in `addBack`. Once Planning Center removes it too, its
// record goes.
//
// Every snapshot that could be read becomes the fresh one, so merging the same plan again
// changes nothing.
ReimportResult mergeReimport(const Service &local, const Service &fresh, const ReimportOptions &options = {});

} // namespace visualis
