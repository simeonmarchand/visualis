#pragma once

#include "pco/pcoauth.h"
#include "pco/pcojson.h"
#include "pco/pcotypes.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QUrl>

#include <functional>
#include <memory>

class QNetworkAccessManager;

// Reads service types, plans, and plan items from Planning Center Services (#53, plan 5.3).
// Every call returns at once and answers later through its callback, so a slow or dead
// network never holds up the operator window or the outputs. Follows pagination, waits as
// the rate-limit headers say (and retries a 429 after Retry-After), and turns every failure
// into a PcoError with a message for the operator. Callbacks never run after the client is
// destroyed.
class PcoClient : public QObject
{
    Q_OBJECT

public:
    // Asked for on every request, so a sign-out takes effect at once. Null means signed out.
    using AuthorizerSource = std::function<std::unique_ptr<visualis::PcoAuthorizer>()>;

    template <typename T>
    using Done = std::function<void(visualis::PcoResult<T>)>;

    PcoClient(QNetworkAccessManager *network, const QUrl &baseUrl, AuthorizerSource authorizer,
              QObject *parent = nullptr);

    void serviceTypes(Done<QList<visualis::PcoServiceType>> done);
    // Plans from today on, soonest first.
    void futurePlans(const QString &serviceTypeId, Done<QList<visualis::PcoPlan>> done);
    void plan(const QString &serviceTypeId, const QString &planId, Done<visualis::PcoPlan> done);
    // The plan's items with their songs, arrangements, and keys.
    void planItems(const QString &serviceTypeId, const QString &planId, Done<visualis::PcoPlanItems> done);

    // How many HTTP requests have been sent, retries included.
    int requestCount() const { return m_requests; }
    // For tests: a shorter time to wait for an answer that never comes.
    void setTransferTimeoutMs(int ms) { m_timeoutMs = ms; }
    // For tests: a shorter longest wait than kMaxRetryWaitMs.
    void setMaxWaitMs(int ms) { m_maxWaitMs = ms; }

    // Retries of one request after 429s before giving up, and the longest wait accepted
    // (for a 429, and for pacing from the rate-limit headers).
    static constexpr int kMaxRateLimitRetries = 3;
    static constexpr int kMaxRetryWaitMs = 60'000;
    // A guard against a server whose `next` links never end.
    static constexpr int kMaxPages = 100;

private:
    struct Response
    {
        int status = 0;
        QByteArray body;
        visualis::PcoHeaders headers;
        bool signedOut = false;
        bool cutOff = false;   // a success status, but the transfer failed partway
    };
    struct Pages
    {
        visualis::PcoDocument merged;
        QSet<QUrl> visited;   // catches next links that loop, A → B → A included
    };
    using Fetched = std::function<void(visualis::PcoResult<visualis::PcoDocument>)>;

    QUrl endpoint(const QStringList &segments, const QList<QPair<QString, QString>> &query = {}) const;
    void getAll(const QUrl &url, Fetched done);
    void getPages(const QUrl &url, std::shared_ptr<Pages> pages, Fetched done);
    void getDocument(const QUrl &url, Fetched done);
    void send(const QUrl &url, int attempt, std::function<void(Response)> done);
    bool sameApi(const QUrl &url) const;

    QNetworkAccessManager *m_network;
    QUrl m_baseUrl;
    AuthorizerSource m_authorizer;
    int m_timeoutMs = 15000;
    int m_maxWaitMs = kMaxRetryWaitMs;
    int m_requests = 0;
    // When the next request may go, per the rate-limit headers.
    QElapsedTimer m_clock;
    qint64 m_notBeforeMs = 0;
};
