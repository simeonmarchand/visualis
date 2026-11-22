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

struct Run {
    QList<PcoItem> before;    // the plan at the first import
    QList<PcoItem> after;     // the plan now
    Service original;         // the first import, as the operator first saw it
    Service mine;             // after the operator's edits
    bool pcoReordered = false;
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

    const int n = rng.bounded(2, 9);
    for (int i = 0; i < n; ++i)
        run.before << newPcoItem();
    run.before = numbered(run.before);
    run.original = local(run.before);
    run.mine = run.original;

    // The operator's edits.
    QList<ServiceItem> &items = run.mine.items;
    for (int k = rng.bounded(0, 6); k > 0; --k) {
        if (items.isEmpty())
            break;
        const qsizetype at = pick(items.size());
        ServiceItem &it = items[at];
        switch (rng.bounded(11)) {
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
        case 7: {
            const QString p = it.servicePosition == "pre" ? "post" : "pre";
            it.servicePosition = p;
            run.log << "position " + it.title;
            break;
        }
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
        default: {
            const qsizetype from = pick(items.size());
            const qsizetype to = pick(items.size());
            run.log << QStringLiteral("move %1 to %2").arg(items[from].title).arg(to);
            items.move(from, to);
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
            const qsizetype from = at;
            const qsizetype to = pick(plan.size());
            run.log << QStringLiteral("pco move %1 to %2").arg(p.title).arg(to);
            if (from != to)
                run.pcoReordered = true;
            plan.move(from, to);
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
    run.after = numbered(run.after);
    return run;
}

bool sameLink(const ServiceItem &a, const ServiceItem &b)
{
    return a.kind == b.kind && a.ref == b.ref && a.arrangement == b.arrangement;
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

// Every property of the merge, for one run. Empty when they all hold.
QString check(Run run, ServiceStore &store)
{
    // Saved twice, as the app does: the store notes what the operator deleted.
    CHECK(store.save(run.original).ok, "save the import");
    run.mine.id = run.original.id;
    run.mine.created = run.original.created;
    CHECK(store.save(run.mine).ok, "save the edits");
    QSet<QString> deleted;
    for (const RemovedPcoItem &d : run.mine.removedPcoItems)
        deleted << d.pcoItemId;

    const Service fresh = import(run.after);
    const ReimportResult r = mergeReimport(run.mine, fresh);
    const QList<ServiceItem> &out = r.service.items;

    QHash<QUuid, const ServiceItem *> merged;
    for (const ServiceItem &it : out) {
        CHECK(!it.id.isNull(), "an item without an id");
        CHECK(!merged.contains(it.id), "an item twice: " + it.title);
        merged.insert(it.id, &it);
    }
    QSet<QString> afterIds;
    for (const PcoItem &p : run.after)
        afterIds << p.id;
    QSet<QString> beforeIds;
    for (const PcoItem &p : run.before)
        beforeIds << p.id;
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

    // Nothing the operator has is lost.
    for (const ServiceItem &mine : run.mine.items) {
        CHECK(merged.contains(mine.id), "lost " + mine.title);
        const ServiceItem &m = *merged[mine.id];
        CHECK(m.overrides == mine.overrides, "overrides changed on " + mine.title);
        if (mine.pcoItemId.isEmpty()) {
            CHECK(m == mine, "a local item changed: " + mine.title);   // theirs alone
            continue;
        }
        if (!afterIds.contains(mine.pcoItemId)) {
            ServiceItem flagged = mine;
            flagged.pcoRemoved = true;
            CHECK(m == flagged, "a removed item wasn't only flagged: " + mine.title);
            CHECK(has(mine.id, CK::Removed), "no Removed line for " + mine.title);
            continue;
        }

        // Still in the plan: every field is the operator's if they changed it, else Planning Center's.
        const ServiceItem &orig = *byPco(run.original, mine.pcoItemId);
        const ServiceItem &now = *byPco(fresh, mine.pcoItemId);
        CHECK(!m.pcoRemoved, "flagged though still in the plan: " + mine.title);
        CHECK(m.importSnapshot == now.importSnapshot, "snapshot not refreshed: " + mine.title);
        struct F { QString name; std::function<QVariant(const ServiceItem &)> get; };
        const QList<F> fields{
            {"title", [](const ServiceItem &i) { return QVariant(i.title); }},
            {"sequence", [](const ServiceItem &i) { return QVariant(i.customSequence); }},
            {"service position", [](const ServiceItem &i) { return QVariant(i.servicePosition); }},
            {"reference", [](const ServiceItem &i) { return QVariant(i.body); }},
        };
        for (const F &f : fields) {
            const bool edited = f.get(mine) != f.get(orig);
            const bool pcoChanged = f.get(now) != f.get(orig);
            CHECK(f.get(m) == (edited ? f.get(mine) : f.get(now)), f.name + " of " + mine.title);
            CHECK(has(mine.id, CK::Changed, f.name) == (pcoChanged && !edited), "Changed line for " + f.name + " of " + mine.title);
            CHECK(has(mine.id, CK::KeptEdit, f.name) == (pcoChanged && edited && f.get(mine) != f.get(now)),
                  "KeptEdit line for " + f.name + " of " + mine.title);
        }
        const PcoItem &pb = *std::find_if(run.before.cbegin(), run.before.cend(), [&](const PcoItem &p) { return p.id == mine.pcoItemId; });
        const PcoItem &pa = *std::find_if(run.after.cbegin(), run.after.cend(), [&](const PcoItem &p) { return p.id == mine.pcoItemId; });
        const bool swapped = (pb.song ? pb.song->id : QString()) != (pa.song ? pa.song->id : QString());
        const bool linked = !sameLink(mine, orig);
        CHECK(sameLink(m, linked && !swapped ? mine : now), "what " + mine.title + " shows");
        CHECK(has(mine.id, CK::Relink) == (linked && swapped), "Relink line for " + mine.title);
    }

    // What the operator deleted stays deleted: they're told if Planning Center changed it,
    // and it's forgotten once Planning Center removes it too.
    QList<RemovedPcoItem> stillDeleted;
    for (const PcoItem &p : run.after) {
        if (!deleted.contains(p.id))
            continue;
        CHECK(!byPco(r.service, p.id), "a deleted item came back: " + p.title);
        const ServiceItem &orig = *byPco(run.original, p.id);
        QJsonObject was = orig.importSnapshot, now = byPco(fresh, p.id)->importSnapshot;
        was.remove("position");
        now.remove("position");
        const bool told = std::any_of(r.changes.cbegin(), r.changes.cend(), [&](const ReimportChange &c) {
            return c.kind == CK::ChangedButDeleted && c.pcoItemId == p.id;
        });
        CHECK(told == (was != now), "ChangedButDeleted line for " + p.title);
        stillDeleted << RemovedPcoItem{p.id, byPco(fresh, p.id)->importSnapshot};
    }
    std::sort(stillDeleted.begin(), stillDeleted.end(),
              [](const RemovedPcoItem &a, const RemovedPcoItem &b) { return a.pcoItemId < b.pcoItemId; });
    QList<RemovedPcoItem> kept = r.service.removedPcoItems;
    std::sort(kept.begin(), kept.end(), [](const RemovedPcoItem &a, const RemovedPcoItem &b) { return a.pcoItemId < b.pcoItemId; });
    CHECK(kept == stillDeleted, "the deleted items' records");

    // Only when the operator asks.
    QSet<QString> addBack;
    for (const ReimportChange &c : r.changes) {
        if (c.kind == CK::ChangedButDeleted)
            addBack << c.pcoItemId;
    }
    if (!addBack.isEmpty()) {
        const ReimportResult back = mergeReimport(run.mine, fresh, addBack);
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

    // Everything in the plan is there; what's new was added.
    int added = 0;
    for (const PcoItem &p : run.after) {
        if (deleted.contains(p.id))
            continue;
        const ServiceItem *m = byPco(r.service, p.id);
        CHECK(m, "missing from the plan: " + p.title);
        if (!beforeIds.contains(p.id)) {
            ++added;
            CHECK(has(m->id, CK::Added), "no Added line for " + p.title);
            CHECK(sameLink(*m, *byPco(fresh, p.id)) && m->title == byPco(fresh, p.id)->title, "added wrong: " + p.title);
        }
    }
    CHECK(out.size() == run.mine.items.size() + added, "item count");
    CHECK(std::count_if(r.changes.cbegin(), r.changes.cend(), [](const ReimportChange &c) { return c.kind == CK::Added; }) == added,
          "Added lines");

    // Order. An item the operator moved or added, or that Planning Center removed, stays
    // right after the item before it in the operator's list.
    const QSet<QString> moved(r.movedByOperator.cbegin(), r.movedByOperator.cend());
    for (const ServiceItem &mine : run.mine.items) {
        const bool anchored = mine.pcoItemId.isEmpty() || !afterIds.contains(mine.pcoItemId) || moved.contains(mine.pcoItemId);
        if (anchored)
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
    if (r.movedByOperator.isEmpty()) {
        // The operator reordered nothing of Planning Center's: its new order stands.
        QStringList pcoOrder, mergedOrder;
        for (const PcoItem &p : run.after) {
            if (!deleted.contains(p.id))
                pcoOrder << p.id;
        }
        for (const ServiceItem &it : out) {
            if (afterIds.contains(it.pcoItemId) && !mergedOrder.contains(it.pcoItemId))
                mergedOrder << it.pcoItemId;
        }
        CHECK(mergedOrder == pcoOrder, "Planning Center's order wasn't applied");
    }

    // The same plan again changes nothing.
    const ReimportResult again = mergeReimport(r.service, fresh);
    CHECK(again.upToDate(), "not idempotent: " + again.summary());
    CHECK(again.service.items == r.service.items, "not idempotent: the items changed");
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

        const ReimportResult back = mergeReimport(mine, import(changed), {"2"});
        QCOMPARE(titles(back.service), (QStringList{"Welcome", "Amazing Grace", "Sermon"}));
        QVERIFY(back.service.removedPcoItems.isEmpty());
        QCOMPARE(kinds(back), QList<CK>{CK::Added});

        // Gone from Planning Center too: forgotten.
        const ReimportResult gone = mergeReimport(mine, import({plain("1", "Welcome"), plain("3", "Sermon")}));
        QVERIFY(gone.service.removedPcoItems.isEmpty());
        QVERIFY(gone.upToDate());
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
