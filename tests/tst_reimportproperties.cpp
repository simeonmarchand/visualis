#include "library/library.h"
#include "pco/planimport.h"
#include "pco/reimport.h"
#include "service/servicestore.h"

#include "reimportplans.h"

#include <QJsonArray>
#include <QRandomGenerator>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>
#include <bit>
#include <climits>
#include <optional>

using namespace visualis;
using namespace visualis::testing;

namespace {

// --- The generator: a plan, the operator's edits, Planning Center's edits. ---

// As it came: positions as the generator left them, ties and all.
PlanImport importAsIs(const QList<PcoItem> &items)
{
    PcoPlanItems p;
    p.items = items;
    PcoPlan plan;
    plan.id = "70001";
    return serviceFromPlan({"900", "Sunday"}, plan, p, QDateTime::currentDateTimeUtc());
}

struct Run {
    QList<PcoItem> before;    // the plan at the first import
    QList<PcoItem> after;     // the plan now
    QList<PcoItem> answer;    // what could be read of it: unreadable items left out, or without their song
    ReimportOptions options;  // what the read says it couldn't read
    Service original;         // the first import, as the operator first saw it
    Service mine;             // after the operator's edits
    bool pcoReordered = false;
    int reorders = 0;         // the operator's moves of Planning Center items, and copies made
    QSet<QUuid> movedItems;   // which items the operator moved
    bool legacy = false;      // imported before snapshots had `order`
    QStringList log;          // what was done, for the failure message
};

Run generate(quint32 seed)
{
    QRandomGenerator rng(seed);
    Run run;
    int counter = 0;
    const auto next = [&] { return QString::number(++counter); };
    const auto pick = [&](qsizetype n) { return qsizetype(rng.bounded(int(n))); };
    const auto newPcoItem = [&] {
        const QString id = "i" + next();
        switch (rng.bounded(4)) {
        case 0: return plain(id, "Worship " + id, PcoItemType::Header);
        case 1: return plain(id, "Scripture Reading: John 3:" + QString::number(rng.bounded(1, 30)));
        case 2: return plain(id, "Welcome " + id);
        default: return song(id, "Hymn " + id);
        }
    };
    // Planning Center's numbers, sometimes with two items sharing one.
    const auto renumber = [&](QList<PcoItem> items) {
        items = numbered(items);
        if (items.size() > 1 && rng.bounded(3) == 0) {
            const qsizetype k = 1 + pick(items.size() - 1);
            items[k].position = items[k - 1].position;
            run.log << "tie " + items[k].title;
        }
        return items;
    };

    const int n = rng.bounded(2, 9);
    for (int i = 0; i < n; ++i)
        run.before << newPcoItem();
    run.before = renumber(run.before);
    run.original = importAsIs(run.before).service;
    run.original.id = QUuid::createUuid();
    for (ServiceItem &it : run.original.items)
        it.id = QUuid::createUuid();
    if (rng.bounded(4) == 0) {
        // Imported by a version before `order`: tied numbers can't be told apart.
        run.legacy = true;
        for (ServiceItem &it : run.original.items)
            it.importSnapshot.remove("order");
        run.log << "old snapshots";
    }
    run.mine = run.original;

    // The operator's edits.
    QList<ServiceItem> &items = run.mine.items;
    for (int k = rng.bounded(0, 6); k > 0; --k) {
        if (items.isEmpty())
            break;
        const qsizetype at = pick(items.size());
        ServiceItem &it = items[at];
        switch (rng.bounded(13)) {
        case 0:
            it.overrides["slides"] = QJsonArray{"split-" + next()};
            run.log << "split " + it.title;
            break;
        case 1:
            it.overrides["background"] = "motion-" + next();
            run.log << "background " + it.title;
            break;
        case 2:
            if (it.kind == Kind::Placeholder) {
                it.kind = Kind::Song;
                it.ref = QUuid::createUuid();
                it.arrangement = QUuid::createUuid();
                run.log << "fill " + it.title;
            }
            break;
        case 3:
            it.kind = Kind::Text;
            it.ref = QUuid();
            it.body.clear();
            run.log << "blank " + it.title;
            break;
        case 4:
            run.log << "retitle " + it.title;
            it.title = "Local " + next();
            break;
        case 5:
            it.customSequence = {"Bridge " + next(), "Chorus"};
            run.log << "sequence " + it.title;
            break;
        case 6:
            it.body = "John 1:" + next();
            run.log << "reference " + it.title;
            break;
        case 7:
            it.servicePosition = it.servicePosition == "pre" ? "post" : "pre";
            run.log << "position " + it.title;
            break;
        case 8: {
            ServiceItem added;
            added.id = QUuid::createUuid();
            added.kind = Kind::Text;
            added.title = "Announcement " + next();
            added.overrides["background"] = "still";
            items.insert(pick(items.size() + 1), added);
            run.log << "add " + added.title;
            break;
        }
        case 9:
            run.log << "delete " + it.title;
            items.removeAt(at);
            break;
        case 10:
            // Imported before snapshots, or from a file: nothing to compare with.
            if (!it.pcoItemId.isEmpty()) {
                it.importSnapshot = {};
                run.log << "no snapshot " + it.title;
            }
            break;
        case 11: {
            ServiceItem copy = it;
            copy.id = QUuid::createUuid();
            copy.overrides["copy"] = next();
            items.insert(pick(items.size() + 1), copy);
            if (!copy.pcoItemId.isEmpty())
                ++run.reorders;   // the lower id may now be the copy, somewhere else
            run.log << "copy " + copy.title;
            break;
        }
        default: {
            const qsizetype to = pick(items.size());
            run.log << QStringLiteral("move %1 to %2").arg(it.title).arg(to);
            if (at != to && !it.pcoItemId.isEmpty()) {
                ++run.reorders;
                run.movedItems << it.id;
            }
            items.move(at, to);
            break;
        }
        }
    }

    // Planning Center's edits during the week.
    run.after = run.before;
    for (int k = rng.bounded(0, 6); k > 0; --k) {
        QList<PcoItem> &plan = run.after;
        const int op = plan.isEmpty() ? 1 : rng.bounded(7);
        if (op == 1) {
            const PcoItem p = newPcoItem();
            plan.insert(pick(plan.size() + 1), p);
            run.log << "pco add " + p.title;
            continue;
        }
        const qsizetype at = pick(plan.size());
        PcoItem &p = plan[at];
        switch (op) {
        case 0: {
            const qsizetype to = pick(plan.size());
            run.log << QStringLiteral("pco move %1 to %2").arg(p.title).arg(to);
            if (at != to)
                run.pcoReordered = true;
            plan.move(at, to);
            break;
        }
        case 2:
            run.log << "pco remove " + p.title;
            plan.removeAt(at);
            break;
        case 3:
            run.log << "pco retitle " + p.title;
            p.title = rng.bounded(2) ? "PCO " + next() : "Scripture Reading: Acts 2:" + next();
            break;
        case 4:
            if (p.type == PcoItemType::Song) {
                p.customArrangementSequence = {"Verse " + next(), "Chorus"};
                run.log << "pco sequence " + p.title;
            }
            break;
        case 5:
            if (p.song) {
                p.song->id = "swapped" + next();
                run.log << "pco swap song " + p.title;
            }
            break;
        default:
            p.servicePosition = p.servicePosition == PcoServicePosition::Post ? PcoServicePosition::Pre
                                                                              : PcoServicePosition::Post;
            run.log << "pco position " + p.title;
            break;
        }
    }
    run.after = renumber(run.after);

    // What came back of it: now and then a record that couldn't be read (H1 of #103's review).
    for (const PcoItem &p : std::as_const(run.after)) {
        const int roll = rng.bounded(14);
        if (roll == 0) {
            run.options.unreadable.ids << p.id;   // left out, but its id is known
            run.log << "unreadable " + p.title;
            continue;
        }
        PcoItem got = p;
        if (roll == 1 && got.song) {
            got.song.reset();   // its song record missing
            run.options.unreadable.ids << p.id;
            run.log << "no song record " + p.title;
        }
        run.answer << got;
    }
    if (rng.bounded(20) == 0) {
        run.options.unreadable.withoutId = true;
        if (!run.answer.isEmpty() && rng.bounded(2) == 0)
            run.answer.removeAt(pick(run.answer.size()));
        run.log << "a record without an id";
    }
    return run;
}

bool sameLink(const ServiceItem &a, const ServiceItem &b)
{
    return a.kind == b.kind && a.ref == b.ref && a.arrangement == b.arrangement;
}

QString songOf(const ServiceItem &it) { return it.importSnapshot.value("song").toObject().value("id").toString(); }

QJsonObject withoutPlace(QJsonObject snapshot)
{
    snapshot.remove("position");
    snapshot.remove("order");
    return snapshot;
}

QUuid before(const QList<ServiceItem> &items, const QUuid &id)
{
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (items[i].id == id)
            return i == 0 ? QUuid() : items[i - 1].id;
    }
    return QUuid::createUuid();   // not there: can't match anything
}

