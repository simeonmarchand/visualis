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

// #54: the picker's model against a fake Planning Center, saving into a real library.
class PlanImporterTest : public QObject
{
    Q_OBJECT

    QNetworkAccessManager m_network;
    QTemporaryDir m_dir;
    int m_n = 0;

    std::unique_ptr<PcoClient> client(const QUrl &url) { return fakeClient(m_network, url); }
    static void serve(FakePlanningCenter &server) { serveFixtures(server); }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void listsServiceTypesThenPlans()
    {
        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setClient(client(server.url()));

        importer.loadServiceTypes();
        QVERIFY(importer.busy());   // answers later; nothing waited on the network
        QVERIFY(idle(importer));
        QCOMPARE(importer.serviceTypes().size(), 2);
        QCOMPARE(importer.serviceTypes()[0].toMap()["name"].toString(), QStringLiteral("Sunday Morning"));

        importer.loadPlans("1001");
        QVERIFY(idle(importer));
        const QVariantList plans = importer.plans();
        QCOMPARE(plans.size(), 2);
        QCOMPARE(plans[0].toMap()["id"].toString(), QStringLiteral("5001"));
        QCOMPARE(plans[1].toMap()["label"].toString(), QStringLiteral("January 17, 2027 · Baptism Sunday"));
        QCOMPARE(plans[0].toMap()["url"].toString(), QStringLiteral("https://services.planningcenteronline.com/plans/5001"));
        QVERIFY(importer.status().isEmpty());
    }

    void importsAPlanIntoAServiceThatOpensOffline()
    {
        const QString folder = m_dir.filePath(QString::number(++m_n));
        QString id;
        {
            SongLibrary songs;   // closed before the library is reopened below
            QVERIFY(songs.openLibrary(folder));
            FakePlanningCenter server;
            serve(server);
            PlanImporter importer;
            importer.setSongLibrary(&songs);
            importer.setClient(client(server.url()));
            importer.loadServiceTypes();
            QVERIFY(idle(importer));

            QSignalSpy imported(&importer, &PlanImporter::imported);
            importer.importPlan("1001", "5001");
            QVERIFY(idle(importer));
            QVERIFY2(imported.size() == 1, qPrintable(importer.status()));
            id = imported[0][0].toString();
            QCOMPARE(imported[0][1].toString(), QStringLiteral("Sunday Morning"));
            QVERIFY(importer.status().contains("5 items"));
        }   // Planning Center is gone from here on

        SongLibrary reopened;
        QVERIFY(reopened.openLibrary(folder));
        const std::optional<Service> s = ServiceStore(*reopened.library()).load(QUuid::fromString(id));
        QVERIFY(s);
        QCOMPARE(s->pcoPlanId, QStringLiteral("5001"));
        QCOMPARE(s->pcoServiceTypeId, QStringLiteral("1001"));
        QCOMPARE(s->pcoPlanUrl, QStringLiteral("https://services.planningcenteronline.com/plans/5001"));
        QVERIFY(s->lastImportedAt.isValid());
        QStringList titles;
        QList<Kind> kinds;
        for (const ServiceItem &it : s->items) {
            titles << it.title;
            kinds << it.kind;
            QVERIFY(!it.pcoItemId.isEmpty());
            QVERIFY(!it.importSnapshot.isEmpty());
        }
        QCOMPARE(titles, (QStringList{"Pre-service countdown", "Worship", "Example Song One", "Acts 2:38", "Example Song Two"}));
        QCOMPARE(kinds, (QList<Kind>{Kind::Media, Kind::Header, Kind::Placeholder, Kind::Scripture, Kind::Placeholder}));
        QVERIFY(!s->items[2].customSequence.isEmpty());
    }

