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
        KeptEdit,       // Planning Center changed `field`, but so did the operator: theirs is kept
        KeptPosition,   // Planning Center moved it, but so did the operator: theirs is kept
        Relink,         // Planning Center put another song here: the item needs a song again
        ChangedButDeleted,   // the operator deleted it, and Planning Center changed it since: not
                             // added back unless they ask (mergeReimport's `addBack`)
    };
    Kind kind;
    QUuid itemId;       // the item in the merged service
    QString title;      // the item's title after the merge
    QString field;      // Changed and KeptEdit: "title", "sequence", "service position", "reference", "kind"
    QString detail;     // "Amazing Grace → Amazing Grace (Live)"
    QString pcoItemId;  // ChangedButDeleted: the item to pass to `addBack`; the item isn't in the service

    bool operator==(const ReimportChange &) const = default;
};

struct ReimportResult {
    Service service;                  // the merged service, not yet saved
    QList<ReimportChange> changes;    // in the merged service's order
    QStringList movedByOperator;      // Planning Center item ids kept where the operator put them

    bool upToDate() const { return changes.isEmpty(); }
    // "1 added, 1 removed, 2 moved, 1 changed. Kept 2 of your edits." or "Up to date with
    // Planning Center."
    QString summary() const;
};

// Merges a fresh import of a plan (serviceFromPlan) into the operator's copy of it (plan
// 5.3, #56). Pure: nothing is saved, so the diff can be shown first.
//
// Each item's importSnapshot is what Planning Center said last time. A field Planning
// Center changed since then takes its new value only if the operator left it alone;
// otherwise the operator's edit is kept and listed. What the operator added is never
// touched: slide splits, backgrounds and other overrides, a filled placeholder's song,
// items they added. The one exception is a placeholder filled with a song when Planning
// Center put a different song in its place: it's a placeholder again (Relink), with its
// overrides kept.
//
// Order: items the operator didn't move follow Planning Center's new order; an item the
// operator moved, added, or that Planning Center removed stays right after the item that
// was before it in the operator's list.
//
// An imported item the operator deleted (Service::removedPcoItems) stays deleted. If
// Planning Center changed it since, the diff says so (ChangedButDeleted), and it comes back
// only if its Planning Center id is in `addBack`. Once Planning Center removes it too, its
// record goes.
//
// Every snapshot becomes the fresh one, so merging the same plan again changes nothing.
ReimportResult mergeReimport(const Service &local, const Service &fresh, const QSet<QString> &addBack = {});

} // namespace visualis