#define CHECK(cond, what) \
    do { if (!(cond)) return QStringLiteral("%1 (line %2)").arg(QString(what)).arg(__LINE__); } while (0)

// Every property of the merge, for one run, from what the generator did rather than from
// what the merge says it did. Empty when they all hold.
QString check(Run run, ServiceStore &store)
{
    // Saved twice, as the app does: the store notes what the operator deleted.
    CHECK(store.save(run.original).ok, "save the import");
    run.mine.id = run.original.id;
    run.mine.created = run.original.created;
    CHECK(store.save(run.mine).ok, "save the edits");
    // What the operator deleted, from what they did rather than from what the store kept:
    // Planning Center items of the first import with no copy left.
    QHash<QString, QJsonObject> deleted;
    for (const ServiceItem &it : run.original.items)
        deleted.insert(it.pcoItemId, it.importSnapshot);
    for (const ServiceItem &it : run.mine.items)
        deleted.remove(it.pcoItemId);

    const Service fresh = importAsIs(run.answer).service;
    const ReimportOptions &o = run.options;
    const ReimportResult r = mergeReimport(run.mine, fresh, o);
    const QList<ServiceItem> &out = r.service.items;

    QHash<QUuid, const ServiceItem *> merged;
    for (const ServiceItem &it : out) {
        CHECK(!it.id.isNull(), "an item without an id");
        CHECK(!merged.contains(it.id), "an item twice: " + it.title);
        merged.insert(it.id, &it);
    }
    QSet<QString> inAnswer;
    for (const PcoItem &p : run.answer)
        inAnswer << p.id;
    // The Planning Center item of each id: the lowest item id of the operator's copies.
    QHash<QString, QUuid> owner;
    for (const ServiceItem &it : run.mine.items) {
        if (!it.pcoItemId.isEmpty() && (!owner.contains(it.pcoItemId) || it.id < owner[it.pcoItemId]))
            owner[it.pcoItemId] = it.id;
    }
    const auto isOwner = [&](const ServiceItem &it) { return !it.pcoItemId.isEmpty() && owner.value(it.pcoItemId) == it.id; };
    const auto comparable = [&](const ServiceItem &it) {
        return isOwner(it) && inAnswer.contains(it.pcoItemId) && !o.unreadable.ids.contains(it.pcoItemId)
               && !it.importSnapshot.isEmpty();
    };
    // Where an item was last time; items sharing one can't be ordered, so keep the operator's place.
    const auto place = [](const ServiceItem &it) {
        return std::pair{it.importSnapshot.value("position").toInt(), it.importSnapshot.value("order").toInt(INT_MAX)};
    };
    QHash<std::pair<int, int>, int> atPlace;
    for (const ServiceItem &it : run.mine.items) {
        if (comparable(it))
            ++atPlace[place(it)];
    }
    const auto ordered = [&](const ServiceItem &it) { return comparable(it) && atPlace.value(place(it)) == 1; };
    QSet<QUuid> expectLinkable;
    const auto changesOf = [&](const QUuid &id) {
        QList<ReimportChange> c;
        for (const ReimportChange &x : r.changes) {
            if (x.itemId == id)
                c << x;
        }
        return c;
    };
    const auto has = [&](const QUuid &id, CK kind, const QString &field = {}) {
        for (const ReimportChange &c : changesOf(id)) {
            if (c.kind == kind && c.field == field)
                return true;
        }
        return false;
    };
    struct F { QString name; std::function<QVariant(const ServiceItem &)> get; };
    const QList<F> fields{
        {"title", [](const ServiceItem &i) { return QVariant(i.title); }},
        {"sequence", [](const ServiceItem &i) { return QVariant(i.customSequence); }},
        {"service position", [](const ServiceItem &i) { return QVariant(i.servicePosition); }},
        {"reference", [](const ServiceItem &i) { return QVariant(i.body); }},
    };

    // Nothing the operator has is lost.
    for (const ServiceItem &mine : run.mine.items) {
        CHECK(merged.contains(mine.id), "lost " + mine.title);
        const ServiceItem &m = *merged[mine.id];
        CHECK(m.overrides == mine.overrides, "overrides changed on " + mine.title);
        if (!isOwner(mine)) {
            CHECK(m == mine, "an item of the operator's own changed: " + mine.title);   // added, or a copy
            continue;
        }
        if (o.unreadable.ids.contains(mine.pcoItemId)) {
            CHECK(m == mine, "an unreadable item changed: " + mine.title);
            CHECK(has(mine.id, CK::Unreadable), "no Unreadable line for " + mine.title);
            continue;
        }
        if (!inAnswer.contains(mine.pcoItemId)) {
            if (o.unreadable.withoutId) {
                CHECK(m == mine, "taken as removed though part of the plan couldn't be read: " + mine.title);
                continue;
            }
            ServiceItem flagged = mine;
            flagged.pcoRemoved = true;
            CHECK(m == flagged, "a removed item wasn't only flagged: " + mine.title);
            CHECK(has(mine.id, CK::Removed), "no Removed line for " + mine.title);
            continue;
        }
        const ServiceItem &now = *byPco(fresh, mine.pcoItemId);
        CHECK(!m.pcoRemoved, "flagged though still in the plan: " + mine.title);
        CHECK(m.importSnapshot == now.importSnapshot, "snapshot not refreshed: " + mine.title);
        if (mine.importSnapshot.isEmpty()) {
            // Nothing to compare with: all of it is the operator's, and differences are listed.
            CHECK(sameLink(m, mine), "what " + mine.title + " shows, with no snapshot");
            CHECK(has(mine.id, CK::KeptEdit, "kind") == !sameLink(mine, now), "kind line, no snapshot: " + mine.title);
            for (const F &f : fields) {
                CHECK(f.get(m) == f.get(mine), f.name + " of " + mine.title + ", no snapshot");
                CHECK(has(mine.id, CK::KeptEdit, f.name) == (f.get(mine) != f.get(now)), f.name + " line, no snapshot: " + mine.title);
            }
            continue;
        }

        // Every field is the operator's if they changed it, else Planning Center's.
        const ServiceItem &orig = *byPco(run.original, mine.pcoItemId);
        for (const F &f : fields) {
            const bool edited = f.get(mine) != f.get(orig);
            const bool pcoChanged = f.get(now) != f.get(orig);
            CHECK(f.get(m) == (edited ? f.get(mine) : f.get(now)), f.name + " of " + mine.title);
            CHECK(has(mine.id, CK::Changed, f.name) == (pcoChanged && !edited), "Changed line for " + f.name + " of " + mine.title);
            CHECK(has(mine.id, CK::KeptEdit, f.name) == (pcoChanged && edited && f.get(mine) != f.get(now)),
                  "KeptEdit line for " + f.name + " of " + mine.title);
        }
        // What it shows is the operator's choice, unless they linked a library song and
        // Planning Center put a different song here.
        const bool swapped = !songOf(orig).isEmpty() && !songOf(now).isEmpty() && songOf(orig) != songOf(now);
        const bool chosen = !sameLink(mine, orig);
        const bool relink = chosen && mine.kind == Kind::Song && !mine.ref.isNull() && swapped;
        CHECK(sameLink(m, relink || !chosen ? now : mine), "what " + mine.title + " shows");
        if (relink || !chosen)
            expectLinkable << mine.id;   // Planning Center's placeholder: linking it undoes nothing
        CHECK(has(mine.id, CK::Relink) == relink, "Relink line for " + mine.title);
        CHECK(has(mine.id, CK::Changed, "kind") == (!chosen && !sameLink(orig, now)), "Changed kind line for " + mine.title);
        CHECK(has(mine.id, CK::KeptEdit, "kind") == (chosen && !relink && (swapped || !sameLink(orig, now)) && !sameLink(mine, now)),
              "kept-choice line for " + mine.title);
    }

    // What the operator deleted stays deleted: they're told if Planning Center changed it,
    // and it's forgotten once Planning Center removes it too, unless it may only have been unreadable.
    QMap<QString, QJsonObject> expectDeleted;
    QSet<QString> addBack;
    for (auto it = deleted.cbegin(); it != deleted.cend(); ++it) {
        const QString &id = it.key();
        const ServiceItem *now = byPco(fresh, id);
        if (!now) {
            if (o.unreadable.ids.contains(id) || o.unreadable.withoutId)
                expectDeleted.insert(id, it.value());
            continue;
        }
        CHECK(!byPco(r.service, id), "a deleted item came back: " + now->title);
        if (o.unreadable.ids.contains(id)) {
            expectDeleted.insert(id, it.value());
            continue;
        }
        const bool changed = withoutPlace(it.value()) != withoutPlace(now->importSnapshot);
        const bool told = std::any_of(r.changes.cbegin(), r.changes.cend(), [&](const ReimportChange &c) {
            return c.kind == CK::ChangedButDeleted && c.pcoItemId == id;
        });
        CHECK(told == changed, "ChangedButDeleted line for " + now->title);
        if (changed)
            addBack << id;
        expectDeleted.insert(id, now->importSnapshot);
    }
    CHECK(r.service.removedPcoItems == expectDeleted, "the deleted items' records");

    // Only when the operator asks.
    if (!addBack.isEmpty()) {
        ReimportOptions withBack = o;
        withBack.addBack = addBack;
        const ReimportResult back = mergeReimport(run.mine, fresh, withBack);
        for (const QString &id : addBack) {
            const ServiceItem *m = byPco(back.service, id);
            CHECK(m, "not added back");
            CHECK(!back.service.removedPcoItems.contains(id), "added back, still recorded deleted");
            CHECK(std::any_of(back.changes.cbegin(), back.changes.cend(),
                              [&](const ReimportChange &c) { return c.kind == CK::Added && c.itemId == m->id; }),
                  "no Added line for an item added back");
        }
        CHECK(back.service.items.size() == out.size() + addBack.size(), "adding back changed other items");
    }

    // Everything read from the plan is there; what's new was added.
    int added = 0;
    for (const ServiceItem &f : fresh.items) {
        if (deleted.contains(f.pcoItemId) && !owner.contains(f.pcoItemId))
            continue;
        const ServiceItem *m = byPco(r.service, f.pcoItemId);
        CHECK(m, "missing from the plan: " + f.title);
        if (!owner.contains(f.pcoItemId)) {
            ++added;
            expectLinkable << m->id;
            CHECK(has(m->id, CK::Added), "no Added line for " + f.title);
            CHECK(sameLink(*m, f) && m->title == f.title, "added wrong: " + f.title);
        }
    }
    CHECK(out.size() == run.mine.items.size() + added, "item count");
    CHECK(std::count_if(r.changes.cbegin(), r.changes.cend(), [](const ReimportChange &c) { return c.kind == CK::Added; }) == added,
          "Added lines");
    CHECK(std::any_of(r.changes.cbegin(), r.changes.cend(), [](const ReimportChange &c) {
              return c.kind == CK::Unreadable && c.itemId.isNull();
          }) == o.unreadable.withoutId,
          "the line saying part of the plan couldn't be read");

    CHECK(r.linkable == expectLinkable, "which items may be linked to the library");

    // Order. Who moved what, worked out here from the orders alone (review of #103, M7):
    // every smallest set of items whose removal leaves the operator's order agreeing with
    // last time's is an explanation of their moves.
    QStringList localOrder;
    for (const ServiceItem &it : run.mine.items) {
        if (ordered(it))
            localOrder << it.pcoItemId;
    }
    QHash<QString, std::pair<int, int>> baseRank;
    for (const ServiceItem &it : run.mine.items) {
        if (ordered(it))
            baseRank[it.pcoItemId] = place(it);
    }
    const auto explanations = [&](const QStringList &order, const auto &rank) {
        QList<QSet<QString>> best;
        const int n = int(order.size());
        for (int size = 0; size <= n && best.isEmpty(); ++size) {
            for (quint32 mask = 0; mask < (1u << n); ++mask) {
                if (std::popcount(mask) != size)
                    continue;
                bool agrees = true;
                std::optional<std::pair<int, int>> last;
                for (int i = 0; i < n && agrees; ++i) {
                    if (mask & (1u << i))
                        continue;
                    agrees = !last || *last < rank(order[i]);
                    last = rank(order[i]);
                }
                if (agrees) {
                    QSet<QString> moved;
                    for (int i = 0; i < n; ++i) {
                        if (mask & (1u << i))
                            moved << order[i];
                    }
                    best << moved;
                }
            }
        }
        return best;
    };
    const QList<QSet<QString>> operatorMoves = explanations(localOrder, [&](const QString &id) { return baseRank[id]; });
    const QSet<QString> moved(r.movedByOperator.cbegin(), r.movedByOperator.cend());
    CHECK(operatorMoves.contains(moved), "what's taken as moved by the operator doesn't explain their order");
    QSet<QString> generatorMoved;
    for (const ServiceItem &it : run.mine.items) {
        if (ordered(it) && run.movedItems.contains(it.id))
            generatorMoved << it.pcoItemId;
    }
    if (operatorMoves.size() == 1)   // then it's the operator's actual moves whenever those are the smallest
        CHECK(moved == operatorMoves[0] && (!operatorMoves.contains(generatorMoved) || moved == generatorMoved),
              "the operator's moves, when only one explanation fits");
    // When it's ambiguous, the explanation that leaves the operator's order standing against
    // Planning Center's moves is taken, if Planning Center's are clear.
    QStringList pcoNow;
    QHash<QString, int> freshRank;
    for (qsizetype i = 0; i < fresh.items.size(); ++i)
        freshRank.insert(fresh.items[i].pcoItemId, int(i));
    for (const QString &id : localOrder)
        pcoNow << id;
    std::sort(pcoNow.begin(), pcoNow.end(), [&](const QString &a, const QString &b) { return freshRank[a] < freshRank[b]; });
    const QList<QSet<QString>> pcoMoves = explanations(pcoNow, [&](const QString &id) { return baseRank[id]; });
    if (pcoMoves.size() == 1 && operatorMoves.size() > 1) {
        qsizetype most = 0;
        for (const QSet<QString> &e : operatorMoves)
            most = std::max(most, QSet<QString>(e).intersect(pcoMoves[0]).size());
        CHECK(QSet<QString>(moved).intersect(pcoMoves[0]).size() == most, "an ambiguous move went Planning Center's way");
    }
    // What keeps the operator's place stays right after the item before it in their list.
    for (const ServiceItem &mine : run.mine.items) {
        if (!ordered(mine) || moved.contains(mine.pcoItemId))
            CHECK(before(out, mine.id) == before(run.mine.items, mine.id), "not kept after its neighbour: " + mine.title);
        CHECK(!has(mine.id, CK::KeptPosition) || moved.contains(mine.pcoItemId), "KeptPosition on an item not kept in place");
        CHECK(!has(mine.id, CK::Moved) || !moved.contains(mine.pcoItemId), "Moved on an item kept in place");
    }
    if (!run.pcoReordered) {
        // Planning Center reordered nothing: the operator's order stands, moves and all.
        QList<QUuid> mineOrder, mergedOrder;
        for (const ServiceItem &it : run.mine.items)
            mineOrder << it.id;
        for (const ServiceItem &it : out) {
            if (mineOrder.contains(it.id))
                mergedOrder << it.id;
        }
        CHECK(mergedOrder == mineOrder, "the operator's order wasn't kept");
        CHECK(!kinds(r).contains(CK::Moved) && !kinds(r).contains(CK::KeptPosition), "a move nobody made");
    }
    if (run.reorders == 0) {
        // The operator reordered nothing of Planning Center's: its new order stands.
        CHECK(r.movedByOperator.isEmpty(), "taken as moved, though the operator moved nothing");
        QStringList pcoOrder, mergedOrder;
        for (const ServiceItem &f : fresh.items) {
            const ServiceItem *mine = nullptr;
            for (const ServiceItem &it : run.mine.items) {
                if (isOwner(it) && it.pcoItemId == f.pcoItemId)
                    mine = &it;
            }
            if (mine && ordered(*mine))
                pcoOrder << f.pcoItemId;
        }
        for (const ServiceItem &it : out) {
            if (pcoOrder.contains(it.pcoItemId) && owner.value(it.pcoItemId) == it.id)
                mergedOrder << it.pcoItemId;
        }
        CHECK(mergedOrder == pcoOrder, "Planning Center's order wasn't applied");
    }

    // Whether there's anything to save.
    Service was = run.mine, now = r.service;
    now.lastImportedAt = was.lastImportedAt;
    CHECK(r.needsSave == (now != was), "needsSave");

    // The same plan again changes nothing; only what still can't be read is said again.
    const ReimportResult again = mergeReimport(r.service, fresh, o);
    CHECK(!again.needsSave, "not idempotent: something to save");
    CHECK(std::all_of(again.changes.cbegin(), again.changes.cend(), [](const ReimportChange &c) { return c.kind == CK::Unreadable; }),
          "not idempotent: " + again.summary());
    Service a = again.service;
    a.lastImportedAt = r.service.lastImportedAt;
    CHECK(a == r.service, "not idempotent: the service changed");
    return {};
}

} // namespace

class ReimportPropertiesTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<ServiceStore> m_store;

private slots:
    void initTestCase()
    {
        auto r = Library::open(m_dir.path());
        QVERIFY2(r.library, qPrintable(r.message));
        m_library = std::move(r.library);
        m_store = std::make_unique<ServiceStore>(*m_library);
    }

    void cleanupTestCase()
    {
        m_store.reset();
        m_library.reset();
    }

    // The issue's acceptance test, as properties: thousands of generated weeks of edits in
    // Planning Center and local edits, and local work is never lost.
    void neverLosesLocalWork()
    {
        const int runs = qEnvironmentVariableIntValue("VISUALIS_REIMPORT_RUNS") > 0
                             ? qEnvironmentVariableIntValue("VISUALIS_REIMPORT_RUNS") : 3000;
        const quint32 first = quint32(qEnvironmentVariableIntValue("VISUALIS_REIMPORT_SEED"));
        for (quint32 seed = first; seed < first + quint32(runs); ++seed) {
            const Run run = generate(seed);
            const QString failure = check(run, *m_store);
            QVERIFY2(failure.isEmpty(), qPrintable(QStringLiteral("seed %1: %2\n  %3")
                                                       .arg(seed).arg(failure, run.log.join(QStringLiteral("\n  ")))));
        }
    }
};

QTEST_GUILESS_MAIN(ReimportPropertiesTest)
#include "tst_reimportproperties.moc"
