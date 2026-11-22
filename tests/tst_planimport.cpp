#include "library/library.h"
#include "pco/planimport.h"
#include "service/servicestore.h"
#include "song/slidelist.h"

#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

using namespace visualis;
using Kind = ServiceItem::Kind;

namespace {

// Synthetic, shaped like the P3 sandbox dump: no church data. Songs are public domain.
PcoItem item(const QString &id, PcoItemType type, const QString &title, int position,
             PcoServicePosition where = PcoServicePosition::During)
{
    PcoItem it;
    it.id = id;
    it.type = type;
    it.title = title;
    it.position = position;
    it.servicePosition = where;
    return it;
}

PcoItem song(const QString &id, const QString &title, int position, QStringList arrangementSequence,
             QStringList custom = {})
{
    PcoItem it = item(id, PcoItemType::Song, title, position);
    it.song = PcoSong{"s" + id, title, " 22025", "John Newton ", "  Public Domain"};
    it.arrangement = PcoArrangement{"a" + id, "Default", std::move(arrangementSequence), 72, "3/4", 240};
    it.customArrangementSequence = std::move(custom);
    it.keyName = "G";
    return it;
}

PcoPlan plan()
{
    PcoPlan p;
    p.id = "70001";
    p.serviceTypeId = "900";
    p.dates = "January 10, 2027";
    p.sortDate = QDateTime(QDate(2027, 1, 10), QTime(12, 0), QTimeZone::UTC);   // noon: same date anywhere near
    p.itemsCount = 7;
    p.planningCenterUrl = QUrl("https://services.planningcenteronline.com/plans/70001");
    return p;
}

const PcoServiceType kType{"900", "Sunday Morning"};

// Out of order on purpose: the import must sort by Planning Center's position.
PcoPlanItems items()
{
    PcoPlanItems p;
    p.items = {
        song("3", "Amazing Grace", 3, {"Intro", "Verse 1", "Verse 2", "Chorus", "Turnaround", "Verse 3", "Ending"}),
        item("1", PcoItemType::Item, "Countdown", 1, PcoServicePosition::Pre),
        item("2", PcoItemType::Header, "Worship", 2),
        song("4", "Holy, Holy, Holy", 4, {"Verse 1", "Verse 2", "Verse 3"}, {"Verse 1", "Verse 3", "Interlude", "Verse 3"}),
        item("5", PcoItemType::Item, "Scripture Reading: Acts 2:38", 5),
        item("6", PcoItemType::Media, "Announcements video", 6),
        item("7", PcoItemType::Item, "Postlude", 7, PcoServicePosition::Post),
    };
    return p;
}

QStringList titles(const Service &s)
{
    QStringList out;
    for (const ServiceItem &it : s.items)
        out << it.title;
    return out;
}

const QDateTime kImported(QDate(2027, 1, 8), QTime(18, 30), QTimeZone::UTC);

} // namespace

class PlanImportTest : public QObject
{
    Q_OBJECT

private slots:
    void itemsKeepPlanningCentersOrderAndPositions()
    {
        const Service s = serviceFromPlan(kType, plan(), items(), kImported).service;
        QCOMPARE(titles(s), (QStringList{"Countdown", "Worship", "Amazing Grace", "Holy, Holy, Holy",
                                         "Scripture Reading: Acts 2:38", "Announcements video", "Postlude"}));
        QStringList where;
        for (const ServiceItem &it : s.items)
            where << it.servicePosition;
        QCOMPARE(where, (QStringList{"pre", "", "", "", "", "", "post"}));
        for (int i = 0; i < s.items.size(); ++i)
            QCOMPARE(s.items[i].pcoItemId, QString::number(i + 1));
    }

    void equalPositionsKeepTheirOrder()
    {
        PcoPlanItems p;
        p.items = {item("a", PcoItemType::Item, "First", 1), item("b", PcoItemType::Item, "Second", 1)};
        QCOMPARE(titles(serviceFromPlan(kType, plan(), p, kImported).service), (QStringList{"First", "Second"}));
    }

    void theServiceLinksBackToThePlan()
    {
        const Service s = serviceFromPlan(kType, plan(), items(), kImported).service;
        QCOMPARE(s.pcoPlanId, QStringLiteral("70001"));
        QCOMPARE(s.pcoServiceTypeId, QStringLiteral("900"));
        QCOMPARE(s.pcoPlanUrl, QStringLiteral("https://services.planningcenteronline.com/plans/70001"));
        QCOMPARE(s.lastImportedAt, kImported);
        QCOMPARE(s.date, QDate(2027, 1, 10));
        QVERIFY(s.id.isNull());   // new until saved
    }

