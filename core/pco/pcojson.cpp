#include "pco/pcojson.h"

#include <algorithm>

#include <QHash>
#include <QJsonDocument>
#include <QJsonParseError>

namespace visualis {

namespace {

QString str(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toString().trimmed();
}

// Planning Center sends some numbers as numbers and some ids as strings; accept either.
QString idOf(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    if (v.isDouble())
        return QString::number(v.toInteger());
    return {};
}

int intOf(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toInt();
    return v.toString().toInt();
}

QStringList stringList(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &e : v.toArray()) {
        if (const QString s = e.toString().trimmed(); !s.isEmpty())
            out << s;
    }
    return out;
}

QJsonObject attrs(const QJsonObject &resource)
{
    return resource.value("attributes").toObject();
}

// The included records by "Type/id", for resolving relationships.
QHash<QString, QJsonObject> indexIncluded(const QJsonArray &included)
{
    QHash<QString, QJsonObject> index;
    for (const QJsonValue &v : included) {
        const QJsonObject o = v.toObject();
        index.insert(o.value("type").toString() + '/' + idOf(o.value("id")), o);
    }
    return index;
}

std::optional<QJsonObject> related(const QJsonObject &resource, const char *name,
                                   const QHash<QString, QJsonObject> &index)
{
    const QJsonObject ref = resource.value("relationships").toObject().value(QLatin1String(name)).toObject()
                                .value("data").toObject();
    if (ref.isEmpty())
        return std::nullopt;
    const auto it = index.constFind(ref.value("type").toString() + '/' + idOf(ref.value("id")));
    if (it == index.constEnd())
        return std::nullopt;
    return *it;
}

PcoItemType itemType(const QString &s)
{
    if (s == "song")
        return PcoItemType::Song;
    if (s == "header")
        return PcoItemType::Header;
    if (s == "media")
        return PcoItemType::Media;
    return PcoItemType::Item;
}

PcoServicePosition servicePosition(const QString &s)
{
    if (s == "pre")
        return PcoServicePosition::Pre;
    if (s == "post")
        return PcoServicePosition::Post;
    return PcoServicePosition::During;
}

} // namespace

PcoError pcoErrorForStatus(int httpStatus)
{
    if (httpStatus >= 200 && httpStatus < 300)
        return {};
    switch (httpStatus) {
    case 0:
        return {PcoError::Offline, 0, QStringLiteral("Couldn't reach Planning Center. Check the internet connection.")};
    case 401:
        return {PcoError::Unauthorized, 401,
                QStringLiteral("Planning Center didn't accept the saved token. Sign in again.")};
    case 403:
        return {PcoError::Forbidden, 403,
                QStringLiteral("The token isn't allowed to see this in Planning Center. Check its permissions.")};
    case 404:
        return {PcoError::NotFound, 404,
                QStringLiteral("Planning Center couldn't find that. It may have been deleted or moved.")};
    case 429:
        return {PcoError::RateLimited, 429,
                QStringLiteral("Planning Center is busy (too many requests). Try again in a minute.")};
    default:
        if (httpStatus >= 500)
            return {PcoError::Server, httpStatus,
                    QStringLiteral("Planning Center is having trouble (%1). Try again later.").arg(httpStatus)};
        return {PcoError::Other, httpStatus,
                QStringLiteral("Planning Center answered with an error (%1).").arg(httpStatus)};
    }
}

PcoError pcoMalformedError()
{
    return {PcoError::Malformed, 0, QStringLiteral("Planning Center's answer wasn't what Visualis expected.")};
}

PcoResult<PcoDocument> parsePcoDocument(const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return {{}, pcoMalformedError()};
    const QJsonObject root = json.object();
    const QJsonValue data = root.value("data");

    PcoDocument doc;
    if (data.isArray())
        doc.data = data.toArray();
    else if (data.isObject())
        doc.data.append(data);
    else
        return {{}, pcoMalformedError()};
    // A missing `included` or `next` is normal; one of the wrong shape isn't, and reading it
    // as "nothing more" would quietly drop records or pages.
    const QJsonValue included = root.value("included");
    if (!included.isUndefined() && !included.isNull()) {
        if (!included.isArray())
            return {{}, pcoMalformedError()};
        for (const QJsonValue &v : included.toArray()) {
            if (!v.isObject())
                return {{}, pcoMalformedError()};
        }
        doc.included = included.toArray();
    }
    const QJsonValue links = root.value("links");
    if (!links.isUndefined() && !links.isNull() && !links.isObject())
        return {{}, pcoMalformedError()};
    const QJsonValue next = links.toObject().value("next");
    if (next.isString()) {
        doc.next = QUrl(next.toString(), QUrl::StrictMode);
        if (!next.toString().isEmpty() && !doc.next.isValid())
            return {{}, pcoMalformedError()};
    } else if (!next.isUndefined() && !next.isNull()) {
        return {{}, pcoMalformedError()};
    }
    return {doc, {}};
}

