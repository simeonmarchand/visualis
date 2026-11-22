#pragma once

#include "pco/pcotypes.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QUrl>

#include <limits>
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

// Parses one page. Not JSON, JSON that isn't a JSON:API document, or `included` or
// `links.next` of the wrong shape, is Malformed.
PcoResult<PcoDocument> parsePcoDocument(const QByteArray &body);

// Each reader keeps the records it can use and names every other one in `skipped`, for the
// operator: a record that isn't a resource, of the wrong type, without an id or attributes,
// or missing a field it can't do without (a service type's name, a plan's date).
QList<PcoServiceType> serviceTypesFrom(const PcoDocument &doc, QStringList *skipped = nullptr);
QList<PcoPlan> plansFrom(const PcoDocument &doc, const QString &serviceTypeId, QStringList *skipped = nullptr);
// Items in plan order, with their included song, arrangement, and key resolved. An item
// without its position, kind, or title is skipped. One whose song, arrangement, or key is
// missing from the answer or malformed is kept without it. Both are named in `skipped`.
PcoPlanItems planItemsFrom(const PcoDocument &doc);

// The rate limit as Planning Center reported it on a response (X-PCO-API-Request-Rate-*).
struct PcoRateLimit
{
    qint64 count = 0;           // requests used in the current period
    qint64 limit = 0;           // 0 when the headers were missing
    qint64 periodSeconds = 0;   // values too big to read saturate rather than wrap
};
using PcoHeaders = QList<QPair<QByteArray, QByteArray>>;
PcoRateLimit pcoRateLimitFrom(const PcoHeaders &headers);

// A wait no caller accepts: Retry-After was there but couldn't be read.
inline constexpr qint64 kPcoWaitTooLongMs = std::numeric_limits<qint64>::max();

// How long to wait before retrying a 429, from Retry-After, else the reported period.
// kPcoWaitTooLongMs when Retry-After can't be read. nullopt when the answer gives no way to
// know (then the caller gives up rather than guess).
std::optional<qint64> pcoRetryDelayMs(const PcoHeaders &headers);
// How long to wait before the next request so as not to cause a 429: the reported period
// once the limit is used up, otherwise no wait. Both clamp absurd values to a day; the
// client caps what it will actually wait (PcoClient::kMaxRetryWaitMs).
qint64 pcoPacingDelayMs(const PcoRateLimit &rate);

} // namespace visualis
