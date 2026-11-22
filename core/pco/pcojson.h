#pragma once

#include "pco/pcotypes.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QUrl>

#include <optional>

// Reading Planning Center's JSON:API answers (#53), with no network, so every case is
// testable from fixtures. The client (app/pco/pcoclient.h) does the requests.
namespace visualis {

// One JSON:API document, or several pages of one merged.
struct PcoDocument
{
    QJsonArray data;        // a single-resource document is stored as a one-element array
    QJsonArray included;
    QUrl next;              // links.next, empty on the last page
};

// The error for an HTTP status (0 for no answer at all), or None for 2xx.
PcoError pcoErrorForStatus(int httpStatus);
PcoError pcoMalformedError();

// Parses one page. Not JSON, or JSON that isn't a JSON:API document, is Malformed.
PcoResult<PcoDocument> parsePcoDocument(const QByteArray &body);

QList<PcoServiceType> serviceTypesFrom(const PcoDocument &doc);
QList<PcoPlan> plansFrom(const PcoDocument &doc, const QString &serviceTypeId);
// Items in plan order, with their included song, arrangement, and key resolved. A record
// without an id is skipped and named in `skipped`.
PcoPlanItems planItemsFrom(const PcoDocument &doc);

// The rate limit as Planning Center reported it on a response (X-PCO-API-Request-Rate-*).
struct PcoRateLimit
{
    int count = 0;           // requests used in the current period
    int limit = 0;           // 0 when the headers were missing
    int periodSeconds = 0;
};
using PcoHeaders = QList<QPair<QByteArray, QByteArray>>;
PcoRateLimit pcoRateLimitFrom(const PcoHeaders &headers);

// How long to wait before retrying a 429, from Retry-After, else the reported period.
// nullopt when the answer gives no way to know (then the caller gives up rather than guess).
std::optional<int> pcoRetryDelayMs(const PcoHeaders &headers);
// How long to wait before the next request so as not to cause a 429: the reported period
// once the limit is used up, otherwise no wait.
int pcoPacingDelayMs(const PcoRateLimit &rate);

} // namespace visualis