QList<PcoServiceType> serviceTypesFrom(const PcoDocument &doc)
{
    QList<PcoServiceType> out;
    for (const QJsonValue &v : doc.data) {
        const QJsonObject o = v.toObject();
        if (o.value("type").toString() != "ServiceType")
            continue;
        const QString id = idOf(o.value("id"));
        if (!id.isEmpty())
            out.append({id, str(attrs(o), "name")});
    }
    return out;
}

QList<PcoPlan> plansFrom(const PcoDocument &doc, const QString &serviceTypeId)
{
    QList<PcoPlan> out;
    for (const QJsonValue &v : doc.data) {
        const QJsonObject o = v.toObject();
        if (o.value("type").toString() != "Plan")
            continue;
        PcoPlan p;
        p.id = idOf(o.value("id"));
        if (p.id.isEmpty())
            continue;
        const QJsonObject a = attrs(o);
        p.serviceTypeId = serviceTypeId;
        p.title = str(a, "title");
        p.seriesTitle = str(a, "series_title");
        p.dates = str(a, "dates");
        p.sortDate = QDateTime::fromString(str(a, "sort_date"), Qt::ISODate).toUTC();
        p.itemsCount = intOf(a.value("items_count"));
        p.planningCenterUrl = QUrl(str(a, "planning_center_url"));
        out.append(p);
    }
    return out;
}

PcoPlanItems planItemsFrom(const PcoDocument &doc)
{
    const QHash<QString, QJsonObject> index = indexIncluded(doc.included);
    PcoPlanItems out;
    for (const QJsonValue &v : doc.data) {
        const QJsonObject o = v.toObject();
        if (o.value("type").toString() != "Item")
            continue;
        const QJsonObject a = attrs(o);
        PcoItem item;
        item.id = idOf(o.value("id"));
        item.title = str(a, "title");
        if (item.id.isEmpty()) {
            out.skipped << QStringLiteral("An item with no id (\"%1\")").arg(item.title);
            continue;
        }
        item.type = itemType(str(a, "item_type"));
        item.position = intOf(a.value("sequence"));
        item.servicePosition = servicePosition(str(a, "service_position"));
        item.lengthSeconds = intOf(a.value("length"));
        item.description = str(a, "description");
        item.htmlDetails = a.value("html_details").toString();
        item.keyName = str(a, "key_name");
        item.customArrangementSequence = stringList(a.value("custom_arrangement_sequence"));

        if (const auto s = related(o, "song", index)) {
            const QJsonObject sa = attrs(*s);
            item.song = PcoSong{idOf(s->value("id")), str(sa, "title"), idOf(sa.value("ccli_number")),
                                str(sa, "author"), str(sa, "copyright")};
        }
        if (const auto r = related(o, "arrangement", index)) {
            const QJsonObject ra = attrs(*r);
            item.arrangement = PcoArrangement{idOf(r->value("id")), str(ra, "name"), stringList(ra.value("sequence")),
                                              intOf(ra.value("bpm")), str(ra, "meter"), intOf(ra.value("length"))};
        }
        if (const auto k = related(o, "key", index)) {
            const QJsonObject ka = attrs(*k);
            item.key = PcoKey{idOf(k->value("id")), str(ka, "name"), str(ka, "starting_key")};
        }
        out.items.append(item);
    }
    // Pages come in order, but the plan's order is `sequence`; keep ties as they came.
    std::stable_sort(out.items.begin(), out.items.end(),
                     [](const PcoItem &x, const PcoItem &y) { return x.position < y.position; });
    return out;
}

namespace {
QByteArray header(const PcoHeaders &headers, const char *name)
{
    for (const auto &[key, value] : headers) {
        if (key.compare(name, Qt::CaseInsensitive) == 0)
            return value.trimmed();
    }
    return {};
}
} // namespace

PcoRateLimit pcoRateLimitFrom(const PcoHeaders &headers)
{
    return {header(headers, "X-PCO-API-Request-Rate-Count").toInt(),
            header(headers, "X-PCO-API-Request-Rate-Limit").toInt(),
            header(headers, "X-PCO-API-Request-Rate-Period").toInt()};
}

namespace {
// Header seconds as milliseconds, 64-bit and clamped, so a huge value can't overflow. Any
// clamped value is far over what the client accepts anyway.
std::optional<qint64> secondsToMs(const QByteArray &value)
{
    bool ok = false;
    const qint64 seconds = value.toLongLong(&ok);
    if (!ok || seconds < 0)
        return std::nullopt;
    constexpr qint64 kOneDay = 24 * 60 * 60;
    return std::min(seconds, kOneDay) * 1000;
}
} // namespace

std::optional<qint64> pcoRetryDelayMs(const PcoHeaders &headers)
{
    if (const auto retryAfter = secondsToMs(header(headers, "Retry-After")))
        return retryAfter;
    if (const auto period = secondsToMs(header(headers, "X-PCO-API-Request-Rate-Period")); period && *period > 0)
        return period;
    return std::nullopt;
}

qint64 pcoPacingDelayMs(const PcoRateLimit &rate)
{
    if (rate.limit > 0 && rate.count >= rate.limit && rate.periodSeconds > 0)
        return std::min<qint64>(rate.periodSeconds, 24 * 60 * 60) * 1000;
    return 0;
}

} // namespace visualis
