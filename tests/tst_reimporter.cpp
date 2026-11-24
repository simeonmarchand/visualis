#include "library/library.h"
#include "library/servicelist.h"
#include "library/songlibrary.h"
#include "pco/pcoclient.h"
#include "pco/planimporter.h"
#include "service/servicestore.h"
#include "song/songstore.h"

#include "fakeplanningcenter.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QPointer>
#include <functional>
#include <utility>
#include <QtTest>

#include "otherprogram.h"

using namespace visualis;
using namespace visualis::testing;

// #56: updating a service from Planning Center, diff first, against a fake Planning Center
// and a real library.
class ReimporterTest : public QObject
{
    Q_OBJECT

    QNetworkAccessManager m_network;
    QTemporaryDir m_dir;
    int m_n = 0;

    std::unique_ptr<PcoClient> client(const QUrl &url) { return fakeClient(m_network, url); }
    static void serve(FakePlanningCenter &server) { serveFixtures(server); }

private slots:
    // #56: the diff first, saved only on Apply, keeping the operator's work.
    void reimportShowsTheDiffThenApplies()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        ServiceList services(&songs, nullptr);
        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        connect(&importer, &PlanImporter::imported, &services, [&](const QString &id) { services.openAdded(id); });
        connect(&importer, &PlanImporter::reimported, &services, [&](const QString &id) { services.openAdded(id); });
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        QCOMPARE(imported.size(), 1);
        const QString id = imported[0][0].toString();

        // The operator's week: a header renamed, a background, the countdown deleted.
        ServiceStore store(*songs.library());
        Service mine = *store.load(QUuid::fromString(id));
        const_cast<ServiceItem *>(byPco(mine, "9002"))->title = "Praise";
        const_cast<ServiceItem *>(byPco(mine, "9003"))->overrides = QJsonObject{{"background", "blue-motion"}};
        mine.items.removeAt(0);   // 9001, the countdown
        QVERIFY(store.save(mine).ok);
        services.openService(id);

        // The planner's week: Worship and Song One retitled, Song Two taken out.
        server.clear(kItems);
        server.onBody(kItems, fixtureWith("items_page1.json", [](QJsonArray &items) {
            retitle(items, "9002", "Worship Set");
            retitle(items, "9003", "Example Song One (Reprise)");
        }));
        server.onBody(kItems, fixtureWith("items_page2.json", [](QJsonArray &items) { items.removeAt(0); }));

        QSignalSpy ready(&importer, &PlanImporter::reimportChanged);
        importer.reimport(id);
        QVERIFY(importer.busy());   // reading; nothing waits on the network
        QVERIFY(idle(importer));
        QCOMPARE(importer.reimportServiceId(), id);
        QCOMPARE(changeKinds(importer), (QStringList{"keptEdit", "changed", "removed"}));
        QCOMPARE(importer.reimportSummary(), QStringLiteral("1 removed, 1 changed. Kept 1 of your edits."));
        QCOMPARE(*store.load(QUuid::fromString(id)), mine);   // nothing saved yet

        QSignalSpy reimported(&importer, &PlanImporter::reimported);
        QVERIFY(importer.applyReimport());
        QVERIFY(idle(importer));
        QCOMPARE(reimported.size(), 1);
        QVERIFY(importer.reimportServiceId().isEmpty());
        const Service s = *store.load(QUuid::fromString(id));
        QVERIFY(!byPco(s, "9001"));                                         // stays deleted
        QCOMPARE(byPco(s, "9002")->title, QStringLiteral("Praise"));        // the operator's
        QCOMPARE(byPco(s, "9003")->title, QStringLiteral("Example Song One (Reprise)"));
        QCOMPARE(byPco(s, "9003")->overrides, QJsonObject({{"background", "blue-motion"}}));
        QVERIFY(byPco(s, "9005")->pcoRemoved);                              // flagged, not deleted
        QVERIFY(importer.status().startsWith("Updated"));

        // The open service shows it: reloaded, so its next edit doesn't save over the merge.
        const QVariantList items = services.items();
        QCOMPARE(items.size(), 4);
        QVERIFY(std::any_of(items.cbegin(), items.cend(), [](const QVariant &v) { return v.toMap()["pcoRemoved"].toBool(); }));
        QVERIFY(services.openImported());

