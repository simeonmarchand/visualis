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

// What merging one of the operator's items gives: the item, its lines, and whether its song
// placeholder is Planning Center's now (so linking it to the library undoes nothing of theirs).
struct ItemMerge {
    ServiceItem item;
    QList<Change> changes;
    bool linkable = false;
};

ItemMerge unchanged(const ServiceItem &local) { return {local, {}, false}; }

// Merges one item Planning Center still has, with a known base. `local` is the operator's,
// `fresh` the new import.
ItemMerge mergeItem(const ServiceItem &local, const ServiceItem &fresh)
{
    const ServiceItem base = importedItem(local.pcoItemId, local.importSnapshot);
    ItemMerge m{local, {}, false};
    ServiceItem &out = m.item;
    out.importSnapshot = fresh.importSnapshot;
    out.pcoRemoved = false;
    const auto add = [&](Change::Kind kind, const QString &field, const QString &detail) {
        m.changes << Change{kind, out.id, {}, field, detail, {}};
    };
    if (local.pcoRemoved)
        add(Change::Kind::Restored, {}, {});

    // What the item shows. The operator's choice stands, with one exception: they linked a
    // library song, and Planning Center has put a different song here, so theirs is wrong.
    const QString songBefore = songId(local.importSnapshot);
    const QString songNow = songId(fresh.importSnapshot);
    const bool anotherSong = !songBefore.isEmpty() && !songNow.isEmpty() && songBefore != songNow;
    if (sameLink(local, base)) {
        m.linkable = true;
        if (!sameLink(base, fresh)) {
            takeLink(out, fresh);
            add(Change::Kind::Changed, QStringLiteral("kind"), arrow(kindName(local.kind), kindName(fresh.kind)));
        }
    } else if (anotherSong && local.kind == ServiceItem::Kind::Song && !local.ref.isNull()) {
        m.linkable = true;
        takeLink(out, fresh);
        add(Change::Kind::Relink, {}, {});
    } else if ((anotherSong || !sameLink(base, fresh)) && !sameLink(local, fresh)) {
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
    for (Change &c : m.changes)
        c.title = out.title;
    return m;
}

// An item with no snapshot: what Planning Center said before is unknown, so every value is
// the operator's. Differences are listed, and the fresh snapshot becomes the base from now on.
ItemMerge mergeUnknown(const ServiceItem &local, const ServiceItem &fresh)
{
    ItemMerge m{local, {}, false};
    m.item.importSnapshot = fresh.importSnapshot;
    m.item.pcoRemoved = false;
    const auto add = [&](Change::Kind kind, const QString &field, const QString &detail) {
        m.changes << Change{kind, local.id, local.title, field, detail, {}};
    };
    if (local.pcoRemoved)
        add(Change::Kind::Restored, {}, {});
    if (!sameLink(local, fresh))
        add(Change::Kind::KeptEdit, QStringLiteral("kind"),
            QStringLiteral("kept %1; Planning Center has %2").arg(kindName(local.kind), kindName(fresh.kind)));
    for (const Field &f : kFields) {
        if (!f.same(local, fresh))
            add(Change::Kind::KeptEdit, QString::fromLatin1(f.name),
                QStringLiteral("kept “%1”; Planning Center has “%2”").arg(f.show(local), f.show(fresh)));
    }
    return m;
}

bool byPcoId(const RemovedPcoItem &a, const RemovedPcoItem &b) { return a.pcoItemId < b.pcoItemId; }

// The operator's items, read against the fresh import: which is each Planning Center id's
// item, and what can be compared.
struct Reading {
    const Service &mine;
    const Service &fresh;
    const ReimportOptions &options;
    QHash<QString, qsizetype> inFresh;   // pco id -> its first item in the fresh import
    QHash<QString, qsizetype> owner;     // pco id -> the operator's item that is Planning Center's

    Reading(const Service &m, const Service &f, const ReimportOptions &o)
        : mine(m), fresh(f), options(o)
    {
        for (qsizetype i = 0; i < fresh.items.size(); ++i) {
            if (!fresh.items[i].pcoItemId.isEmpty() && !inFresh.contains(fresh.items[i].pcoItemId))
                inFresh.insert(fresh.items[i].pcoItemId, i);
        }
        // Of several with one id (a copy), the lowest item id: arbitrary, but it doesn't change
        // when they're reordered. The rest are the operator's own.
        for (qsizetype i = 0; i < mine.items.size(); ++i) {
            const QString &id = mine.items[i].pcoItemId;
            if (id.isEmpty())
                continue;
            if (const auto at = owner.constFind(id); at == owner.constEnd() || mine.items[i].id < mine.items[*at].id)
                owner.insert(id, i);
        }
    }

    bool isOwner(qsizetype i) const
    {
        const QString &id = mine.items[i].pcoItemId;
        return !id.isEmpty() && owner.value(id) == i;
    }
    bool unreadable(const QString &id) const { return options.unreadable.contains(id); }
    const ServiceItem &freshItem(const QString &id) const { return fresh.items[inFresh[id]]; }
    // Still in the plan, read whole, with a known base: merged field by field.
    bool comparable(qsizetype i) const
    {
        const QString &id = mine.items[i].pcoItemId;
        return isOwner(i) && inFresh.contains(id) && !unreadable(id) && knownBase(mine.items[i]);
    }
};

// Who reordered what since the last import, among the items that can be compared.
struct Moves {
    QSet<QString> unplaced;     // where they were last time can't be told: the operator's place stands
    QSet<QString> byPco;
    QSet<QString> byOperator;
};

Moves whoMoved(const Reading &r)
{
    Moves m;
    QStringList localOrder;
    QHash<std::pair<int, int>, int> atPlace;
    for (qsizetype i = 0; i < r.mine.items.size(); ++i) {
        if (r.comparable(i)) {
            localOrder << r.mine.items[i].pcoItemId;
            ++atPlace[placeInPlan(r.mine.items[i])];
        }
    }
    const auto place = [&](const QString &id) { return placeInPlan(r.mine.items[r.owner[id]]); };
    // Two with the same place (old snapshots with no `order`) can't be told apart.
    for (const QString &id : std::as_const(localOrder)) {
        if (atPlace.value(place(id)) > 1)
            m.unplaced << id;
    }
    localOrder.removeIf([&](const QString &id) { return m.unplaced.contains(id); });
    QStringList baseOrder = localOrder;
    std::sort(baseOrder.begin(), baseOrder.end(), [&](const QString &a, const QString &b) { return place(a) < place(b); });
    QStringList freshOrder;
    for (const ServiceItem &f : r.fresh.items) {
        if (localOrder.contains(f.pcoItemId) && !freshOrder.contains(f.pcoItemId))
            freshOrder << f.pcoItemId;
    }
    m.byPco = movedBetween(freshOrder, baseOrder);
    // A reorder that could be either side's is taken as the operator's, so it stands.
    m.byOperator = movedBetween(localOrder, baseOrder, m.byPco);
    return m;
}

} // namespace

