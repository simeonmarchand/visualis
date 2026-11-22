#include "pco/reimport.h"

#include "pco/planimport.h"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <climits>
#include <utility>

namespace visualis {

namespace {

using Change = ReimportChange;

// The ids in `a` that aren't in a longest subsequence common to `a` and `b`: what moved.
// Both hold the same ids. Of several longest, the one that leaves out most of `rather`.
QSet<QString> movedBetween(const QStringList &a, const QStringList &b, const QSet<QString> &rather = {})
{
    const qsizetype n = a.size();
    const qsizetype m = b.size();
    // Each id kept counts n + 1, plus 1 if it's not one of `rather`: longest first, then fewest of `rather`.
    const auto weight = [&](const QString &id) { return int(n) + 1 + (rather.contains(id) ? 0 : 1); };
    QList<QList<int>> best(n + 1, QList<int>(m + 1, 0));
    for (qsizetype i = n - 1; i >= 0; --i) {
        for (qsizetype j = m - 1; j >= 0; --j) {
            best[i][j] = std::max(best[i + 1][j], best[i][j + 1]);
            if (a[i] == b[j])
                best[i][j] = std::max(best[i][j], best[i + 1][j + 1] + weight(a[i]));
        }
    }
    QSet<QString> kept;
    for (qsizetype i = 0, j = 0; i < n && j < m;) {
        if (a[i] == b[j] && best[i][j] == best[i + 1][j + 1] + weight(a[i])) {
            kept << a[i];
            ++i;
            ++j;
        } else if (best[i + 1][j] == best[i][j]) {
            ++i;
        } else {
            ++j;
        }
    }
    QSet<QString> moved(a.cbegin(), a.cend());
    moved.subtract(kept);
    return moved;
}

QString showPosition(const QString &p) { return p.isEmpty() ? QStringLiteral("during") : p; }

QString arrow(const QString &from, const QString &to) { return QStringLiteral("%1 → %2").arg(from, to); }

// One field Planning Center fills: its name for the diff, how to show it, and where it is.
struct Field {
    const char *name;
    QString (*show)(const ServiceItem &);
    bool (*same)(const ServiceItem &, const ServiceItem &);
    void (*take)(ServiceItem &, const ServiceItem &);
};

const Field kFields[] = {
    {"title", [](const ServiceItem &i) { return i.title; },
     [](const ServiceItem &a, const ServiceItem &b) { return a.title == b.title; },
     [](ServiceItem &to, const ServiceItem &from) { to.title = from.title; }},
    {"sequence", [](const ServiceItem &i) { return i.customSequence.join(QStringLiteral(", ")); },
     [](const ServiceItem &a, const ServiceItem &b) { return a.customSequence == b.customSequence; },
     [](ServiceItem &to, const ServiceItem &from) { to.customSequence = from.customSequence; }},
    {"service position", [](const ServiceItem &i) { return showPosition(i.servicePosition); },
     [](const ServiceItem &a, const ServiceItem &b) { return a.servicePosition == b.servicePosition; },
     [](ServiceItem &to, const ServiceItem &from) { to.servicePosition = from.servicePosition; }},
    {"reference", [](const ServiceItem &i) { return i.body; },
     [](const ServiceItem &a, const ServiceItem &b) { return a.body == b.body; },
     [](ServiceItem &to, const ServiceItem &from) { to.body = from.body; }},
};

// What the item shows: a filled placeholder is a song, a blank slide, ...
bool sameLink(const ServiceItem &a, const ServiceItem &b)
{
    return a.kind == b.kind && a.ref == b.ref && a.arrangement == b.arrangement;
}

void takeLink(ServiceItem &to, const ServiceItem &from)
{
    to.kind = from.kind;
    to.ref = from.ref;
    to.arrangement = from.arrangement;
}

QString songId(const QJsonObject &snapshot)
{
    return snapshot.value(QStringLiteral("song")).toObject().value(QStringLiteral("id")).toString();
}

// Whether a snapshot says what Planning Center had: items imported before snapshots, or
// from a file, have none, and there's nothing to tell the operator's edits from.
bool knownBase(const ServiceItem &item) { return item.importSnapshot.contains(QStringLiteral("itemType")); }

// Whether Planning Center changed an item, apart from where it sits in the plan (other items
// coming and going renumber it).
bool changedApartFromPosition(QJsonObject before, QJsonObject now)
{
    for (const char *where : {"position", "order"}) {
        before.remove(QLatin1String(where));
        now.remove(QLatin1String(where));
    }
    return before != now;
}

// Where an item was in the plan when it was last imported: Planning Center's number, then
// its place among items with the same number. Snapshots from before `order` have only the number.
std::pair<int, int> placeInPlan(const ServiceItem &item)
{
    return {item.importSnapshot.value(QStringLiteral("position")).toInt(),
            item.importSnapshot.value(QStringLiteral("order")).toInt(INT_MAX)};
}

// Merges one item Planning Center still has, with a known base. `local` is the operator's,
// `fresh` the new import.
ServiceItem mergeItem(const ServiceItem &local, const ServiceItem &fresh, QList<Change> &changes, bool *planLink)
{
    const ServiceItem base = importedItem(local.pcoItemId, local.importSnapshot);
    ServiceItem out = local;
    out.importSnapshot = fresh.importSnapshot;
    out.pcoRemoved = false;
    QList<Change> mine;
    const auto add = [&](Change::Kind kind, const QString &field, const QString &detail) {
        mine << Change{kind, out.id, {}, field, detail, {}};
    };
    if (local.pcoRemoved)
        add(Change::Kind::Restored, {}, {});

    // What the item shows. The operator's choice stands, with one exception: they linked a
    // library song, and Planning Center has put a different song here, so theirs is wrong.
    const QString songBefore = songId(local.importSnapshot);
    const QString songNow = songId(fresh.importSnapshot);
    const bool anotherSong = !songBefore.isEmpty() && !songNow.isEmpty() && songBefore != songNow;
    const bool pcoChangedIt = !sameLink(base, fresh) || anotherSong;
    *planLink = false;
    if (sameLink(local, base)) {
        *planLink = true;
        if (!sameLink(base, fresh)) {
            takeLink(out, fresh);
            add(Change::Kind::Changed, QStringLiteral("kind"), arrow(kindName(local.kind), kindName(fresh.kind)));
        }
    } else if (anotherSong && local.kind == ServiceItem::Kind::Song && !local.ref.isNull()) {
        *planLink = true;
        takeLink(out, fresh);
        add(Change::Kind::Relink, {}, {});
    } else if (pcoChangedIt && !sameLink(local, fresh)) {
        add(Change::Kind::KeptEdit, QStringLiteral("kind"),
            anotherSong ? QStringLiteral("kept your %1; Planning Center has another song here").arg(kindName(local.kind))
                        : QStringLiteral("kept %1; Planning Center has %2").arg(kindName(local.kind), kindName(fresh.kind)));
    }

    for (const Field &f : kFields) {
        if (f.same(base, fresh))
            continue;   // Planning Center didn't change it
        if (f.same(local, base)) {
            f.take(out, fresh);
            add(Change::Kind::Changed, QString::fromLatin1(f.name), arrow(f.show(local), f.show(fresh)));
        } else if (!f.same(local, fresh)) {
            add(Change::Kind::KeptEdit, QString::fromLatin1(f.name),
                QStringLiteral("kept “%1”; Planning Center has “%2”").arg(f.show(local), f.show(fresh)));
        }
    }
    for (Change &c : mine)
        c.title = out.title;
    changes << mine;
    return out;
}

// An item with no snapshot: what Planning Center said before is unknown, so every value is
// the operator's. Differences are listed, and the fresh snapshot becomes the base from now on.
ServiceItem mergeUnknown(const ServiceItem &local, const ServiceItem &fresh, QList<Change> &changes)
{
    ServiceItem out = local;
    out.importSnapshot = fresh.importSnapshot;
    out.pcoRemoved = false;
    if (local.pcoRemoved)
        changes << Change{Change::Kind::Restored, out.id, out.title, {}, {}, {}};
    if (!sameLink(local, fresh))
        changes << Change{Change::Kind::KeptEdit, out.id, out.title, QStringLiteral("kind"),
                          QStringLiteral("kept %1; Planning Center has %2").arg(kindName(local.kind), kindName(fresh.kind)), {}};
    for (const Field &f : kFields) {
        if (!f.same(local, fresh))
            changes << Change{Change::Kind::KeptEdit, out.id, out.title, QString::fromLatin1(f.name),
                              QStringLiteral("kept “%1”; Planning Center has “%2”").arg(f.show(local), f.show(fresh)), {}};
    }
    return out;
}

} // namespace

QString ReimportResult::summary() const
{
    if (changes.isEmpty())
        return needsSave ? QStringLiteral("Nothing to review. Apply saves Planning Center's latest details.")
                         : QStringLiteral("Up to date with Planning Center.");
    QHash<Change::Kind, int> n;
    for (const Change &c : changes)
        ++n[c.kind];
    QStringList parts;
    const auto part = [&](Change::Kind k, const char *what) {
        if (n.value(k) > 0)
            parts << QStringLiteral("%1 %2").arg(n.value(k)).arg(QLatin1String(what));
    };
    part(Change::Kind::Added, "added");
    part(Change::Kind::Removed, "removed");
    part(Change::Kind::Restored, "back in the plan");
    part(Change::Kind::Moved, "moved");
    part(Change::Kind::Changed, "changed");
    part(Change::Kind::ChangedButDeleted, "you removed changed in Planning Center");
    part(Change::Kind::Relink, n.value(Change::Kind::Relink) == 1 ? "needs a song again" : "need a song again");
    part(Change::Kind::Unreadable, "couldn't be read and left as it was");
    part(Change::Kind::Linked, "linked to the library");
    QString s = parts.join(QStringLiteral(", "));
    if (!s.isEmpty())
        s += QLatin1Char('.');
    if (const int kept = n.value(Change::Kind::KeptEdit) + n.value(Change::Kind::KeptPosition); kept > 0) {
        if (!s.isEmpty())
            s += QLatin1Char(' ');
        s += kept == 1 ? QStringLiteral("Kept 1 of your edits.") : QStringLiteral("Kept %1 of your edits.").arg(kept);
    }
    return s;
}

ReimportResult mergeReimport(const Service &local, const Service &fresh, const ReimportOptions &options)
{
    ReimportResult r;
    Service mine = local;
    // Ids made from the inputs, so the same inputs give the same result.
    static const QUuid kUnsaved = QUuid::fromString(QStringLiteral("{5c3f7a8e-2b41-4f0e-9c1d-6a7e8b9f0a21}"));
    const QUuid space = local.id.isNull() ? kUnsaved : local.id;
    for (qsizetype i = 0; i < mine.items.size(); ++i) {
        if (mine.items[i].id.isNull())   // so an item can be found after the reorder
            mine.items[i].id = QUuid::createUuidV5(space, QStringLiteral("item %1").arg(i));
    }
    r.service = mine;
    r.service.date = fresh.date;
    r.service.pcoPlanId = fresh.pcoPlanId;
    r.service.pcoServiceTypeId = fresh.pcoServiceTypeId;
    r.service.pcoPlanUrl = fresh.pcoPlanUrl;
    r.service.lastImportedAt = fresh.lastImportedAt;
    const auto unreadable = [&](const QString &id) { return options.unreadable.contains(id); };

    QHash<QString, qsizetype> inFresh;   // pco id -> its first item in the fresh import
    for (qsizetype i = 0; i < fresh.items.size(); ++i) {
        if (!fresh.items[i].pcoItemId.isEmpty() && !inFresh.contains(fresh.items[i].pcoItemId))
            inFresh.insert(fresh.items[i].pcoItemId, i);
    }

    // Each Planning Center id's item: of several (a copy), the lowest item id, which doesn't
    // change when they're reordered. The rest are the operator's own.
    QHash<QString, qsizetype> owner;
    for (qsizetype i = 0; i < mine.items.size(); ++i) {
        const QString &id = mine.items[i].pcoItemId;
        if (id.isEmpty())
            continue;
        if (const auto at = owner.constFind(id); at == owner.constEnd() || mine.items[i].id < mine.items[*at].id)
            owner.insert(id, i);
    }
    // Owners merged field by field and placed by order: still in the plan, read whole, with
    // a known base. Other owners in the plan keep their place.
    const auto comparable = [&](const QString &id) {
        return owner.contains(id) && inFresh.contains(id) && !unreadable(id) && knownBase(mine.items[owner[id]]);
    };

    // Who moved what, against the order Planning Center had last time.
    QStringList localOrder;
    for (qsizetype i = 0; i < mine.items.size(); ++i) {
        const QString &id = mine.items[i].pcoItemId;
        if (comparable(id) && owner[id] == i)
            localOrder << id;
    }
    // Where each was last time. Two with the same place (old snapshots with no `order`) can't
    // be told apart, so nobody's order of them is known: they keep the operator's place.
    QHash<std::pair<int, int>, int> atPlace;
    for (const QString &id : std::as_const(localOrder))
        ++atPlace[placeInPlan(mine.items[owner[id]])];
    QSet<QString> unplaced;
    for (const QString &id : std::as_const(localOrder)) {
        if (atPlace.value(placeInPlan(mine.items[owner[id]])) > 1)
            unplaced << id;
    }
    localOrder.removeIf([&](const QString &id) { return unplaced.contains(id); });
    const auto ordered = [&](const QString &id) { return comparable(id) && !unplaced.contains(id); };
    QStringList baseOrder = localOrder;
    std::sort(baseOrder.begin(), baseOrder.end(), [&](const QString &a, const QString &b) {
        return placeInPlan(mine.items[owner[a]]) < placeInPlan(mine.items[owner[b]]);
    });
    QStringList freshOrder;
    for (qsizetype i = 0; i < fresh.items.size(); ++i) {
        const QString &id = fresh.items[i].pcoItemId;
        if (ordered(id) && inFresh[id] == i)
            freshOrder << id;
    }
    const QSet<QString> movedInPco = movedBetween(freshOrder, baseOrder);
    // A reorder that could be either side's is taken as the operator's, so it stands.
    const QSet<QString> movedLocally = movedBetween(localOrder, baseOrder, movedInPco);

    QHash<QString, RemovedPcoItem> deleted;   // pco id -> record, of items the operator deleted
    for (const RemovedPcoItem &d : std::as_const(mine.removedPcoItems)) {
        if (!owner.contains(d.pcoItemId))
            deleted.insert(d.pcoItemId, d);
    }
    QList<RemovedPcoItem> stillDeleted;
    QList<Change> planChanges;   // not about an item in the service

    // The skeleton: Planning Center's new order, without what keeps the operator's place.
    QList<ServiceItem> result;
    QHash<QUuid, QList<Change>> changesFor;
    for (qsizetype fi = 0; fi < fresh.items.size(); ++fi) {
        const ServiceItem &f = fresh.items[fi];
        const QString &id = f.pcoItemId;
        if (inFresh.value(id, -1) != fi)
            continue;   // no id, or a second item with one: Planning Center doesn't make those
        if (owner.contains(id)) {
            if (!ordered(id) || movedLocally.contains(id))
                continue;   // placed with the operator's order below
            QList<Change> c;
            bool planLink = false;
            const ServiceItem merged = mergeItem(mine.items[owner[id]], f, c, &planLink);
            if (planLink)
                r.linkable << merged.id;
            if (movedInPco.contains(id))
                c.prepend(Change{Change::Kind::Moved, merged.id, merged.title, {}, {}, {}});
            changesFor[merged.id] << c;
            result << merged;
            continue;
        }
        if (deleted.contains(id) && !options.addBack.contains(id)) {
            if (unreadable(id)) {
                stillDeleted << deleted[id];   // can't tell what changed: as it was
                continue;
            }
            if (changedApartFromPosition(deleted[id].importSnapshot, f.importSnapshot))
                planChanges << Change{Change::Kind::ChangedButDeleted, {}, f.title, {},
                                      QStringLiteral("changed in Planning Center; you removed it"), id};
            stillDeleted << RemovedPcoItem{id, f.importSnapshot};
            continue;
        }
        ServiceItem added = f;
        added.id = QUuid::createUuidV5(space, QStringLiteral("pco %1").arg(id));
        r.linkable << added.id;
        // One the operator chose to add back says so, with its id, so they can change their mind.
        changesFor[added.id] << (options.addBack.contains(id) && deleted.contains(id)
                                     ? Change{Change::Kind::Added, added.id, added.title, {}, QStringLiteral("added back"), id}
                                     : Change{Change::Kind::Added, added.id, added.title, {}, {}, {}});
        result << added;
    }
    // A deleted item missing from the answer is forgotten, unless it may only have been unreadable.
    for (const RemovedPcoItem &d : std::as_const(mine.removedPcoItems)) {
        if (deleted.contains(d.pcoItemId) && !inFresh.contains(d.pcoItemId)
            && (unreadable(d.pcoItemId) || options.unreadableWithoutId))
            stillDeleted << d;
    }

    // Then everything that keeps its place in the operator's list, right after the item
    // before it there. In list order, so a run of them stays together.
    for (qsizetype i = 0; i < mine.items.size(); ++i) {
        const ServiceItem &it = mine.items[i];
        const QString &id = it.pcoItemId;
        const bool isOwner = !id.isEmpty() && owner.value(id) == i;
        if (isOwner && ordered(id) && !movedLocally.contains(id))
            continue;   // already in the skeleton
        ServiceItem kept = it;
        QList<Change> c;
        if (isOwner && comparable(id)) {
            bool planLink = false;
            kept = mergeItem(it, fresh.items[inFresh[id]], c, &planLink);
            if (planLink)
                r.linkable << kept.id;
            if (movedLocally.contains(id)) {
                r.movedByOperator << id;
                if (movedInPco.contains(id))
                    c.prepend(Change{Change::Kind::KeptPosition, kept.id, kept.title, {}, {}, {}});
            }
        } else if (isOwner && unreadable(id)) {
            c << Change{Change::Kind::Unreadable, kept.id, kept.title, {},
                        QStringLiteral("couldn't be read from Planning Center; left as it was"), {}};
        } else if (isOwner && inFresh.contains(id)) {
            kept = mergeUnknown(it, fresh.items[inFresh[id]], c);
        } else if (isOwner && !options.unreadableWithoutId && !it.pcoRemoved) {
            kept.pcoRemoved = true;
            c << Change{Change::Kind::Removed, kept.id, kept.title, {}, {}, {}};
        }
        changesFor[kept.id] << c;
        qsizetype at = 0;
        if (i > 0) {
            const QUuid before = mine.items[i - 1].id;
            const auto found = std::find_if(result.cbegin(), result.cend(),
                                            [&](const ServiceItem &x) { return x.id == before; });
            at = found - result.cbegin() + 1;
        }
        result.insert(at, kept);
    }
    if (options.unreadableWithoutId)
        planChanges << Change{Change::Kind::Unreadable, {}, {}, {},
                              QStringLiteral("part of the plan couldn't be read, so nothing was taken as removed"), {}};

    r.service.items = result;
    std::sort(stillDeleted.begin(), stillDeleted.end(),
              [](const RemovedPcoItem &a, const RemovedPcoItem &b) { return a.pcoItemId < b.pcoItemId; });
    r.service.removedPcoItems = stillDeleted;   // what Planning Center removed too is forgotten
    for (const ServiceItem &it : std::as_const(result))
        r.changes << changesFor.value(it.id);
    r.changes << planChanges;

    Service before = local;
    std::sort(before.removedPcoItems.begin(), before.removedPcoItems.end(),
              [](const RemovedPcoItem &a, const RemovedPcoItem &b) { return a.pcoItemId < b.pcoItemId; });
    Service after = r.service;
    after.lastImportedAt = before.lastImportedAt;
    r.needsSave = after != before;
    return r;
}

} // namespace visualis
