#pragma once

// A fake Planning Center for the importer tests: a localhost server answering from the
// fixtures, and helpers to edit what it sends. tst_pcoclient has the full one.

#include "pco/pcoclient.h"
#include "pco/planimporter.h"
#include "service/service.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <functional>
#include <memory>
#include <utility>

namespace visualis::testing {

using Kind = ServiceItem::Kind;


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
inline QByteArray fixtureWith(const QString &name, const std::function<void(QJsonArray &)> &edit)
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

inline void retitle(QJsonArray &items, const QString &id, const QString &title)
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

inline const ServiceItem *byPco(const Service &s, const QString &id)
{
    for (const ServiceItem &it : s.items) {
        if (it.pcoItemId == id)
            return &it;
    }
    return nullptr;
}

inline QStringList changeKinds(const PlanImporter &importer)
{
    QStringList out;
    for (const QVariant &c : importer.reimportChanges())
        out << c.toMap().value("kind").toString();
    return out;
}


inline std::unique_ptr<PcoClient> fakeClient(QNetworkAccessManager &network, const QUrl &url)
{
    auto c = std::make_unique<PcoClient>(&network, url, [] { return std::make_unique<FixedToken>(); });
    c->setTransferTimeoutMs(2000);
    return c;
}

// The whole sample plan: two service types, two plans, five items on two pages.
inline void serveFixtures(FakePlanningCenter &server)
{
    server.on(kTypes, {"service_types.json"});
    server.on(kPlans, {"plans.json"});
    server.on(kPlan, {"plan.json"});
    server.on(kItems, {"items_page1.json", "items_page2.json"});
}

inline bool idle(PlanImporter &importer) { return QTest::qWaitFor([&] { return !importer.busy(); }, 5000); }

} // namespace visualis::testing