    // As main.cpp wires it: the imported service shows in the service list, open.
    void theImportedServiceOpensInTheServiceList()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        ServiceList services(&songs, nullptr);
        QVERIFY(services.services().isEmpty());
        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        connect(&importer, &PlanImporter::imported, &services, [&](const QString &id) { services.openAdded(id); });
        QSignalSpy listed(&services, &ServiceList::servicesChanged);

        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        QVERIFY(listed.size() >= 1);
        QCOMPARE(services.services().size(), 1);
        QCOMPARE(services.openTitle(), QStringLiteral("January 10, 2027"));   // no service types read: the date
        QCOMPARE(services.items().size(), 5);
        QCOMPARE(services.items()[3].toMap()["kind"].toString(), QStringLiteral("scripture"));
    }

    // Review of #72: A, then B, then A again. The first A answers last and must not win.
    void onlyTheLatestPlanListIsShown()
    {
        FakePlanningCenter server;
        server.onHeld(kPlans, "plans.json");   // the first answer for A waits for release()
        server.onBody(kPlans, R"({"data":[{"type":"Plan","id":"5009","attributes":{"dates":"March 7, 2027",
            "sort_date":"2027-03-07T10:00:00Z","items_count":1}}],"included":[],"meta":{}})");
        server.onBody("/services/v2/service_types/1002/plans", R"({"data":[],"included":[],"meta":{}})");
        PlanImporter importer;
        importer.setClient(client(server.url()));
        // Each request reaches the server before the next is sent, so they get the replies
        // queued for them in this order (they'd otherwise race on separate connections).
        const auto received = [&](int n) { return QTest::qWaitFor([&] { return server.requests >= n; }, 5000); };
        importer.loadPlans("1001");
        QVERIFY(received(1));
        importer.loadPlans("1002");
        QVERIFY(received(2));
        importer.loadPlans("1001");
        QVERIFY(received(3));
        // The latest request's answer is shown while the first is still outstanding...
        QVERIFY(QTest::qWaitFor([&] { return importer.plans().size() == 1; }, 5000));
        QVERIFY(importer.busy());
        // ...and the first, answering last, doesn't replace it.
        server.release();
        QVERIFY(idle(importer));
        const QVariantList plans = importer.plans();
        QCOMPARE(plans.size(), 1);
        QCOMPARE(plans[0].toMap()["id"].toString(), QStringLiteral("5009"));
    }

    // Review of #72: a plan whose items all fail to read isn't saved, and can be tried again.
    void aPlanWithNoReadableItemsIsNotSavedAndCanBeRetried()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        FakePlanningCenter server;
        server.on(kPlan, {"plan.json"});
        server.onBody(kItems, R"({"data":[{"type":"Item","attributes":{"title":"No id","item_type":"item","sequence":1}}],
            "included":[],"meta":{}})");
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        QCOMPARE(imported.size(), 0);
        QVERIFY2(importer.status().contains("Nothing was imported"), qPrintable(importer.status()));
        QVERIFY(ServiceStore(*songs.library()).list().isEmpty());

        server.clear(kItems);   // Planning Center is fine again
        server.on(kItems, {"items_page1.json", "items_page2.json"});
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        QCOMPARE(imported.size(), 1);
        QCOMPARE(ServiceStore(*songs.library()).list().size(), 1);
    }

    void anEmptyPlanIsNotSaved()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        FakePlanningCenter server;
        server.on(kPlan, {"plan.json"});
        server.onBody(kItems, R"({"data":[],"included":[],"meta":{}})");
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        QVERIFY(importer.status().contains("Nothing was imported"));
        QVERIFY(ServiceStore(*songs.library()).list().isEmpty());
    }

    void aPlanIsNotImportedTwice()
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
        QCOMPARE(imported.size(), 1);

        // Picked again: it's re-imported into its service (#56), not imported twice.
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        QCOMPARE(imported.size(), 1);
        QCOMPARE(importer.reimportServiceId(), imported[0][0].toString());
        QCOMPARE(ServiceStore(*songs.library()).list().size(), 1);
    }

    // #76: another program holding the library's write lock never holds up the event loop.
    // The import waits on a timer and saves once the lock is free.
    void aLockedLibraryDelaysTheSaveWithoutBlocking()
    {
        const QString folder = m_dir.filePath(QString::number(++m_n));
        SongLibrary songs;
        QVERIFY(songs.openLibrary(folder));
        OtherProgram other(folder);
        QSqlQuery lock(other.database());
        QVERIFY(lock.exec("BEGIN IMMEDIATE"));
        bool held = true;

        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        int ticksWhileHeld = 0;
        qint64 longestGap = 0;
        QElapsedTimer sinceTick;
        QTimer ticker;
        ticker.setInterval(10);
        connect(&ticker, &QTimer::timeout, this, [&] {
            ticksWhileHeld += held ? 1 : 0;
            longestGap = qMax(longestGap, sinceTick.restart());
        });
        sinceTick.start();
        ticker.start();

        importer.importPlan("1001", "5001");
        QTRY_VERIFY(importer.status().contains("Waiting for the library"));
        QVERIFY(importer.busy());
        // Released by a timer on this thread: if the save blocked, it couldn't run.
        QTimer::singleShot(500, this, [&] {
            QSqlQuery(other.database()).exec("ROLLBACK");
            held = false;
        });
        QVERIFY(idle(importer));
        QVERIFY2(ticksWhileHeld >= 10, qPrintable(QString("only %1 ticks while locked").arg(ticksWhileHeld)));
        // A save that waited on SQLite's busy timeout would stall for all of it.
        QVERIFY2(longestGap < Library::kBusyTimeoutMs / 2, qPrintable(QString("stalled %1 ms").arg(longestGap)));
        QCOMPARE(imported.size(), 1);
        QVERIFY(importer.status().startsWith("Imported"));
        QCOMPARE(ServiceStore(*songs.library()).list().size(), 1);
    }

    void aLibraryLockedTooLongFailsClearlyAndCanBeRetried()
    {
        const QString folder = m_dir.filePath(QString::number(++m_n));
        SongLibrary songs;
        QVERIFY(songs.openLibrary(folder));
        OtherProgram other(folder);
        QSqlQuery lock(other.database());
        QVERIFY(lock.exec("BEGIN IMMEDIATE"));

        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        importer.setLockWaitLimit(600);
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        QCOMPARE(imported.size(), 0);
        QVERIFY2(importer.status().contains("another program kept the library busy"), qPrintable(importer.status()));

        QVERIFY(QSqlQuery(other.database()).exec("ROLLBACK"));
        QVERIFY(ServiceStore(*songs.library()).list().isEmpty());
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        QCOMPARE(imported.size(), 1);
    }

    // #88 review: an import is for the library that was open when it started.
    void switchingLibrariesWhileWaitingOnTheLockCancelsTheImport()
    {
        const QString first = m_dir.filePath(QString::number(++m_n));
        const QString second = m_dir.filePath(QString::number(++m_n));
        SongLibrary songs;
        QVERIFY(songs.openLibrary(first));
        OtherProgram other(first);
        QSqlQuery lock(other.database());
        QVERIFY(lock.exec("BEGIN IMMEDIATE"));

        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        QTRY_VERIFY(importer.status().contains("Waiting for the library"));
        // The next retry would succeed here. (Closing the locked first library takes the
        // 5 s busy timeout, in ~Library's checkpoint: reported separately from #88.)
        QVERIFY(songs.openLibrary(second));
        QVERIFY(idle(importer));
        QCOMPARE(imported.size(), 0);
        QVERIFY2(importer.status().contains("another library was opened"), qPrintable(importer.status()));
        QVERIFY(ServiceStore(*songs.library()).list().isEmpty());

        QVERIFY(QSqlQuery(other.database()).exec("ROLLBACK"));
        auto reopened = Library::open(first);   // nor in the first, now that nothing has it open
        QVERIFY(reopened.library);
        QVERIFY(ServiceStore(*reopened.library).list().isEmpty());
    }

    void switchingLibrariesDuringTheReadsCancelsTheImport()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        FakePlanningCenter server;
        serve(server);
        server.clear(kPlan);
        server.onHeld(kPlan, "plan.json");
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        QTRY_VERIFY(server.requests > 0);
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        server.release();
        QVERIFY(idle(importer));
        QCOMPARE(imported.size(), 0);
        QVERIFY(importer.status().contains("another library was opened"));
        QVERIFY(ServiceStore(*songs.library()).list().isEmpty());
    }

    // #88 review: no attempt after the deadline, even if the lock is free by then.
    void nothingIsSavedAfterTheDeadline()
    {
        const QString folder = m_dir.filePath(QString::number(++m_n));
        SongLibrary songs;
        QVERIFY(songs.openLibrary(folder));
        OtherProgram other(folder);
        QSqlQuery lock(other.database());
        QVERIFY(lock.exec("BEGIN IMMEDIATE"));

        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        importer.setLockWaitLimit(PlanImporter::kLockRetryMs + 50);   // one retry fits; the next is cut short
        QSignalSpy imported(&importer, &PlanImporter::imported);
        QTimer release;   // frees the lock after the deadline but before an uncapped retry
        release.setSingleShot(true);
        connect(&release, &QTimer::timeout, this, [&] { QSqlQuery(other.database()).exec("ROLLBACK"); });
        connect(&importer, &PlanImporter::statusChanged, this, [&] {
            if (importer.status().contains("Waiting") && !release.isActive())
                release.start(PlanImporter::kLockRetryMs + 150);
        });
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        QTest::qWait(2 * PlanImporter::kLockRetryMs);   // any stray retry would have run by now
        QCOMPARE(imported.size(), 0);
        QVERIFY2(importer.status().contains("another program kept the library busy"), qPrintable(importer.status()));
        QVERIFY(ServiceStore(*songs.library()).list().isEmpty());
    }

    // Review of #103 (M6): the songs' Planning Center links are saved, with the service.
    void importingRemembersEachSongsPlanningCenterLink()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        QVERIFY(songs.createFromPaste("Example Song One", "[Verse 1]\nSynthetic words"));
        const QUuid one = QUuid::fromString(songs.selectedId());
        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        QCOMPARE(imported.size(), 1);
        QCOMPARE(SongStore(*songs.library()).load(one)->pcoSongId, QStringLiteral("7001"));
        QVERIFY(importer.status().contains("1 by title"));
    }

    void aFailedReadLeavesTheLibraryAlone()
    {
        SongLibrary songs;
        QVERIFY(songs.openLibrary(m_dir.filePath(QString::number(++m_n))));
        FakePlanningCenter server;
        server.on(kPlan, {"plan.json"});   // the items 404
        PlanImporter importer;
        importer.setSongLibrary(&songs);
        importer.setClient(client(server.url()));
        QSignalSpy imported(&importer, &PlanImporter::imported);
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        QCOMPARE(imported.size(), 0);
        QVERIFY(!importer.status().isEmpty());
        QVERIFY(ServiceStore(*songs.library()).list().isEmpty());
    }

    void offlineSaysSo()
    {
        PlanImporter importer;
        importer.setClient(client(QUrl("http://127.0.0.1:1/services/v2/")));   // nothing listens there
        importer.loadServiceTypes();
        QVERIFY(idle(importer));
        QVERIFY(importer.serviceTypes().isEmpty());
        QVERIFY(!importer.status().isEmpty());
    }

    void withNoLibraryNothingIsRead()
    {
        FakePlanningCenter server;
        serve(server);
        PlanImporter importer;
        importer.setClient(client(server.url()));
        importer.importPlan("1001", "5001");
        QVERIFY(!importer.busy());
        QCOMPARE(server.requests, 0);
        QVERIFY(importer.status().contains("library"));
    }

    void signedOutSaysSo()
    {
        PlanImporter importer;   // no Planning Center connection and no client
        importer.loadServiceTypes();
        QVERIFY(!importer.busy());
        QVERIFY(importer.status().contains("Sign in"));
    }
};

QTEST_MAIN(PlanImporterTest)
#include "tst_planimporter.moc"
