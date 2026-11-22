#include "pco/pcoclient.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>

using namespace visualis;

Q_LOGGING_CATEGORY(lcPcoClient, "visualis.pco.client")

namespace {
constexpr char kPerPage[] = "100";

template <typename T>
PcoResult<T> failed(const PcoError &error)
{
    return {T{}, error};
}
} // namespace

PcoClient::PcoClient(QNetworkAccessManager *network, const QUrl &baseUrl, AuthorizerSource authorizer,
                     QObject *parent)
    : QObject(parent), m_network(network), m_baseUrl(baseUrl), m_authorizer(std::move(authorizer))
{
    m_clock.start();
}

void PcoClient::serviceTypes(Done<QList<PcoServiceType>> done)
{
    getAll(endpoint({"service_types"}, {{"per_page", kPerPage}}), [done](PcoResult<PcoDocument> r) {
        if (!r.ok())
            return done(failed<QList<PcoServiceType>>(r.error));
        PcoResult<QList<PcoServiceType>> out;
        out.value = serviceTypesFrom(r.value, &out.skipped);
        done(out);
    });
}

void PcoClient::futurePlans(const QString &serviceTypeId, Done<QList<PcoPlan>> done)
{
    const QUrl url = endpoint({"service_types", serviceTypeId, "plans"},
                              {{"filter", "future"}, {"order", "sort_date"}, {"per_page", kPerPage}});
    getAll(url, [done, serviceTypeId](PcoResult<PcoDocument> r) {
        if (!r.ok())
            return done(failed<QList<PcoPlan>>(r.error));
        PcoResult<QList<PcoPlan>> out;
        out.value = plansFrom(r.value, serviceTypeId, &out.skipped);
        done(out);
    });
}

void PcoClient::plan(const QString &serviceTypeId, const QString &planId, Done<PcoPlan> done)
{
    const QUrl url = endpoint({"service_types", serviceTypeId, "plans", planId});
    getDocument(url, [done, serviceTypeId](PcoResult<PcoDocument> r) {
        if (!r.ok())
            return done(failed<PcoPlan>(r.error));
        QStringList skipped;
        const QList<PcoPlan> plans = plansFrom(r.value, serviceTypeId, &skipped);
        if (plans.size() != 1) {
            qCWarning(lcPcoClient).noquote() << "plan unreadable:" << skipped.join("; ");
            return done(failed<PcoPlan>(pcoMalformedError()));
        }
        done(PcoResult<PcoPlan>{plans.first(), {}, skipped});
    });
}

void PcoClient::planItems(const QString &serviceTypeId, const QString &planId, Done<PcoPlanItems> done)
{
    const QUrl url = endpoint({"service_types", serviceTypeId, "plans", planId, "items"},
                              {{"include", "song,arrangement,key"}, {"per_page", kPerPage}});
    getAll(url, [done](PcoResult<PcoDocument> r) {
        if (!r.ok())
            return done(failed<PcoPlanItems>(r.error));
        PcoResult<PcoPlanItems> out;
        out.value = planItemsFrom(r.value);
        out.skipped = out.value.skipped;
        done(out);
    });
}

QUrl PcoClient::endpoint(const QStringList &segments, const QList<QPair<QString, QString>> &query) const
{
    QString path = m_baseUrl.path();
    if (!path.endsWith('/'))
        path += '/';
    QStringList encoded;
    for (const QString &s : segments)
        encoded << QString::fromLatin1(QUrl::toPercentEncoding(s));
    QUrl url = m_baseUrl;
    url.setPath(path + encoded.join('/'), QUrl::TolerantMode);
    QUrlQuery q;
    q.setQueryItems(query);
    url.setQuery(q);
    return url;
}

void PcoClient::holdUntil(qint64 delayMs)
{
    m_notBeforeMs = std::max(m_notBeforeMs, m_clock.elapsed() + delayMs);
}

bool PcoClient::sameApi(const QUrl &url) const
{
    // The token goes with every request, so only ever to Planning Center's own address.
    return url.scheme() == m_baseUrl.scheme() && url.host() == m_baseUrl.host()
           && url.port() == m_baseUrl.port() && url.path().startsWith(m_baseUrl.path());
}

void PcoClient::getAll(const QUrl &url, Fetched done)
{
    getPages(url, std::make_shared<Pages>(), std::move(done));
}

