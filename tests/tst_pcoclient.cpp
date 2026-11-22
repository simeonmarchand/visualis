#include "pco/pcoclient.h"

#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace visualis;

namespace {

struct Reply
{
    int status = 200;
    QByteArray body;
    QList<QPair<QByteArray, QByteArray>> headers;
    bool hang = false;   // accept the request and never answer
};

// A stand-in for Planning Center on localhost. Answers each path (query ignored) from a
// queue of replies, the last one repeating; 404 for anything else. Records what it was sent.
class FakePlanningCenter : public QObject
{
public:
    struct Request
    {
        QByteArray target;   // path and query
        QByteArray authorization;
        qint64 atMs = 0;
    };
    QList<Request> requests;

    FakePlanningCenter()
    {
        m_clock.start();
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, s, [this, s] { onRead(s); });
        });
    }

    QUrl url() const { return QUrl(base()); }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/services/v2/").arg(m_server.serverPort()); }

    void on(const QByteArray &path, QList<Reply> replies) { m_routes[path] = std::move(replies); }
    void onFixture(const QByteArray &path, const QString &fixtureName, QList<QPair<QByteArray, QByteArray>> headers = {})
    {
        on(path, {{200, fixture(fixtureName), std::move(headers)}});
    }
    QByteArray fixture(const QString &name) const
    {
        QFile f(QStringLiteral(FIXTURES_DIR "/pco/") + name);
        if (!f.open(QIODevice::ReadOnly))
            qFatal("missing fixture %s", qPrintable(name));
        return f.readAll().replace("{{BASE}}", base().toUtf8());
    }

private:
    void onRead(QTcpSocket *s)
    {
        QByteArray buffer = s->property("buffer").toByteArray() + s->readAll();
        const qsizetype end = buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            s->setProperty("buffer", buffer);
            return;
        }
        s->setProperty("buffer", buffer.mid(end + 4));
        const QList<QByteArray> lines = buffer.left(end).split('\n');
        Request req;
        req.target = lines.value(0).split(' ').value(1);
        req.atMs = m_clock.elapsed();
        for (const QByteArray &line : lines) {
            if (line.toLower().startsWith("authorization:"))
                req.authorization = line.mid(line.indexOf(':') + 1).trimmed();
        }
        requests << req;

        const QByteArray path = req.target.left(req.target.indexOf('?') < 0 ? req.target.size() : req.target.indexOf('?'));
        Reply reply{404, R"({"errors":[{"status":"404"}]})", {}, false};
        if (auto it = m_routes.find(path); it != m_routes.end() && !it->isEmpty())
            reply = it->size() > 1 ? it->takeFirst() : it->first();
        if (reply.hang)
            return;
        QByteArray head = "HTTP/1.1 " + QByteArray::number(reply.status) + " X\r\nContent-Type: application/json\r\n";
        for (const auto &[k, v] : reply.headers)
            head += k + ": " + v + "\r\n";
        head += "Content-Length: " + QByteArray::number(reply.body.size()) + "\r\nConnection: close\r\n\r\n";
        s->write(head + reply.body);
        s->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QByteArray, QList<Reply>> m_routes;
    QElapsedTimer m_clock;
};

class FixedToken : public PcoAuthorizer
{
public:
    QByteArray authorization() const override { return "Basic dGVzdDp0ZXN0"; }
};

constexpr char kItems[] = "/services/v2/service_types/1001/plans/5001/items";

// Waits for a callback's result without blocking the event loop (the client never does).
template <typename T>
struct Waiter
{
    std::optional<PcoResult<T>> result;
    PcoClient::Done<T> done()
    {
        return [this](PcoResult<T> r) { result = std::move(r); };
    }
    bool wait(int ms = 5000)
    {
        return QTest::qWaitFor([this] { return result.has_value(); }, ms);
    }
};

} // namespace

// #53: the client against a fake Planning Center: paging, rate limits, and every failure.
class PcoClientTest : public QObject
{
    Q_OBJECT

