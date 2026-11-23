#include "pco/planimport.h"
#include "pco/reimport.h"

#include "reimportplans.h"

#include <QJsonArray>
#include <QtTest>

using namespace visualis;
using namespace visualis::testing;

// The merge's rules, one case each (#56). tst_reimportproperties checks them all at once on
// generated runs.
class ReimportTest : public QObject
{
    Q_OBJECT

private slots:
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

    // Second review of #103 (H1): old snapshots have no `order`, so tied numbers don't say
    // which came first; the operator's order of them stands.
    void oldTiedSnapshotsKeepTheOperatorsOrder()
    {
        QList<PcoItem> plan = numbered({plain("1", "A"), plain("2", "B"), plain("3", "C")});
        plan[1].position = plan[0].position;
        PcoPlanItems items;
        items.items = plan;
        PcoPlan p;
        p.id = "70001";
        Service mine = serviceFromPlan({"900", "Sunday"}, p, items, QDateTime::currentDateTimeUtc()).service;
        for (ServiceItem &it : mine.items) {
            it.id = QUuid::createUuid();
            it.importSnapshot.remove("order");   // imported before `order`
        }
        mine.items.move(1, 0);   // B before A
        const Service fresh = serviceFromPlan({"900", "Sunday"}, p, items, QDateTime::currentDateTimeUtc()).service;
        QCOMPARE(titles(mergeReimport(mine, fresh).service), (QStringList{"B", "A", "C"}));
    }

    // So a proposal can be worked out again and compared before it's applied.
    void theSameInputsGiveTheSameResult()
    {
        Service mine = local({plain("1", "Welcome")});
        const Service fresh = import({plain("1", "Welcome!"), song("2", "Amazing Grace")});
        const ReimportResult a = mergeReimport(mine, fresh);
        const ReimportResult b = mergeReimport(mine, fresh);
        QCOMPARE(a.service, b.service);
        QCOMPARE(a.changes, b.changes);
        QCOMPARE(a.linkable, b.linkable);
        QVERIFY(a.linkable.contains(byPco(a.service, "2")->id));
    }

    void aSummaryCountsEachKind()
    {
        ReimportResult r;
        r.changes = {{CK::Added, {}, {}, {}, {}}, {CK::Added, {}, {}, {}, {}}, {CK::Removed, {}, {}, {}, {}},
                     {CK::KeptEdit, {}, {}, {}, {}}, {CK::KeptPosition, {}, {}, {}, {}}};
        QCOMPARE(r.summary(), QStringLiteral("2 added, 1 removed. Kept 2 of your edits."));
    }
};

QTEST_GUILESS_MAIN(ReimportTest)
#include "tst_reimport.moc"