    void theTitleFallsBackToTheServiceType()
    {
        PcoPlan p = plan();
        QCOMPARE(serviceFromPlan(kType, p, {}, kImported).service.title, QStringLiteral("Sunday Morning"));
        p.seriesTitle = "Advent";
        QCOMPARE(serviceFromPlan(kType, p, {}, kImported).service.title, QStringLiteral("Advent"));
        p.title = "  Christmas Eve ";
        QCOMPARE(serviceFromPlan(kType, p, {}, kImported).service.title, QStringLiteral("Christmas Eve"));
        QCOMPARE(serviceFromPlan({"900", ""}, plan(), {}, kImported).service.title, QStringLiteral("January 10, 2027"));
        PcoPlan bare;
        bare.id = "5";
        QCOMPARE(serviceFromPlan({}, bare, {}, kImported).service.title, QStringLiteral("Planning Center plan 5"));
    }

    void kindsFollowTheItemType()
    {
        const Service s = serviceFromPlan(kType, plan(), items(), kImported).service;
        QCOMPARE(s.items[0].kind, Kind::Placeholder);   // a plain item
        QCOMPARE(s.items[1].kind, Kind::Header);
        QCOMPARE(s.items[2].kind, Kind::Placeholder);   // songs, until #55 matches them
        QCOMPARE(s.items[3].kind, Kind::Placeholder);
        QCOMPARE(s.items[4].kind, Kind::Scripture);
        QCOMPARE(s.items[5].kind, Kind::Media);
        QVERIFY(s.items[5].ref.isNull());
    }

    // P3 finding 6: there's no scripture item type, only a title with a reference in it.
    void scriptureTitlesBecomeScripturePlaceholders()
    {
        PcoPlanItems p;
        p.items = {item("1", PcoItemType::Item, "Acts 2:38", 1),
                   item("2", PcoItemType::Item, "Scripture Reading: Romans 8:28-39", 2),
                   item("3", PcoItemType::Item, "Psalm 23", 3),
                   item("4", PcoItemType::Item, "Doors open 9:30", 4),
                   item("5", PcoItemType::Header, "John 3:16", 5)};   // a header stays a header
        const Service s = serviceFromPlan(kType, plan(), p, kImported).service;
        QCOMPARE(s.items[0].kind, Kind::Scripture);
        QCOMPARE(s.items[0].body, QStringLiteral("Acts 2:38"));
        QCOMPARE(s.items[1].kind, Kind::Scripture);
        QCOMPARE(s.items[1].body, QStringLiteral("Romans 8:28-39"));
        QCOMPARE(s.items[1].title, QStringLiteral("Scripture Reading: Romans 8:28-39"));
        QCOMPARE(s.items[2].kind, Kind::Scripture);
        QCOMPARE(s.items[3].kind, Kind::Placeholder);
        QCOMPARE(s.items[4].kind, Kind::Header);
    }

    // P3 finding 4: the item's own order wins over the arrangement's.
    void aCustomSequenceWinsOverTheArrangements()
    {
        const Service s = serviceFromPlan(kType, plan(), items(), kImported).service;
        QCOMPARE(s.items[3].customSequence, (QStringList{"Verse 1", "Verse 3", "Interlude", "Verse 3"}));
    }

    // P3 finding 9: the song map keeps lyric-less sections, which play as blank slides.
    void lyriclessSectionsAreKeptAndPlayBlank()
    {
        const Service s = serviceFromPlan(kType, plan(), items(), kImported).service;
        const QStringList sequence = s.items[2].customSequence;
        QCOMPARE(sequence, (QStringList{"Intro", "Verse 1", "Verse 2", "Chorus", "Turnaround", "Verse 3", "Ending"}));

        // Once #55 links the song, the sequence plays with blanks where there are no lyrics.
        Song grace;
        grace.title = "Amazing Grace";
        for (const char *label : {"Verse 1", "Verse 2", "Verse 3", "Chorus"})
            grace.groups << Group{QUuid::createUuid(), label, {}, {QStringLiteral("%1 words").arg(label)}};
        QStringList texts;
        for (const SongSlide &slide : songSlides(grace, sequence))
            texts << slide.text;
        QCOMPARE(texts, (QStringList{"", "Verse 1 words", "Verse 2 words", "Chorus words", "", "Verse 3 words", ""}));
    }

