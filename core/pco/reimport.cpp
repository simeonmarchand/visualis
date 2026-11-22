#include "pco/reimport.h"

#include "pco/planimport.h"

#include <QHash>
#include <QSet>
#include <algorithm>

namespace visualis {

namespace {

using Change = ReimportChange;

// The ids in `a` that aren't in a longest subsequence common to `a` and `b`: what moved.
// Both hold the same ids.
QSet<QString> movedBetween(const QStringList &a, const QStringList &b)
{
    const qsizetype n = a.size();
    const qsizetype m = b.size();
    QList<QList<int>> len(n + 1, QList<int>(m + 1, 0));
    for (qsizetype i = n - 1; i >= 0; --i) {
        for (qsizetype j = m - 1; j >= 0; --j)
            len[i][j] = a[i] == b[j] ? len[i + 1][j + 1] + 1 : std::max(len[i + 1][j], len[i][j + 1]);
    }
    QSet<QString> kept;
    for (qsizetype i = 0, j = 0; i < n && j < m;) {
        if (a[i] == b[j]) {
            kept << a[i];
            ++i;
            ++j;
        } else if (len[i + 1][j] >= len[i][j + 1]) {
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

// Planning Center put something else in the item's place: another song, or another type.
bool otherThing(const QJsonObject &before, const QJsonObject &now)
{
    return before.value("itemType") != now.value("itemType")
           || before.value("song").toObject().value("id") != now.value("song").toObject().value("id");
}

// Merges one item Planning Center still has. `local` is the operator's, `fresh` the new import.
ServiceItem mergeItem(const ServiceItem &local, const ServiceItem &fresh, QList<Change> &changes)
{
    const ServiceItem base = importedItem(local.pcoItemId, local.importSnapshot);
    ServiceItem out = local;
    out.importSnapshot = fresh.importSnapshot;
    out.pcoRemoved = false;
    QList<Change> mine;
    const auto add = [&](Change::Kind kind, const QString &field, const QString &detail) {
        mine << Change{kind, out.id, {}, field, detail};
    };
    if (local.pcoRemoved)
        add(Change::Kind::Restored, {}, {});

    // What the item shows. The operator's choice stands unless Planning Center put another
    // song (or another type of item) in its place: then their song is the wrong one.
    if (sameLink(local, base)) {
        if (!sameLink(base, fresh)) {
            takeLink(out, fresh);
            add(Change::Kind::Changed, QStringLiteral("kind"), arrow(kindName(local.kind), kindName(fresh.kind)));
        }
    } else if (otherThing(local.importSnapshot, fresh.importSnapshot)) {
        takeLink(out, fresh);
        add(Change::Kind::Relink, {}, {});
    } else if (!sameLink(base, fresh) && !sameLink(local, fresh)) {
        add(Change::Kind::KeptEdit, QStringLiteral("kind"),
            QStringLiteral("kept %1; Planning Center has %2").arg(kindName(local.kind), kindName(fresh.kind)));
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

} // namespace

QString ReimportResult::summary() const
{
    if (changes.isEmpty())
        return QStringLiteral("Up to date with Planning Center.");
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
    part(Change::Kind::Relink, n.value(Change::Kind::Relink) == 1 ? "needs a song again" : "need a song again");
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

ReimportResult mergeReimport(const Service &local, const Service &fresh)
{
    ReimportResult r;
    Service mine = local;
    for (ServiceItem &it : mine.items) {
        if (it.id.isNull())
            it.id = QUuid::createUuid();   // so an item can be found after the reorder
    }
    r.service = mine;
    r.service.date = fresh.date;
    r.service.pcoPlanId = fresh.pcoPlanId;
    r.service.pcoServiceTypeId = fresh.pcoServiceTypeId;
    r.service.pcoPlanUrl = fresh.pcoPlanUrl;
    r.service.lastImportedAt = fresh.lastImportedAt;

    QHash<QString, qsizetype> inFresh;
    for (qsizetype i = 0; i < fresh.items.size(); ++i) {
        if (!fresh.items[i].pcoItemId.isEmpty() && !inFresh.contains(fresh.items[i].pcoItemId))
            inFresh.insert(fresh.items[i].pcoItemId, i);
    }

    // Which local items Planning Center still has: the first one with each id. A copy the
    // operator made is theirs.
    QHash<QString, qsizetype> matched;   // pco id -> local index
    for (qsizetype i = 0; i < mine.items.size(); ++i) {
        const QString &id = mine.items[i].pcoItemId;
        if (inFresh.contains(id) && !matched.contains(id))
            matched.insert(id, i);
    }

    // Who moved what, against the order Planning Center had last time.
    QStringList localOrder;
    for (qsizetype i = 0; i < mine.items.size(); ++i) {
        if (matched.value(mine.items[i].pcoItemId, -1) == i)
            localOrder << mine.items[i].pcoItemId;
    }
    QStringList baseOrder = localOrder;
    std::stable_sort(baseOrder.begin(), baseOrder.end(), [&](const QString &a, const QString &b) {
        return mine.items[matched[a]].importSnapshot.value("position").toInt()
               < mine.items[matched[b]].importSnapshot.value("position").toInt();
    });
    QStringList freshOrder;
    for (qsizetype i = 0; i < fresh.items.size(); ++i) {
        if (matched.contains(fresh.items[i].pcoItemId) && inFresh.value(fresh.items[i].pcoItemId) == i)
            freshOrder << fresh.items[i].pcoItemId;
    }
    const QSet<QString> movedLocally = movedBetween(localOrder, baseOrder);
    const QSet<QString> movedInPco = movedBetween(freshOrder, baseOrder);

    // The skeleton: Planning Center's new order, without what the operator moved.
    QList<ServiceItem> result;
    QHash<QUuid, QList<Change>> changesFor;
    for (qsizetype fi = 0; fi < fresh.items.size(); ++fi) {
        const ServiceItem &f = fresh.items[fi];
        if (inFresh.value(f.pcoItemId, -1) != fi)
            continue;   // no id, or a second item with one: Planning Center doesn't make those
        if (!matched.contains(f.pcoItemId)) {
            ServiceItem added = f;
            added.id = QUuid::createUuid();
            changesFor[added.id] << Change{Change::Kind::Added, added.id, added.title, {}, {}};
            result << added;
            continue;
        }
        if (movedLocally.contains(f.pcoItemId))
            continue;
        QList<Change> c;
        const ServiceItem merged = mergeItem(mine.items[matched[f.pcoItemId]], f, c);
        if (movedInPco.contains(f.pcoItemId))
            c.prepend(Change{Change::Kind::Moved, merged.id, merged.title, {}, {}});
        changesFor[merged.id] << c;
        result << merged;
    }

    // Then everything that keeps its place in the operator's list, right after the item
    // before it there. In list order, so a run of them stays together.
    for (qsizetype i = 0; i < mine.items.size(); ++i) {
        const ServiceItem &it = mine.items[i];
        const bool isMatched = matched.value(it.pcoItemId, -1) == i;
        if (isMatched && !movedLocally.contains(it.pcoItemId))
            continue;   // already in the skeleton
        ServiceItem kept = it;
        QList<Change> c;
        if (isMatched) {
            kept = mergeItem(it, fresh.items[inFresh[it.pcoItemId]], c);
            r.movedByOperator << it.pcoItemId;
            if (movedInPco.contains(it.pcoItemId))
                c.prepend(Change{Change::Kind::KeptPosition, kept.id, kept.title, {}, {}});
        } else if (!it.pcoItemId.isEmpty() && !inFresh.contains(it.pcoItemId) && !it.pcoRemoved) {
            kept.pcoRemoved = true;
            c << Change{Change::Kind::Removed, kept.id, kept.title, {}, {}};
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

    r.service.items = result;
    for (const ServiceItem &it : std::as_const(result))
        r.changes << changesFor.value(it.id);
    return r;
}

} // namespace visualis