    std::unique_ptr<PcoClient> client(const FakePlanningCenter &server)
    {
        return std::make_unique<PcoClient>(&m_network, server.url(), [] { return std::make_unique<FixedToken>(); });
    }

private slots:
    void readsServiceTypesWithTheToken()
    {
        FakePlanningCenter server;
        server.onFixture("/services/v2/service_types", "service_types.json");
        auto pco = client(server);
        Waiter<QList<PcoServiceType>> w;
        pco->serviceTypes(w.done());
        QVERIFY(!w.result);   // asynchronous: nothing has happened yet
        QVERIFY(w.wait());
        QVERIFY(w.result->ok());
        QCOMPARE(w.result->value.size(), 2);
        QCOMPARE(w.result->value[0].name, QStringLiteral("Sunday Morning"));
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests[0].authorization, QByteArray("Basic dGVzdDp0ZXN0"));
        QVERIFY(server.requests[0].target.contains("per_page=100"));
    }

    void readsFuturePlansSoonestFirst()
    {
        FakePlanningCenter server;
        server.onFixture("/services/v2/service_types/1001/plans", "plans.json");
        auto pco = client(server);
        Waiter<QList<PcoPlan>> w;
        pco->futurePlans("1001", w.done());
        QVERIFY(w.wait());
        QVERIFY(w.result->ok());
        QCOMPARE(w.result->value.size(), 2);
        QCOMPARE(w.result->value[0].serviceTypeId, QStringLiteral("1001"));
        const QByteArray target = server.requests[0].target;
        QVERIFY2(target.contains("filter=future") && target.contains("order=sort_date"), target.constData());
    }

    void readsOnePlanWithItsLink()
    {
        FakePlanningCenter server;
        server.onFixture("/services/v2/service_types/1001/plans/5001", "plan.json");
        auto pco = client(server);
        Waiter<PcoPlan> w;
        pco->plan("1001", "5001", w.done());
        QVERIFY(w.wait());
        QVERIFY(w.result->ok());
        QCOMPARE(w.result->value.planningCenterUrl, QUrl("https://services.planningcenteronline.com/plans/5001"));
    }

    void followsPagesForPlanItems()
    {
        FakePlanningCenter server;
        server.on(kItems, {{200, server.fixture("items_page1.json"), {}}, {200, server.fixture("items_page2.json"), {}}});
        auto pco = client(server);
        Waiter<PcoPlanItems> w;
        pco->planItems("1001", "5001", w.done());
        QVERIFY(w.wait());
        QVERIFY(w.result->ok());
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(server.requests[0].target.contains("include=song%2Carrangement%2Ckey")
                || server.requests[0].target.contains("include=song,arrangement,key"));
        QVERIFY(server.requests[1].target.contains("offset=3"));
        const QList<PcoItem> &items = w.result->value.items;
        QCOMPARE(items.size(), 5);
        QCOMPARE(items[4].song->title, QStringLiteral("Example Song Two"));   // from page two's included
        QCOMPARE(items[2].song->ccliNumber, QStringLiteral("1234567"));
    }

    void waitsOutA429ThenSucceeds()
    {
        FakePlanningCenter server;
        server.on("/services/v2/service_types",
                  {{429, R"({"errors":[{"status":"429"}]})", {{"Retry-After", "1"}}},
                   {200, server.fixture("service_types.json"), {}}});
        auto pco = client(server);
        Waiter<QList<PcoServiceType>> w;
        pco->serviceTypes(w.done());
        QVERIFY(w.wait(10000));
        QVERIFY2(w.result->ok(), qPrintable(w.result->error.message));
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(server.requests[1].atMs - server.requests[0].atMs >= 900);   // Retry-After was honored
    }

    void givesUpAfterRepeated429s()
    {
        FakePlanningCenter server;
        server.on("/services/v2/service_types", {{429, "{}", {{"Retry-After", "0"}}}});
        auto pco = client(server);
        Waiter<QList<PcoServiceType>> w;
        pco->serviceTypes(w.done());
        QVERIFY(w.wait());
        QCOMPARE(w.result->error.kind, PcoError::RateLimited);
        QCOMPARE(server.requests.size(), PcoClient::kMaxRateLimitRetries + 1);
    }

    void doesntGuessA429WaitWithoutHeaders()
    {
        FakePlanningCenter server;
        server.on("/services/v2/service_types", {{429, "{}", {}}});
        auto pco = client(server);
        Waiter<QList<PcoServiceType>> w;
        pco->serviceTypes(w.done());
        QVERIFY(w.wait());
        QCOMPARE(w.result->error.kind, PcoError::RateLimited);
        QCOMPARE(server.requests.size(), 1);
    }

    void doesntWaitLongerThanItAccepts()
    {
        FakePlanningCenter server;
        server.on("/services/v2/service_types", {{429, "{}", {{"Retry-After", "3600"}}}});
        auto pco = client(server);
        Waiter<QList<PcoServiceType>> w;
        pco->serviceTypes(w.done());
        QVERIFY(w.wait());
        QCOMPARE(w.result->error.kind, PcoError::RateLimited);
        QCOMPARE(server.requests.size(), 1);
    }

    void pacesItselfWhenTheLimitIsUsedUp()
    {
        FakePlanningCenter server;
        const QList<QPair<QByteArray, QByteArray>> usedUp{{"X-PCO-API-Request-Rate-Count", "100"},
                                                          {"X-PCO-API-Request-Rate-Limit", "100"},
                                                          {"X-PCO-API-Request-Rate-Period", "1"}};
        server.on(kItems, {{200, server.fixture("items_page1.json"), usedUp},
                           {200, server.fixture("items_page2.json"), {}}});
        auto pco = client(server);
        Waiter<PcoPlanItems> w;
        pco->planItems("1001", "5001", w.done());
        QVERIFY(w.wait(10000));
        QVERIFY(w.result->ok());
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(server.requests[1].atMs - server.requests[0].atMs >= 900);
    }

    void httpFailures_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("kind");
        QTest::newRow("401") << 401 << int(PcoError::Unauthorized);
        QTest::newRow("403") << 403 << int(PcoError::Forbidden);
        QTest::newRow("404") << 404 << int(PcoError::NotFound);
        QTest::newRow("500") << 500 << int(PcoError::Server);
        QTest::newRow("502") << 502 << int(PcoError::Server);
    }
    void httpFailures()
    {
        QFETCH(int, status);
        QFETCH(int, kind);
        FakePlanningCenter server;
        server.on(kItems, {{status, R"({"errors":[]})", {}}});
        auto pco = client(server);
        Waiter<PcoPlanItems> w;
        pco->planItems("1001", "5001", w.done());
        QVERIFY(w.wait());
        QCOMPARE(int(w.result->error.kind), kind);
        QCOMPARE(w.result->error.httpStatus, status);
        QVERIFY(!w.result->error.message.isEmpty());
        QVERIFY(w.result->value.items.isEmpty());
    }

    void aFailureOnALaterPageFailsTheWhole()
    {
        FakePlanningCenter server;
        server.on(kItems, {{200, server.fixture("items_page1.json"), {}}, {500, "oops", {}}});
        auto pco = client(server);
        Waiter<PcoPlanItems> w;
        pco->planItems("1001", "5001", w.done());
        QVERIFY(w.wait());
        QCOMPARE(w.result->error.kind, PcoError::Server);
        QVERIFY(w.result->value.items.isEmpty());   // never half a plan
    }

    void malformedJson()
    {
        FakePlanningCenter server;
        server.on(kItems, {{200, "<html>maintenance</html>", {}}});
        auto pco = client(server);
        Waiter<PcoPlanItems> w;
        pco->planItems("1001", "5001", w.done());
        QVERIFY(w.wait());
        QCOMPARE(w.result->error.kind, PcoError::Malformed);
    }

    void neverSendsTheTokenToAnotherHost()
    {
        FakePlanningCenter server;
        QByteArray page = server.fixture("items_page1.json");
        page.replace(server.base().toUtf8() + "service_types/1001/plans/5001/items?include",
                     "https://elsewhere.example/steal?include");
        server.on(kItems, {{200, page, {}}});
        auto pco = client(server);
        Waiter<PcoPlanItems> w;
        pco->planItems("1001", "5001", w.done());
        QVERIFY(w.wait());
        QCOMPARE(w.result->error.kind, PcoError::Malformed);
        QCOMPARE(server.requests.size(), 1);
    }

    void stopsANextLinkThatLoops()
    {
        FakePlanningCenter server;
        QByteArray page = server.fixture("items_page1.json");
        page.replace("offset=3&per_page=3", "per_page=3");   // next == self
        server.on(kItems, {{200, page, {}}});
        auto pco = client(server);
        Waiter<PcoPlanItems> w;
        pco->planItems("1001", "5001", w.done());
        QVERIFY(w.wait());
        QCOMPARE(w.result->error.kind, PcoError::Malformed);
    }

    void offline()
    {
        auto pco = std::make_unique<PcoClient>(&m_network, QUrl("http://127.0.0.1:1/services/v2/"),
                                               [] { return std::make_unique<FixedToken>(); });
        Waiter<QList<PcoServiceType>> w;
        pco->serviceTypes(w.done());
        QVERIFY(w.wait(20000));
        QCOMPARE(w.result->error.kind, PcoError::Offline);
        QVERIFY(w.result->error.message.contains("internet"));
    }

    void aServerThatNeverAnswersTimesOut()
    {
        FakePlanningCenter server;
        Reply hang;
        hang.hang = true;
        server.on("/services/v2/service_types", {hang});
        auto pco = client(server);
        pco->setTransferTimeoutMs(300);
        Waiter<QList<PcoServiceType>> w;
        pco->serviceTypes(w.done());
        QVERIFY(w.wait());
        QCOMPARE(w.result->error.kind, PcoError::Offline);
    }

    void signedOutSendsNothing()
    {
        FakePlanningCenter server;
        PcoClient pco(&m_network, server.url(), [] { return std::unique_ptr<PcoAuthorizer>(); });
        Waiter<QList<PcoServiceType>> w;
        pco.serviceTypes(w.done());
        QVERIFY(!w.result);
        QVERIFY(w.wait());
        QCOMPARE(w.result->error.kind, PcoError::NotSignedIn);
        QCOMPARE(server.requests.size(), 0);
    }

    void noCallbackAfterTheClientIsGone()
    {
        FakePlanningCenter server;
        Reply hang;
        hang.hang = true;
        server.on("/services/v2/service_types", {hang});
        bool called = false;
        {
            auto pco = client(server);
            pco->serviceTypes([&called](auto) { called = true; });
            QTRY_COMPARE(server.requests.size(), 1);
        }
        QTest::qWait(200);
        QVERIFY(!called);
    }

    // Against the real API, only when a token is in the environment (never in CI). Walks
    // service types → the next plan → its items and checks shape, not content.
    void liveSandbox()
    {
        const PcoCredentials creds{qEnvironmentVariable("PCO_APP_ID"), qEnvironmentVariable("PCO_SECRET")};
        if (!creds.isValid())
            QSKIP("set PCO_APP_ID and PCO_SECRET to read the sandbox plan");
        PcoClient pco(&m_network, QUrl(QString::fromLatin1(kPcoServicesApi)),
                      [creds] { return std::make_unique<PersonalAccessToken>(creds); });

        Waiter<QList<PcoServiceType>> types;
        pco.serviceTypes(types.done());
        QVERIFY(types.wait(30000));
        QVERIFY2(types.result->ok(), qPrintable(types.result->error.message));
        for (const PcoServiceType &type : types.result->value) {
            Waiter<QList<PcoPlan>> plans;
            pco.futurePlans(type.id, plans.done());
            QVERIFY(plans.wait(30000));
            QVERIFY2(plans.result->ok(), qPrintable(plans.result->error.message));
            if (plans.result->value.isEmpty())
                continue;
            const PcoPlan plan = plans.result->value.first();
            QVERIFY(plan.planningCenterUrl.isValid());
            Waiter<PcoPlanItems> items;
            pco.planItems(type.id, plan.id, items.done());
            QVERIFY(items.wait(30000));
            QVERIFY2(items.result->ok(), qPrintable(items.result->error.message));
            QCOMPARE(items.result->value.items.size(), plan.itemsCount);
            int songs = 0;
            for (const PcoItem &i : items.result->value.items) {
                if (i.type == PcoItemType::Song) {
                    ++songs;
                    QVERIFY(i.song);
                    QVERIFY(!i.sequence().isEmpty());
                }
            }
            qInfo("plan %s: %lld items, %d songs, %d requests", qPrintable(plan.id),
                  qlonglong(items.result->value.items.size()), songs, pco.requestCount());
            return;
        }
        QSKIP("no upcoming plans for this token");
    }

private:
    QNetworkAccessManager m_network;
};

QTEST_GUILESS_MAIN(PcoClientTest)
#include "tst_pcoclient.moc"
