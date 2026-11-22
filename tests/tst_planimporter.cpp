#include "library/library.h"
#include "library/servicelist.h"
#include "library/songlibrary.h"
#include "pco/pcoclient.h"
#include "pco/planimporter.h"
#include "service/servicestore.h"

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

using namespace visualis;
using Kind = ServiceItem::Kind;

namespace {

// A stand-in for Planning Center on localhost: each path (query ignored) answers from a
// queue of bodies, the last one repeating; 404 otherwise. tst_pcoclient has the full one.
class FakePlanningCenter : public QObject
{
public:
    FakePlanningCenter()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, s, [this, s] { answer(s); });
        });
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/services/v2/").arg(m_server.serverPort())); }
    void on(const QByteArray &path, const QStringList &fixtures, int delayMs = 0)
    {
        for (const QString &name : fixtures) {
            QFile f(QStringLiteral(FIXTURES_DIR "/pco/") + name);
            if (!f.open(QIODevice::ReadOnly))
                qFatal("missing fixture %s", qPrintable(name));
            onBody(path, f.readAll(), delayMs);
        }
    }
    // A reply written here, answered after `delayMs`.
    void onBody(const QByteArray &path, const QByteArray &body, int delayMs = 0)
    {
        m_routes[path] << Reply{QByteArray(body).replace("{{BASE}}", url().toString().toUtf8()), delayMs};
    }
    void clear(const QByteArray &path) { m_routes.remove(path); }
    // A reply held back until release(), so a test decides the order answers arrive in.
    void onHeld(const QByteArray &path, const QString &fixture)
    {
        on(path, {fixture});
        m_routes[path].last().held = true;
    }
    void release()
    {
        for (const auto &send : std::exchange(m_held, {}))
            send();
    }
    int requests = 0;

private:
    void answer(QTcpSocket *s)
    {
        const QByteArray head = s->readAll();
        if (!head.contains("\r\n\r\n"))
            return;
        ++requests;
        QByteArray path = head.split(' ').value(1);
        path = path.left(path.indexOf('?') < 0 ? path.size() : path.indexOf('?'));
        QByteArray status = "200 OK";
        Reply reply{R"({"errors":[{"status":"404"}]})", 0};
        if (auto it = m_routes.find(path); it != m_routes.end() && !it->isEmpty())
            reply = it->size() > 1 ? it->takeFirst() : it->first();
        else
            status = "404 Not Found";
        const QPointer<QTcpSocket> socket(s);
        auto send = [socket, status, body = reply.body] {
            if (!socket)
                return;
            socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        };
        if (reply.held)
            m_held << send;
        else
            QTimer::singleShot(reply.delayMs, s, send);
    }

    struct Reply {
        QByteArray body;
        int delayMs = 0;
        bool held = false;
    };
    QList<std::function<void()>> m_held;
    QTcpServer m_server;
    QHash<QByteArray, QList<Reply>> m_routes;
};

class FixedToken : public PcoAuthorizer
{
public:
    QByteArray authorization() const override { return "Basic dGVzdDp0ZXN0"; }
};

constexpr char kTypes[] = "/services/v2/service_types";
constexpr char kPlans[] = "/services/v2/service_types/1001/plans";
constexpr char kPlan[] = "/services/v2/service_types/1001/plans/5001";
constexpr char kItems[] = "/services/v2/service_types/1001/plans/5001/items";

// A fixture with its items edited: as if the planner changed the plan during the week.
QByteArray fixtureWith(const QString &name, const std::function<void(QJsonArray &)> &edit)
{
    QFile f(QStringLiteral(FIXTURES_DIR "/pco/") + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QJsonArray data = o.value("data").toArray();
    edit(data);
    o["data"] = data;
    return QJsonDocument(o).toJson();
}

void retitle(QJsonArray &items, const QString &id, const QString &title)
{
    for (qsizetype i = 0; i < items.size(); ++i) {
        QJsonObject it = items[i].toObject();
        if (it.value("id").toString() == id) {
            QJsonObject a = it.value("attributes").toObject();
            a["title"] = title;
            it["attributes"] = a;
            items[i] = it;
        }
    }
}

const ServiceItem *byPco(const Service &s, const QString &id)
{
    for (const ServiceItem &it : s.items) {
        if (it.pcoItemId == id)
            return &it;
    }
    return nullptr;
}

QStringList changeKinds(const PlanImporter &importer)
{
    QStringList out;
    for (const QVariant &c : importer.reimportChanges())
        out << c.toMap().value("kind").toString();
    return out;
}

} // namespace

// #54: the picker's model against a fake Planning Center, saving into a real library.
class PlanImporterTest : public QObject
{
    Q_OBJECT

    QNetworkAccessManager m_network;
    QTemporaryDir m_dir;
    int m_n = 0;

    std::unique_ptr<PcoClient> client(const QUrl &url)
    {
        auto c = std::make_unique<PcoClient>(&m_network, url, [] { return std::make_unique<FixedToken>(); });
        c->setTransferTimeoutMs(2000);
        return c;
    }

    void serve(FakePlanningCenter &server)
    {
        server.on(kTypes, {"service_types.json"});
        server.on(kPlans, {"plans.json"});
        server.on(kPlan, {"plan.json"});
        server.on(kItems, {"items_page1.json", "items_page2.json"});
    }

    static bool idle(PlanImporter &importer) { return QTest::qWaitFor([&] { return !importer.busy(); }, 5000); }

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
        SongLibrary songs;
        const QString folder = m_dir.filePath(QString::number(++m_n));
        QVERIFY(songs.openLibrary(folder));
        QString id;
        {
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
        auto other = Library::open(folder).library;   // stands in for another program
        QVERIFY(other);
        QSqlQuery lock(other->database());
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
            QSqlQuery(other->database()).exec("ROLLBACK");
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
        auto other = Library::open(folder).library;
        QSqlQuery lock(other->database());
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

        QVERIFY(QSqlQuery(other->database()).exec("ROLLBACK"));
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
        auto other = Library::open(first).library;
        QSqlQuery lock(other->database());
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

        QVERIFY(QSqlQuery(other->database()).exec("ROLLBACK"));
        QVERIFY(ServiceStore(*other).list().isEmpty());   // nor in the first
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
        auto other = Library::open(folder).library;
        QSqlQuery lock(other->database());
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
        connect(&release, &QTimer::timeout, this, [&] { QSqlQuery(other->database()).exec("ROLLBACK"); });
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
        QVERIFY(importer.applyReimport({"9001"}));
        const Service s = *store.load(QUuid::fromString(id));
        QCOMPARE(s.items.first().title, QStringLiteral("Countdown (10 min)"));
        QVERIFY(s.removedPcoItems.isEmpty());
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