void PcoClient::getPages(const QUrl &url, std::shared_ptr<Pages> pages, Fetched done)
{
    pages->visited.insert(url);
    getDocument(url, [this, url, pages, done](PcoResult<PcoDocument> page) {
        if (!page.ok())
            return done(page);
        for (const QJsonValue &v : page.value.data)
            pages->merged.data.append(v);
        for (const QJsonValue &v : page.value.included)
            pages->merged.included.append(v);
        if (page.value.next.isEmpty())
            return done(PcoResult<PcoDocument>{pages->merged, {}});
        const QUrl next = url.resolved(page.value.next);
        if (!sameApi(next) || pages->visited.contains(next) || pages->visited.size() >= kMaxPages) {
            qCWarning(lcPcoClient) << "stopped paging: unexpected next link";
            return done(failed<PcoDocument>(pcoMalformedError()));
        }
        getPages(next, pages, done);
    });
}

void PcoClient::getDocument(const QUrl &url, Fetched done)
{
    send(url, 0, [done](Response response) {
        if (response.signedOut)
            return done(failed<PcoDocument>(
                {PcoError::NotSignedIn, 0, QStringLiteral("Not signed in to Planning Center.")}));
        if (const PcoError e = pcoErrorForStatus(response.status))
            return done(failed<PcoDocument>(e));
        if (response.cutOff)
            return done(failed<PcoDocument>({PcoError::Offline, response.status,
                QStringLiteral("The connection to Planning Center dropped. Check the internet connection.")}));
        done(parsePcoDocument(response.body));
    });
}

void PcoClient::send(const QUrl &url, int attempt, std::function<void(Response)> done)
{
    const qint64 waitMs = std::max<qint64>(0, m_notBeforeMs - m_clock.elapsed());
    // Planning Center asked for a longer pause than the operator should wait: say it's busy
    // now rather than send inside its cooldown (it would only answer 429) or hang.
    if (waitMs > m_maxWaitMs) {
        QTimer::singleShot(0, this, [done] {
            Response r;
            r.status = 429;
            r.notSent = true;   // never reached Planning Center
            done(r);
        });
        return;
    }
    QTimer::singleShot(waitMs, this, [this, url, attempt, done] {
        // A response that came in meanwhile may have moved the deadline later.
        if (m_notBeforeMs > m_clock.elapsed())
            return send(url, attempt, done);
        const std::unique_ptr<PcoAuthorizer> auth = m_authorizer ? m_authorizer() : nullptr;
        if (!auth) {
            Response r;
            r.signedOut = true;
            done(r);
            return;
        }
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", auth->authorization());
        request.setRawHeader("Accept", "application/json");
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("Visualis/%1").arg(QCoreApplication::applicationVersion()));
        request.setTransferTimeout(m_timeoutMs);
        ++m_requests;

        QNetworkReply *reply = m_network->get(request);
        reply->setParent(this);   // destroying the client aborts what it has in flight
        connect(reply, &QNetworkReply::finished, this, [this, reply, url, attempt, done] {
            reply->deleteLater();
            Response r;
            r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            r.body = reply->readAll();
            for (const auto &pair : reply->rawHeaderPairs())
                r.headers.append(pair);
            // An HTTP error status also sets a reply error; that's reported by status. A
            // transport error with a success status means the body didn't arrive whole.
            r.cutOff = reply->error() != QNetworkReply::NoError && r.status >= 200 && r.status < 300;

            // The real deadline, however far off: send() turns a wait past the cap into "busy".
            if (const qint64 pace = pcoPacingDelayMs(pcoRateLimitFrom(r.headers)); pace > 0)
                holdUntil(pace);

            if (r.status == 429) {
                const std::optional<qint64> delay = pcoRetryDelayMs(r.headers);
                if (delay && *delay != kPcoWaitTooLongMs)
                    holdUntil(*delay);
                if (delay && *delay <= m_maxWaitMs && attempt < kMaxRateLimitRetries) {
                    qCInfo(lcPcoClient) << "rate limited; retrying in" << *delay << "ms";
                    send(url, attempt + 1, done);
                    return;
                }
            }
            if (r.status != 200 || r.cutOff)
                qCInfo(lcPcoClient) << "HTTP" << r.status << reply->error() << url.path();   // never the token
            done(r);
        });
    });
}