    // P3 finding 8: credits come with stray spaces.
    void theSnapshotKeepsTrimmedCredits()
    {
        const Service s = serviceFromPlan(kType, plan(), items(), kImported).service;
        const QJsonObject snap = s.items[2].importSnapshot;
        QCOMPARE(snap["itemType"].toString(), QStringLiteral("song"));
        QCOMPARE(snap["position"].toInt(), 3);
        QCOMPARE(snap["key"].toString(), QStringLiteral("G"));
        const QJsonObject credits = snap["song"].toObject();
        QCOMPARE(credits["id"].toString(), QStringLiteral("s3"));
        QCOMPARE(credits["ccliNumber"].toString(), QStringLiteral("22025"));
        QCOMPARE(credits["author"].toString(), QStringLiteral("John Newton"));
        QCOMPARE(credits["copyright"].toString(), QStringLiteral("Public Domain"));
        const QJsonObject arrangement = snap["arrangement"].toObject();
        QCOMPARE(arrangement["id"].toString(), QStringLiteral("a3"));
        QCOMPARE(arrangement["sequence"].toArray().size(), 7);
        QCOMPARE(arrangement["bpm"].toInt(), 72);
        QVERIFY(!s.items[1].importSnapshot.contains("song"));
    }

    void aSongItemWithNoTitleUsesTheSongs()
    {
        PcoPlanItems p;
        p.items = {song("1", "Amazing Grace", 1, {"Verse 1"})};
        p.items[0].title = "";
        QCOMPARE(serviceFromPlan(kType, plan(), p, kImported).service.items[0].title, QStringLiteral("Amazing Grace"));
    }

    void skippedRecordsAreReported()
    {
        PcoPlanItems p = items();
        p.skipped = {"item 99 (no attributes)"};
        const PlanImport r = serviceFromPlan(kType, plan(), p, kImported);
        QCOMPARE(r.service.items.size(), 7);
        QCOMPARE(r.notes.size(), 1);
        QVERIFY(r.notes[0].contains("item 99"));
    }

    void anEmptyPlanImportsAnEmptyService()
    {
        const PlanImport r = serviceFromPlan(kType, plan(), {}, kImported);
        QVERIFY(r.service.items.isEmpty());
        QVERIFY(r.notes.isEmpty());
    }

    // Phase 2 acceptance: the last import is in the library, so it opens with no network.
    void theImportOpensFromTheLibraryAlone()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QUuid id;
        Service imported = serviceFromPlan(kType, plan(), items(), kImported).service;
        {
            auto lib = Library::open(dir.path());
            QVERIFY2(lib.library, qPrintable(lib.message));
            QVERIFY(ServiceStore(*lib.library).save(imported).ok);
            id = imported.id;
        }
        auto lib = Library::open(dir.path());
        QVERIFY2(lib.library, qPrintable(lib.message));
        const std::optional<Service> opened = ServiceStore(*lib.library).load(id);
        QVERIFY(opened);
        QCOMPARE(opened->items, imported.items);
        QCOMPARE(opened->pcoPlanUrl, imported.pcoPlanUrl);
        QCOMPARE(opened->lastImportedAt, kImported);
    }

    void thePickerListsUpcomingPlansFirst()
    {
        const auto at = [](const QString &id, QDate d) {
            PcoPlan p;
            p.id = id;
            p.sortDate = QDateTime(d, QTime(12, 0), QTimeZone::UTC);
            return p;
        };
        const QDate today(2027, 1, 10);
        const QList<PcoPlan> plans = plansForPicker(
            {at("lastWeek", today.addDays(-7)), at("nextWeek", today.addDays(7)), at("today", today),
             at("lastMonth", today.addDays(-28)), at("tomorrow", today.addDays(1))}, today);
        QStringList ids;
        for (const PcoPlan &p : plans)
            ids << p.id;
        QCOMPARE(ids, (QStringList{"today", "tomorrow", "nextWeek", "lastWeek", "lastMonth"}));
    }
};

QTEST_GUILESS_MAIN(PlanImportTest)
#include "tst_planimport.moc"
