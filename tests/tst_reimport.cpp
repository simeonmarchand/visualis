#include "pco/planimport.h"
#include "pco/reimport.h"
#include "service/servicestore.h"
#include "library/library.h"

#include <QJsonArray>
#include <QRandomGenerator>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

using namespace visualis;
using Kind = ServiceItem::Kind;
using CK = ReimportChange::Kind;

namespace {

// Synthetic plans only: public-domain titles and made-up ids.
PcoItem song(const QString &id, const QString &title, QStringList sequence = {"Verse 1", "Chorus"})
{
    PcoItem it;
    it.id = id;
    it.type = PcoItemType::Song;
    it.title = title;
    it.song = PcoSong{"s" + id, title, {}, "Traditional", "Public Domain"};
    it.arrangement = PcoArrangement{"a" + id, "Default", std::move(sequence), 0, {}, 0};
    return it;
}

PcoItem plain(const QString &id, const QString &title, PcoItemType type = PcoItemType::Item)
{
    PcoItem it;
    it.id = id;
    it.type = type;
    it.title = title;
    return it;
}

// Planning Center numbers items in plan order.
QList<PcoItem> numbered(QList<PcoItem> items)
{
    for (int i = 0; i < items.size(); ++i)
        items[i].position = (i + 1) * 10;
    return items;
}

Service import(const QList<PcoItem> &items)
{
    PcoPlanItems p;
    p.items = numbered(items);
    PcoPlan plan;
    plan.id = "70001";
    return serviceFromPlan({"900", "Sunday"}, plan, p, QDateTime::currentDateTimeUtc()).service;
}

// The operator's copy: saved, so every item has an id.
Service local(const QList<PcoItem> &items)
{
    Service s = import(items);
    s.id = QUuid::createUuid();
    for (ServiceItem &it : s.items)
        it.id = QUuid::createUuid();
    return s;
}

QStringList titles(const Service &s)
{
    QStringList out;
    for (const ServiceItem &it : s.items)
        out << it.title;
    return out;
}

const ServiceItem *byPco(const Service &s, const QString &pcoId)
{
    for (const ServiceItem &it : s.items) {
        if (it.pcoItemId == pcoId)
            return &it;
    }
    return nullptr;
}

QList<CK> kinds(const ReimportResult &r)
{
    QList<CK> out;
    for (const ReimportChange &c : r.changes)
        out << c.kind;
    return out;
}

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
            if (at != to && !it.pcoItemId.isEmpty())
                ++run.reorders;
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
            run.options.unreadable << p.id;   // left out, but its id is known
            run.log << "unreadable " + p.title;
            continue;
        }
        PcoItem got = p;
        if (roll == 1 && got.song) {
            got.song.reset();   // its song record missing
            run.options.unreadable << p.id;
            run.log << "no song record " + p.title;
        }
        run.answer << got;
    }
    if (rng.bounded(20) == 0) {
        run.options.unreadableWithoutId = true;
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
        return isOwner(it) && inAnswer.contains(it.pcoItemId) && !o.unreadable.contains(it.pcoItemId)
               && !it.importSnapshot.isEmpty();
    };
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
        if (o.unreadable.contains(mine.pcoItemId)) {
            CHECK(m == mine, "an unreadable item changed: " + mine.title);
            CHECK(has(mine.id, CK::Unreadable), "no Unreadable line for " + mine.title);
            continue;
        }
        if (!inAnswer.contains(mine.pcoItemId)) {
            if (o.unreadableWithoutId) {
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
        CHECK(has(mine.id, CK::Relink) == relink, "Relink line for " + mine.title);
        CHECK(has(mine.id, CK::Changed, "kind") == (!chosen && !sameLink(orig, now)), "Changed kind line for " + mine.title);
        CHECK(has(mine.id, CK::KeptEdit, "kind") == (chosen && !relink && (swapped || !sameLink(orig, now)) && !sameLink(mine, now)),
              "kept-choice line for " + mine.title);
    }

    // What the operator deleted stays deleted: they're told if Planning Center changed it,
    // and it's forgotten once Planning Center removes it too, unless it may only have been unreadable.
    QList<RemovedPcoItem> expectDeleted;
    QSet<QString> addBack;
    for (auto it = deleted.cbegin(); it != deleted.cend(); ++it) {
        const RemovedPcoItem d{it.key(), it.value()};
        const ServiceItem *now = byPco(fresh, d.pcoItemId);
        if (!now) {
            if (o.unreadable.contains(d.pcoItemId) || o.unreadableWithoutId)
                expectDeleted << d;
            continue;
        }
        CHECK(!byPco(r.service, d.pcoItemId), "a deleted item came back: " + now->title);
        if (o.unreadable.contains(d.pcoItemId)) {
            expectDeleted << d;
            continue;
        }
        const bool changed = withoutPlace(d.importSnapshot) != withoutPlace(now->importSnapshot);
        const bool told = std::any_of(r.changes.cbegin(), r.changes.cend(), [&](const ReimportChange &c) {
            return c.kind == CK::ChangedButDeleted && c.pcoItemId == d.pcoItemId;
        });
        CHECK(told == changed, "ChangedButDeleted line for " + now->title);
        if (changed)
            addBack << d.pcoItemId;
        expectDeleted << RemovedPcoItem{d.pcoItemId, now->importSnapshot};
    }
    std::sort(expectDeleted.begin(), expectDeleted.end(),
              [](const RemovedPcoItem &a, const RemovedPcoItem &b) { return a.pcoItemId < b.pcoItemId; });
    CHECK(r.service.removedPcoItems == expectDeleted, "the deleted items' records");

    // Only when the operator asks.
    if (!addBack.isEmpty()) {
        ReimportOptions withBack = o;
        withBack.addBack = addBack;
        const ReimportResult back = mergeReimport(run.mine, fresh, withBack);
        for (const QString &id : addBack) {
            const ServiceItem *m = byPco(back.service, id);
            CHECK(m, "not added back");
            CHECK(std::none_of(back.service.removedPcoItems.cbegin(), back.service.removedPcoItems.cend(),
                               [&](const RemovedPcoItem &d) { return d.pcoItemId == id; }), "added back, still recorded deleted");
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
            CHECK(has(m->id, CK::Added), "no Added line for " + f.title);
            CHECK(sameLink(*m, f) && m->title == f.title, "added wrong: " + f.title);
        }
    }
    CHECK(out.size() == run.mine.items.size() + added, "item count");
    CHECK(std::count_if(r.changes.cbegin(), r.changes.cend(), [](const ReimportChange &c) { return c.kind == CK::Added; }) == added,
          "Added lines");
    CHECK(std::any_of(r.changes.cbegin(), r.changes.cend(), [](const ReimportChange &c) {
              return c.kind == CK::Unreadable && c.itemId.isNull();
          }) == o.unreadableWithoutId,
          "the line saying part of the plan couldn't be read");

    // Order. What keeps the operator's place stays right after the item before it in their list.
    const QSet<QString> moved(r.movedByOperator.cbegin(), r.movedByOperator.cend());
    CHECK(r.movedByOperator.size() <= run.reorders, "more items taken as moved than the operator moved");
    for (const ServiceItem &mine : run.mine.items) {
        if (!comparable(mine) || moved.contains(mine.pcoItemId))
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
            if (mine && comparable(*mine))
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
    std::sort(was.removedPcoItems.begin(), was.removedPcoItems.end(),
              [](const RemovedPcoItem &a, const RemovedPcoItem &b) { return a.pcoItemId < b.pcoItemId; });
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

class ReimportTest : public QObject
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

    void theSamePlanIsUpToDate()
    {
        const QList<PcoItem> plan{plain("1", "Worship", PcoItemType::Header), song("2", "Amazing Grace")};
        const Service mine = local(plan);
        const ReimportResult r = mergeReimport(mine, import(plan));
        QVERIFY(r.upToDate());
        QCOMPARE(r.summary(), QStringLiteral("Up to date with Planning Center."));
        QCOMPARE(r.service.items, mine.items);
    }

    void planningCentersChangesApply()
    {
        const Service mine = local({song("1", "Amazing Grace"), plain("2", "Welcome")});
        PcoItem retitled = song("1", "Amazing Grace (My Chains Are Gone)", {"Verse 1", "Verse 2"});
        const ReimportResult r = mergeReimport(mine, import({retitled, plain("2", "Welcome")}));
        QCOMPARE(r.service.items[0].title, retitled.title);
        QCOMPARE(r.service.items[0].customSequence, (QStringList{"Verse 1", "Verse 2"}));
        QCOMPARE(r.service.items[0].id, mine.items[0].id);
        QCOMPARE(kinds(r), (QList<CK>{CK::Changed, CK::Changed}));
        QCOMPARE(r.changes[0].detail, QStringLiteral("Amazing Grace → Amazing Grace (My Chains Are Gone)"));
        QCOMPARE(r.summary(), QStringLiteral("2 changed."));
    }

    void theOperatorsEditIsKept()
    {
        Service mine = local({song("1", "Amazing Grace")});
        mine.items[0].title = "Amazing Grace (choir)";
        const ReimportResult r = mergeReimport(mine, import({song("1", "Amazing Grace!")}));
        QCOMPARE(r.service.items[0].title, QStringLiteral("Amazing Grace (choir)"));
        QCOMPARE(kinds(r), QList<CK>{CK::KeptEdit});
        QCOMPARE(r.changes[0].detail, QStringLiteral("kept “Amazing Grace (choir)”; Planning Center has “Amazing Grace!”"));
        QCOMPARE(r.summary(), QStringLiteral("Kept 1 of your edits."));
    }

    void localWorkIsNeverTouched()
    {
        Service mine = local({song("1", "Amazing Grace"), plain("2", "Welcome")});
        mine.items[0].kind = Kind::Song;
        mine.items[0].ref = QUuid::createUuid();
        mine.items[0].overrides = QJsonObject{{"background", "blue-motion"}, {"slides", QJsonArray{"split"}}};
        mine.items[1].kind = Kind::Text;   // left as a blank slide
        const ReimportResult r = mergeReimport(mine, import({song("1", "Amazing Grace", {"Chorus"}), plain("2", "Welcome!")}));
        QCOMPARE(r.service.items[0].kind, Kind::Song);
        QCOMPARE(r.service.items[0].ref, mine.items[0].ref);
        QCOMPARE(r.service.items[0].overrides, mine.items[0].overrides);
        QCOMPARE(r.service.items[0].customSequence, QStringList{"Chorus"});
        QCOMPARE(r.service.items[1].kind, Kind::Text);
        QCOMPARE(r.service.items[1].title, QStringLiteral("Welcome!"));
    }

    void anotherSongInItsPlaceNeedsASongAgain()
    {
        Service mine = local({song("1", "Amazing Grace")});
        mine.items[0].kind = Kind::Song;
        mine.items[0].ref = QUuid::createUuid();
        mine.items[0].overrides = QJsonObject{{"background", "blue-motion"}};
        PcoItem other = song("1", "Be Thou My Vision");
        other.song->id = "s-other";
        const ReimportResult r = mergeReimport(mine, import({other}));
        QCOMPARE(r.service.items[0].kind, Kind::Placeholder);
        QVERIFY(r.service.items[0].ref.isNull());
        QCOMPARE(r.service.items[0].overrides, mine.items[0].overrides);
        QCOMPARE(kinds(r), (QList<CK>{CK::Relink, CK::Changed}));
        QCOMPARE(r.summary(), QStringLiteral("1 changed, 1 needs a song again."));
    }

    void aRemovedItemIsFlaggedAndCanComeBack()
    {
        const QList<PcoItem> plan{plain("1", "Welcome"), song("2", "Amazing Grace"), plain("3", "Sermon")};
        Service mine = local(plan);
        mine.items[1].overrides = QJsonObject{{"background", "blue-motion"}};
        const ReimportResult r = mergeReimport(mine, import({plan[0], plan[2]}));
        QCOMPARE(titles(r.service), (QStringList{"Welcome", "Amazing Grace", "Sermon"}));
        QVERIFY(r.service.items[1].pcoRemoved);
        QCOMPARE(r.service.items[1].overrides, mine.items[1].overrides);
        QCOMPARE(kinds(r), QList<CK>{CK::Removed});
        QVERIFY(mergeReimport(r.service, import({plan[0], plan[2]})).upToDate());   // said once

        const ReimportResult back = mergeReimport(r.service, import(plan));
        QVERIFY(!back.service.items[1].pcoRemoved);
        QCOMPARE(kinds(back), QList<CK>{CK::Restored});
    }

    void newItemsGoWherePlanningCenterPutsThem()
    {
        const Service mine = local({plain("1", "Welcome"), plain("3", "Sermon")});
        const ReimportResult r = mergeReimport(mine, import({plain("1", "Welcome"), song("2", "Amazing Grace"), plain("3", "Sermon")}));
        QCOMPARE(titles(r.service), (QStringList{"Welcome", "Amazing Grace", "Sermon"}));
        QCOMPARE(r.service.items[1].kind, Kind::Placeholder);
        QVERIFY(!r.service.items[1].id.isNull());
        QCOMPARE(kinds(r), QList<CK>{CK::Added});
    }

    void anItemTheOperatorAddedStaysAfterItsNeighbour()
    {
        Service mine = local({plain("1", "Welcome"), song("2", "Amazing Grace"), plain("3", "Sermon")});
        ServiceItem notice;
        notice.id = QUuid::createUuid();
        notice.kind = Kind::Text;
        notice.title = "Announcements";
        mine.items.insert(1, notice);   // after Welcome
        const ReimportResult r = mergeReimport(mine, import({song("2", "Amazing Grace"), plain("3", "Sermon"), plain("1", "Welcome")}));
        QCOMPARE(titles(r.service), (QStringList{"Amazing Grace", "Sermon", "Welcome", "Announcements"}));
        QCOMPARE(r.service.items[3], notice);
    }

    void planningCentersReorderApplies()
    {
        const Service mine = local({plain("1", "A"), plain("2", "B"), plain("3", "C")});
        const ReimportResult r = mergeReimport(mine, import({plain("3", "C"), plain("1", "A"), plain("2", "B")}));
        QCOMPARE(titles(r.service), (QStringList{"C", "A", "B"}));
        QCOMPARE(kinds(r), QList<CK>{CK::Moved});
        QCOMPARE(r.summary(), QStringLiteral("1 moved."));
    }

    // t4's decision on #56: the operator's move is an edit like any other.
    void theOperatorsMoveIsKept()
    {
        Service mine = local({plain("1", "A"), plain("2", "B"), plain("3", "C"), plain("4", "D")});
        mine.items.move(3, 0);   // D A B C
        const ReimportResult same = mergeReimport(mine, import({plain("1", "A"), plain("2", "B"), plain("3", "C"), plain("4", "D")}));
        QCOMPARE(titles(same.service), (QStringList{"D", "A", "B", "C"}));
        QVERIFY(same.upToDate());
        QCOMPARE(same.movedByOperator, QStringList{"4"});

        // Planning Center swaps A and B, and moves D too: A and B swap, D stays first.
        const ReimportResult both = mergeReimport(mine, import({plain("2", "B"), plain("4", "D"), plain("1", "A"), plain("3", "C")}));
        QCOMPARE(titles(both.service), (QStringList{"D", "B", "A", "C"}));
        QVERIFY(kinds(both).contains(CK::KeptPosition));
        QVERIFY(kinds(both).contains(CK::Moved));
    }

    // t4's decision on #56: deleting an imported item is a local edit too.
    void aDeletedItemStaysDeleted()
    {
        const QList<PcoItem> plan{plain("1", "Welcome"), song("2", "Amazing Grace"), plain("3", "Sermon")};
        Service mine = local(plan);
        mine.removedPcoItems = {{"2", mine.items[1].importSnapshot}};
        mine.items.removeAt(1);

        // Unchanged, though renumbered: nothing to say.
        const ReimportResult same = mergeReimport(mine, import({plain("1", "Welcome"), plain("4", "Notices"), song("2", "Amazing Grace"), plain("3", "Sermon")}));
        QCOMPARE(titles(same.service), (QStringList{"Welcome", "Notices", "Sermon"}));
        QCOMPARE(kinds(same), QList<CK>{CK::Added});
        QCOMPARE(same.service.removedPcoItems.size(), 1);

        // Changed: said, not added, until the operator asks.
        const QList<PcoItem> changed{plain("1", "Welcome"), song("2", "Amazing Grace", {"Chorus"}), plain("3", "Sermon")};
        const ReimportResult told = mergeReimport(mine, import(changed));
        QCOMPARE(titles(told.service), (QStringList{"Welcome", "Sermon"}));
        QCOMPARE(kinds(told), QList<CK>{CK::ChangedButDeleted});
        QCOMPARE(told.changes[0].pcoItemId, QStringLiteral("2"));
        QCOMPARE(told.changes[0].detail, QStringLiteral("changed in Planning Center; you removed it"));
        QCOMPARE(told.summary(), QStringLiteral("1 you removed changed in Planning Center."));
        QVERIFY(mergeReimport(told.service, import(changed)).upToDate());   // said once

        const ReimportResult back = mergeReimport(mine, import(changed), ReimportOptions{{"2"}, {}, false});
        QCOMPARE(titles(back.service), (QStringList{"Welcome", "Amazing Grace", "Sermon"}));
        QVERIFY(back.service.removedPcoItems.isEmpty());
        QCOMPARE(kinds(back), QList<CK>{CK::Added});

        // Gone from Planning Center too: forgotten.
        const ReimportResult gone = mergeReimport(mine, import({plain("1", "Welcome"), plain("3", "Sermon")}));
        QVERIFY(gone.service.removedPcoItems.isEmpty());
        // Review of #103 (M5): nothing to show, but forgetting the record is a change to save.
        QVERIFY(gone.changes.isEmpty());
        QVERIFY(gone.needsSave);
        QVERIFY(!gone.upToDate());
        QCOMPARE(gone.summary(), QStringLiteral("Nothing to review. Apply saves Planning Center's latest details."));
    }

    // Review of #103 (H1): what couldn't be read is never taken as a change.
    void anUnreadableItemIsLeftAsItWas()
    {
        const QList<PcoItem> plan{song("1", "Amazing Grace"), plain("2", "Welcome"), plain("3", "Sermon")};
        Service mine = local(plan);
        mine.items[0].kind = Kind::Song;
        mine.items[0].ref = QUuid::createUuid();
        mine.removedPcoItems = {{"3", mine.items[2].importSnapshot}};
        mine.items.removeAt(2);

        PcoItem noSong = song("1", "Amazing Grace");
        noSong.song.reset();   // its song record was missing from the answer
        ReimportOptions o;
        o.unreadable = {"1", "2", "3"};   // 2 and 3 were left out altogether
        const ReimportResult r = mergeReimport(mine, import({noSong}), o);
        QCOMPARE(r.service.items, mine.items);
        QCOMPARE(r.service.removedPcoItems, mine.removedPcoItems);
        QCOMPARE(kinds(r), (QList<CK>{CK::Unreadable, CK::Unreadable}));

        // A record with no id: anything missing may be it, so nothing is taken as removed.
        ReimportOptions noId;
        noId.unreadableWithoutId = true;
        const ReimportResult unsure = mergeReimport(mine, import({plan[0]}), noId);
        QVERIFY(!unsure.service.items[1].pcoRemoved);
        QCOMPARE(unsure.service.removedPcoItems, mine.removedPcoItems);
        QCOMPARE(kinds(unsure), QList<CK>{CK::Unreadable});
        QVERIFY(unsure.changes[0].itemId.isNull());
    }

    // Review of #103 (H2): only a linked library song gives way to Planning Center's new song.
    void aBlankSlideIsKeptWhenTheSongChanges()
    {
        Service mine = local({song("1", "Amazing Grace")});
        mine.items[0].kind = Kind::Text;   // left as a blank slide
        PcoItem other = song("1", "Amazing Grace");
        other.song->id = "s-other";
        const ReimportResult r = mergeReimport(mine, import({other}));
        QCOMPARE(r.service.items[0].kind, Kind::Text);
        QCOMPARE(kinds(r), QList<CK>{CK::KeptEdit});
        QCOMPARE(r.changes[0].detail, QStringLiteral("kept your text; Planning Center has another song here"));
    }

    // Review of #103 (H3): with no snapshot there's nothing to tell the operator's work from.
    void anItemWithNoSnapshotKeepsEverything()
    {
        Service mine = local({plain("1", "A"), song("2", "Amazing Grace"), plain("3", "C")});
        mine.items[1].kind = Kind::Song;
        mine.items[1].ref = QUuid::createUuid();
        mine.items[1].importSnapshot = {};
        mine.items.move(1, 0);   // and its place: it can't be compared either
        PcoItem renamed = song("2", "Amazing Grace (Live)");
        renamed.song->id = "s-other";
        const ReimportResult r = mergeReimport(mine, import({plain("1", "A"), renamed, plain("3", "C")}));
        QCOMPARE(titles(r.service), (QStringList{"Amazing Grace", "A", "C"}));
        QCOMPARE(r.service.items[0].kind, Kind::Song);
        QCOMPARE(r.service.items[0].ref, mine.items[0].ref);
        QCOMPARE(kinds(r), (QList<CK>{CK::KeptEdit, CK::KeptEdit}));   // what it shows, and its title
        QVERIFY(!r.service.items[0].importSnapshot.isEmpty());   // compared from now on
        QVERIFY(mergeReimport(r.service, import({plain("1", "A"), renamed, plain("3", "C")})).upToDate());
    }

    // Review of #103 (M4): of copies, the lowest item id is Planning Center's, whatever the order.
    void aCopyStaysTheOperatorsOwn()
    {
        Service mine = local({plain("1", "Welcome")});
        ServiceItem copy = mine.items[0];
        copy.id = QUuid::createUuid();
        copy.title = "Welcome (again)";
        mine.items << copy;
        const QUuid planningCenters = std::min(mine.items[0].id, copy.id);
        for (int round = 0; round < 2; ++round) {
            mine.items.move(0, 1);
            const ReimportResult r = mergeReimport(mine, import({plain("1", "Welcome!")}));
            for (const ServiceItem &it : r.service.items) {
                const ServiceItem &was = *std::find_if(mine.items.cbegin(), mine.items.cend(),
                                                       [&](const ServiceItem &x) { return x.id == it.id; });
                if (it.id == planningCenters)
                    QVERIFY(it.title == "Welcome!" || was.title == "Welcome (again)");
                else
                    QCOMPARE(it, was);
            }
        }
    }

    // Tied `sequence` numbers: the operator's move between the two isn't lost next time.
    void tiedPositionsKeepTheOperatorsMove()
    {
        QList<PcoItem> plan = numbered({plain("1", "A"), plain("2", "B"), plain("3", "C")});
        plan[1].position = plan[0].position;
        PcoPlanItems items;
        items.items = plan;
        PcoPlan p;
        p.id = "70001";
        Service mine = serviceFromPlan({"900", "Sunday"}, p, items, QDateTime::currentDateTimeUtc()).service;
        for (ServiceItem &it : mine.items)
            it.id = QUuid::createUuid();
        mine.items.move(1, 0);   // B before A
        const Service fresh = serviceFromPlan({"900", "Sunday"}, p, items, QDateTime::currentDateTimeUtc()).service;
        const ReimportResult once = mergeReimport(mine, fresh);
        QCOMPARE(titles(once.service), (QStringList{"B", "A", "C"}));
        const ReimportResult twice = mergeReimport(once.service, fresh);
        QCOMPARE(titles(twice.service), (QStringList{"B", "A", "C"}));
        QVERIFY(twice.upToDate());
    }

    void aSummaryCountsEachKind()
    {
        ReimportResult r;
        r.changes = {{CK::Added, {}, {}, {}, {}}, {CK::Added, {}, {}, {}, {}}, {CK::Removed, {}, {}, {}, {}},
                     {CK::KeptEdit, {}, {}, {}, {}}, {CK::KeptPosition, {}, {}, {}, {}}};
        QCOMPARE(r.summary(), QStringLiteral("2 added, 1 removed. Kept 2 of your edits."));
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

QTEST_GUILESS_MAIN(ReimportTest)
#include "tst_reimport.moc"