        // The same plan again (the fake server's queue is spent: serve both pages again).
        const QByteArray page1 = fixtureWith("items_page1.json", [](QJsonArray &items) {
            retitle(items, "9002", "Worship Set");
            retitle(items, "9003", "Example Song One (Reprise)");
        });
        server.clear(kItems);
        server.onBody(kItems, page1);
        server.onBody(kItems, fixtureWith("items_page2.json", [](QJsonArray &items) { items.removeAt(0); }));
        importer.reimport(id);
        QVERIFY(idle(importer));
        QCOMPARE(changeKinds(importer), QStringList());
        QCOMPARE(importer.reimportSummary(), QStringLiteral("Up to date with Planning Center."));
    }

    void aDeletedItemChangedInPlanningCenterCanBeAddedBack()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        const QString id = imported[0][0].toString();
        ServiceStore store(*songs.library());
        Service mine = *store.load(QUuid::fromString(id));
        mine.items.removeAt(0);   // 9001
        QVERIFY(store.save(mine).ok);

        server.clear(kItems);
        server.onBody(kItems, fixtureWith("items_page1.json", [](QJsonArray &items) { retitle(items, "9001", "Countdown (10 min)"); }));
        server.on(kItems, {"items_page2.json"});
        importer.reimport(id);
        QVERIFY(idle(importer));
        QCOMPARE(changeKinds(importer), QStringList{"changedButDeleted"});
        QCOMPARE(importer.reimportChanges()[0].toMap()["pcoItemId"].toString(), QStringLiteral("9001"));
        importer.setReimportAddBack({"9001"});   // the diff is worked out again: what's shown is what's applied
        QCOMPARE(changeKinds(importer), QStringList{"added"});
        QCOMPARE(importer.reimportChanges()[0].toMap()["pcoItemId"].toString(), QStringLiteral("9001"));
        QVERIFY(importer.applyReimport());
        QVERIFY(idle(importer));
        const Service s = *store.load(QUuid::fromString(id));
        QCOMPARE(s.items.first().title, QStringLiteral("Countdown (10 min)"));
        QVERIFY(s.removedPcoItems.isEmpty());
    }

    // A re-import that's ready to apply, for the lock tests: Worship renamed in Planning Center.
    QString readyReimport(SongLibrary &songs, FakePlanningCenter &server, PlanImporter &importer)
    {
        serve(server);
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        if (!idle(importer) || imported.isEmpty())
            qFatal("import failed: %s", qPrintable(importer.status()));
        server.clear(kItems);
        server.onBody(kItems, fixtureWith("items_page1.json", [](QJsonArray &items) { retitle(items, "9002", "Worship Set"); }));
        server.on(kItems, {"items_page2.json"});
        const QString id = imported[0][0].toString();
        importer.reimport(id);
        if (!idle(importer) || importer.reimportServiceId() != id)
            qFatal("re-import not offered: %s", qPrintable(importer.status()));
        return id;
    }

    // #76 for re-import: another program's lock never holds up the event loop.
    void aLockedLibraryDelaysApplyWithoutBlocking()
    {
        const QString folder = m_dir.filePath(QString::number(++m_n));
        SongLibrary songs;
        QVERIFY(songs.openLibrary(folder));
        FakePlanningCenter server;
        PlanImporter importer;
        const QString id = readyReimport(songs, server, importer);

        OtherProgram other(folder);   // stands in for another program
        QSqlQuery lock(other.database());
        QVERIFY(lock.exec("BEGIN IMMEDIATE"));
        QSignalSpy reimported(&importer, &PlanImporter::reimported);
        QElapsedTimer clock;
        clock.start();
        QVERIFY(importer.applyReimport());
        QVERIFY2(clock.elapsed() < Library::kBusyTimeoutMs / 2, "Apply waited on the lock");
        QVERIFY(importer.busy());
        QVERIFY(importer.status().contains("Waiting for the library"));
        QTimer::singleShot(300, this, [&] { QSqlQuery(other.database()).exec("ROLLBACK"); });
        QVERIFY(idle(importer));
        QCOMPARE(reimported.size(), 1);
        QCOMPARE(byPco(*ServiceStore(*songs.library()).load(QUuid::fromString(id)), "9002")->title, QStringLiteral("Worship Set"));
    }

    void aLibraryLockedTooLongLeavesTheServiceAlone()
    {
        const QString folder = m_dir.filePath(QString::number(++m_n));
        SongLibrary songs;
        QVERIFY(songs.openLibrary(folder));
        FakePlanningCenter server;
        PlanImporter importer;
        const QString id = readyReimport(songs, server, importer);
        const Service before = *ServiceStore(*songs.library()).load(QUuid::fromString(id));

        OtherProgram other(folder);
        QSqlQuery lock(other.database());
        QVERIFY(lock.exec("BEGIN IMMEDIATE"));
        importer.setLockWaitLimit(600);
        QSignalSpy reimported(&importer, &PlanImporter::reimported);
        QVERIFY(importer.applyReimport());
        QVERIFY(idle(importer));
        QCOMPARE(reimported.size(), 0);
        QVERIFY2(importer.status().contains("kept the library busy"), qPrintable(importer.status()));
        QVERIFY(importer.reimportServiceId().isEmpty());
        QVERIFY(QSqlQuery(other.database()).exec("ROLLBACK"));
        QCOMPARE(*ServiceStore(*songs.library()).load(QUuid::fromString(id)), before);
    }

    void openingAnotherLibraryCancelsTheUpdate()
    {
        const QString folder = m_dir.filePath(QString::number(++m_n));
        SongLibrary songs;
        QVERIFY(songs.openLibrary(folder));
        FakePlanningCenter server;
        PlanImporter importer;
        const QString id = readyReimport(songs, server, importer);
        const Service before = *ServiceStore(*songs.library()).load(QUuid::fromString(id));

        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        QSignalSpy reimported(&importer, &PlanImporter::reimported);
        importer.applyReimport();
        QVERIFY(idle(importer));
        QCOMPARE(reimported.size(), 0);
        QVERIFY(importer.status().contains("another library was opened"));

        auto first = Library::open(folder).library;
        QVERIFY(first);
        QCOMPARE(*ServiceStore(*first).load(QUuid::fromString(id)), before);   // untouched
    }

    // Review of #103 (H1): an item whose song record didn't come is left exactly as it was,
    // and the dialog says what couldn't be read.
    void anItemThatCouldntBeReadIsLeftAlone()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        const QString id = imported[0][0].toString();
        ServiceStore store(*songs.library());
        Service mine = *store.load(QUuid::fromString(id));
        ServiceItem *one = const_cast<ServiceItem *>(byPco(mine, "9003"));
        one->kind = Kind::Text;   // the operator's choice
        QVERIFY(store.save(mine).ok);
        const ServiceItem before = *byPco(mine, "9003");

        // Page 1 without its included records: 9003's song is a dangling link.
        QFile f(QStringLiteral(FIXTURES_DIR "/pco/items_page1.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonObject page = QJsonDocument::fromJson(f.readAll()).object();
        page["included"] = QJsonArray{};
        server.clear(kItems);
        server.onBody(kItems, QJsonDocument(page).toJson());
        server.on(kItems, {"items_page2.json"});
        importer.reimport(id);
        QVERIFY(idle(importer));
        QVERIFY(changeKinds(importer).contains("unreadable"));
        QVERIFY(!importer.reimportNotes().isEmpty());
        QVERIFY(importer.applyReimport());
        QVERIFY(idle(importer));
        QCOMPARE(*byPco(*store.load(QUuid::fromString(id)), "9003"), before);
    }

    // Review of #103 (M5): nothing to show can still be something to save.
    void anApplyWithNothingToShowStillSaves()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        const QString id = imported[0][0].toString();
        ServiceStore store(*songs.library());
        Service mine = *store.load(QUuid::fromString(id));
        mine.items.removeAt(0);   // 9001: remembered as deleted
        QVERIFY(store.save(mine).ok);
        QCOMPARE(store.load(QUuid::fromString(id))->removedPcoItems.size(), 1);

        server.clear(kItems);   // and Planning Center takes it out too
        server.onBody(kItems, fixtureWith("items_page1.json", [](QJsonArray &items) { items.removeAt(0); }));
        server.on(kItems, {"items_page2.json"});
        importer.reimport(id);
        QVERIFY(idle(importer));
        QVERIFY(importer.reimportChanges().isEmpty());
        QVERIFY(importer.reimportNeedsSave());
        QVERIFY(importer.applyReimport());
        QVERIFY(idle(importer));
        QVERIFY(store.load(QUuid::fromString(id))->removedPcoItems.isEmpty());
    }

    // Imported, with "Example Song One" in the library but not linked yet: the operator left
    // its placeholder alone or chose for it, as each test says.
    QString importedWithLibrarySong(SongLibrary &songs, FakePlanningCenter &server, PlanImporter &importer)
    {
        serve(server);
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        if (!idle(importer) || imported.isEmpty())
            qFatal("import failed: %s", qPrintable(importer.status()));
        if (!songs.createFromPaste("Example Song One", "[Verse 1]\nSynthetic words"))
            qFatal("no song");
        return imported[0][0].toString();
    }

    // Second review of #103 (H2): only what came from Planning Center is linked, and the
    // diff says so before anything is saved.
    void onlyPlanningCentersPlaceholdersAreLinkedAndShown()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        FakePlanningCenter server;
        PlanImporter importer;
        const QString id = importedWithLibrarySong(songs, server, importer);
        server.clear(kItems);
        server.on(kItems, {"items_page1.json", "items_page2.json"});
        importer.reimport(id);
        QVERIFY(idle(importer));
        QCOMPARE(changeKinds(importer), QStringList{"linked"});   // shown first
        QVERIFY(importer.reimportChanges()[0].toMap()["detail"].toString().contains("matched by title"));
        QVERIFY(importer.reimportNeedsSave());
        QVERIFY(importer.applyReimport());
        QVERIFY(idle(importer));
        QCOMPARE(byPco(*ServiceStore(*songs.library()).load(QUuid::fromString(id)), "9003")->kind, Kind::Song);

        // An item the merge leaves alone isn't linked either: here, one whose song record
        // didn't come, though the library has a song with its title.
        SongLibrary other;
        QVERIFY(other.openLibrary(m_dir.filePath(QString::number(++m_n))));
        FakePlanningCenter server2;
        PlanImporter importer2;
        const QString id2 = importedWithLibrarySong(other, server2, importer2);
        QFile f(QStringLiteral(FIXTURES_DIR "/pco/items_page1.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonObject page = QJsonDocument::fromJson(f.readAll()).object();
        page["included"] = QJsonArray{};
        server2.clear(kItems);
        server2.onBody(kItems, QJsonDocument(page).toJson());
        server2.on(kItems, {"items_page2.json"});
        importer2.reimport(id2);
        QVERIFY(idle(importer2));
        QCOMPARE(changeKinds(importer2), QStringList{"unreadable"});
        QVERIFY(importer2.applyReimport());
        QVERIFY(idle(importer2));
        QCOMPARE(byPco(*ServiceStore(*other.library()).load(QUuid::fromString(id2)), "9003")->kind, Kind::Placeholder);
    }

    // Second review of #103 (H3, M4): what's applied is what was shown, or nothing.
    void aServiceChangedWhileReviewingIsShownAgain()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        FakePlanningCenter server;
        PlanImporter importer;
        const QString id = readyReimport(songs, server, importer);   // Worship renamed in Planning Center
        QCOMPARE(changeKinds(importer), QStringList{"changed"});

        // Meanwhile the operator renames Worship themselves.
        ServiceStore store(*songs.library());
        Service mine = *store.load(QUuid::fromString(id));
        const_cast<ServiceItem *>(byPco(mine, "9002"))->title = "Praise";
        QVERIFY(store.save(mine).ok);
        const Service edited = *store.load(QUuid::fromString(id));

        QSignalSpy reimported(&importer, &PlanImporter::reimported);
        QVERIFY(importer.applyReimport());
        QVERIFY(idle(importer));
        QCOMPARE(reimported.size(), 0);
        QCOMPARE(*store.load(QUuid::fromString(id)), edited);   // nothing saved
        QVERIFY(!importer.reimportNotice().isEmpty());
        QCOMPARE(changeKinds(importer), QStringList{"keptEdit"});   // the new diff, for approval

        QVERIFY(importer.applyReimport());   // approved this time
        QVERIFY(idle(importer));
        QCOMPARE(reimported.size(), 1);
        QCOMPARE(byPco(*store.load(QUuid::fromString(id)), "9002")->title, QStringLiteral("Praise"));
    }

    // Second review of #103 (M5): only a song that's gone is skipped; any other failure to
    // write a link rolls the whole update back.
    void aLinkThatCantBeWrittenRollsTheUpdateBack()
    {
        const QString folder = m_dir.filePath(QString::number(++m_n));
        SongLibrary songs;
        QVERIFY(songs.openLibrary(folder));
        FakePlanningCenter server;
        PlanImporter importer;
        const QString id = importedWithLibrarySong(songs, server, importer);
        const Service before = *ServiceStore(*songs.library()).load(QUuid::fromString(id));
        server.clear(kItems);
        server.on(kItems, {"items_page1.json", "items_page2.json"});
        importer.reimport(id);
        QVERIFY(idle(importer));
        QCOMPARE(changeKinds(importer), QStringList{"linked"});

        QSqlQuery trigger(songs.library()->database());
        QVERIFY(trigger.exec("CREATE TEMP TRIGGER refuse_links BEFORE UPDATE OF pco_song_id ON song "
                             "BEGIN SELECT RAISE(ABORT, 'refused'); END"));
        QSignalSpy reimported(&importer, &PlanImporter::reimported);
        QVERIFY(importer.applyReimport());
        QVERIFY(idle(importer));
        QCOMPARE(reimported.size(), 0);
        QVERIFY2(importer.status().contains("refused"), qPrintable(importer.status()));
        QCOMPARE(*ServiceStore(*songs.library()).load(QUuid::fromString(id)), before);
        QVERIFY(trigger.exec("DROP TRIGGER refuse_links"));
    }

    void aCancelledReimportChangesNothing()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        const QString id = imported[0][0].toString();
        const Service before = *ServiceStore(*songs.library()).load(QUuid::fromString(id));

        server.clear(kItems);
        server.onBody(kItems, fixtureWith("items_page1.json", [](QJsonArray &items) { retitle(items, "9002", "Worship Set"); }));
        server.on(kItems, {"items_page2.json"});
        importer.reimport(id);
        QVERIFY(idle(importer));
        QVERIFY(!importer.reimportServiceId().isEmpty());
        importer.cancelReimport();
        QVERIFY(importer.reimportServiceId().isEmpty());
        QVERIFY(!importer.applyReimport());
        QCOMPARE(*ServiceStore(*songs.library()).load(QUuid::fromString(id)), before);

        // Cancelled while reading: the answer, when it comes, isn't offered.
        server.clear(kItems);
        server.onHeld(kItems, "items_page1.json");
        server.on(kItems, {"items_page2.json"});
        importer.reimport(id);
        QVERIFY(QTest::qWaitFor([&] { return server.requests > 0 && importer.busy(); }, 5000));
        importer.cancelReimport();
        server.release();
        QVERIFY(idle(importer));
        QVERIFY(importer.reimportServiceId().isEmpty());
    }

    // A read that comes back empty is far likelier a fault than a planner deleting
    // everything: merging it would flag every item removed.
    void anEmptyReadChangesNothing()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        const QString id = imported[0][0].toString();
        server.clear(kItems);
        server.onBody(kItems, R"({"data": [], "included": [], "links": {}})");
        importer.reimport(id);
        QVERIFY(idle(importer));
        QVERIFY(importer.reimportServiceId().isEmpty());
        QVERIFY(importer.status().contains("nothing was changed"));
    }

};

QTEST_MAIN(ReimporterTest)
#include "tst_reimporter.moc"