QString reimportSummary(const QList<ReimportChange> &changes, bool needsSave)
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
    const Reading reading(mine, fresh, options);
    const Moves moves = whoMoved(reading);

    // 1. Each of the operator's items, once: what it becomes, and whether it follows Planning
    //    Center's order (placed) or keeps the operator's place.
    QList<ItemMerge> merged;
    QList<bool> placed;
    for (qsizetype i = 0; i < mine.items.size(); ++i) {
        const ServiceItem &it = mine.items[i];
        const QString &id = it.pcoItemId;
        if (reading.comparable(i)) {
            ItemMerge m = mergeItem(it, reading.freshItem(id));
            const bool keepsPlace = moves.unplaced.contains(id) || moves.byOperator.contains(id);
            if (moves.byOperator.contains(id))
                r.movedByOperator << id;
            if (moves.byPco.contains(id) && !moves.unplaced.contains(id))
                m.changes.prepend(Change{keepsPlace ? Change::Kind::KeptPosition : Change::Kind::Moved, m.item.id,
                                         m.item.title, {}, {}, {}});
            merged << m;
            placed << !keepsPlace;
            continue;
        }
        ItemMerge m = unchanged(it);
        if (!reading.isOwner(i)) {
            // The operator's own: added by them, or a copy.
        } else if (reading.unreadable(id)) {
            m.changes << Change{Change::Kind::Unreadable, it.id, it.title, {},
                                QStringLiteral("couldn't be read from Planning Center; left as it was"), {}};
        } else if (reading.inFresh.contains(id)) {
            m = mergeUnknown(it, reading.freshItem(id));
        } else if (!options.unreadableWithoutId && !it.pcoRemoved) {
            m.item.pcoRemoved = true;   // gone from Planning Center: flagged, never deleted
            m.changes << Change{Change::Kind::Removed, it.id, it.title, {}, {}, {}};
        }
        merged << m;
        placed << false;
    }
    for (const ItemMerge &m : std::as_const(merged)) {
        if (m.linkable)
            r.linkable << m.item.id;
    }

    // 2. Planning Center's order, with its new items, the operator's placed items, and what
    //    they deleted kept out.
    QHash<QString, RemovedPcoItem> deleted;   // pco id -> record, of items the operator deleted
    for (const RemovedPcoItem &d : std::as_const(mine.removedPcoItems)) {
        if (!reading.owner.contains(d.pcoItemId))
            deleted.insert(d.pcoItemId, d);
    }
    QList<RemovedPcoItem> stillDeleted;
    QList<Change> planChanges;   // not about an item in the service
    QList<ItemMerge> order;
    for (qsizetype fi = 0; fi < fresh.items.size(); ++fi) {
        const ServiceItem &f = fresh.items[fi];
        const QString &id = f.pcoItemId;
        if (reading.inFresh.value(id, -1) != fi)
            continue;   // no id, or a second item with one: Planning Center doesn't make those
        if (reading.owner.contains(id)) {
            if (const qsizetype i = reading.owner[id]; placed[i])
                order << merged[i];
            continue;
        }
        if (deleted.contains(id) && !options.addBack.contains(id)) {
            if (reading.unreadable(id)) {
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
        const bool back = options.addBack.contains(id) && deleted.contains(id);
        order << ItemMerge{added,
                           {Change{Change::Kind::Added, added.id, added.title, {}, back ? QStringLiteral("added back") : QString(),
                                   back ? id : QString()}},
                           true};
    }
    // A deleted item missing from the answer is forgotten, unless it may only have been unreadable.
    for (const RemovedPcoItem &d : std::as_const(mine.removedPcoItems)) {
        if (deleted.contains(d.pcoItemId) && !reading.inFresh.contains(d.pcoItemId)
            && (reading.unreadable(d.pcoItemId) || options.unreadableWithoutId))
            stillDeleted << d;
    }

    // 3. Everything that keeps the operator's place goes right after the item before it in
    //    their list. In list order, so a run of them stays together.
    for (qsizetype i = 0; i < mine.items.size(); ++i) {
        if (placed[i])
            continue;
        qsizetype at = 0;
        if (i > 0) {
            const QUuid before = mine.items[i - 1].id;
            at = std::find_if(order.cbegin(), order.cend(), [&](const ItemMerge &x) { return x.item.id == before; })
                 - order.cbegin() + 1;
        }
        order.insert(at, merged[i]);
    }
    if (options.unreadableWithoutId)
        planChanges << Change{Change::Kind::Unreadable, {}, {}, {},
                              QStringLiteral("part of the plan couldn't be read, so nothing was taken as removed"), {}};

    r.service = mine;
    r.service.date = fresh.date;
    r.service.pcoPlanId = fresh.pcoPlanId;
    r.service.pcoServiceTypeId = fresh.pcoServiceTypeId;
    r.service.pcoPlanUrl = fresh.pcoPlanUrl;
    r.service.lastImportedAt = fresh.lastImportedAt;
    r.service.items.clear();
    for (const ItemMerge &m : std::as_const(order)) {
        r.service.items << m.item;
        r.changes << m.changes;
    }
    r.changes << planChanges;
    std::sort(stillDeleted.begin(), stillDeleted.end(), byPcoId);
    r.service.removedPcoItems = stillDeleted;   // what Planning Center removed too is forgotten

    Service before = local;
    std::sort(before.removedPcoItems.begin(), before.removedPcoItems.end(), byPcoId);
    Service after = r.service;
    after.lastImportedAt = before.lastImportedAt;
    r.needsSave = after != before;
    return r;
}

} // namespace visualis
