#include "library/servicelist.h"
#include "library/songlibrary.h"
#include "pco/pcoclient.h"
#include "pco/planimporter.h"
#include "service/servicestore.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
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
    void on(const QByteArray &path, const QStringList &fixtures)
    {
        for (const QString &name : fixtures) {
            QFile f(QStringLiteral(FIXTURES_DIR "/pco/") + name);
            if (!f.open(QIODevice::ReadOnly))
                qFatal("missing fixture %s", qPrintable(name));
            m_routes[path] << f.readAll().replace("{{BASE}}", url().toString().toUtf8());
        }
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
        QByteArray status = "200 OK", body;
        if (auto it = m_routes.find(path); it != m_routes.end() && !it->isEmpty())
            body = it->size() > 1 ? it->takeFirst() : it->first();
        else
            status = "404 Not Found", body = R"({"errors":[{"status":"404"}]})";
        s->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                 + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        s->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QByteArray, QList<QByteArray>> m_routes;
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

        const int before = server.requests;
        importer.importPlan("1001", "5001");
        QVERIFY(idle(importer));
        QCOMPARE(imported.size(), 1);
        QCOMPARE(server.requests, before);   // refused before asking Planning Center
        QVERIFY(importer.status().contains("already in the library"));
        QCOMPARE(ServiceStore(*songs.library()).list().size(), 1);
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
