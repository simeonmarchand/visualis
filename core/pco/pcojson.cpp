#include "pco/pcojson.h"

#include <algorithm>
#include <limits>

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

namespace {

// A data record that's a resource of the expected type with an id and attributes, or why not.
struct Record
{
    QString id;
    QJsonObject resource;
    QJsonObject attributes;
};

std::optional<Record> record(const QJsonValue &v, qsizetype index, const char *type, QStringList *skipped)
{
    const auto skip = [skipped](const QString &why) {
        if (skipped)
            *skipped << why;
        return std::nullopt;
    };
    const QString where = QStringLiteral("Record %1").arg(index + 1);
    if (!v.isObject() || v.toObject().isEmpty())
        return skip(QStringLiteral("%1: not a Planning Center record").arg(where));
    const QJsonObject o = v.toObject();
    const QString actual = o.value("type").toString();
    if (actual != QLatin1String(type))
        return skip(actual.isEmpty() ? QStringLiteral("%1: has no type").arg(where)
                                     : QStringLiteral("%1: a %2 where a %3 was expected").arg(where, actual, type));
    const QString id = idOf(o.value("id"));
    if (id.isEmpty()) {
        const QString title = o.value("attributes").toObject().value("title").toString().trimmed();
        return skip(title.isEmpty() ? QStringLiteral("%1 (%2): has no id").arg(where, actual)
                                    : QStringLiteral("%1 (%2 \"%3\"): has no id").arg(where, actual, title));
    }
    if (!o.value("attributes").isObject())
        return skip(QStringLiteral("%1 %2: has no attributes").arg(actual, id));
    return Record{id, o, o.value("attributes").toObject()};
}

bool isNumber(const QJsonValue &v) { return v.isDouble(); }

// A section order: absent, null, or a list of labels.
bool validSequence(const QJsonValue &v)
{
    if (v.isNull() || v.isUndefined())
        return true;
    const QJsonArray steps = v.toArray();
    return v.isArray() && std::all_of(steps.begin(), steps.end(), [](const QJsonValue &e) { return e.isString(); });
}

// What a relationship of an item points at: nothing, the included record, or why it can't be used.
struct Related
{
    std::optional<QJsonObject> resource;
    QString problem;
};

Related related(const QJsonObject &item, const char *name, const QHash<QString, QJsonObject> &index)
{
    const QJsonValue data = item.value("relationships").toObject().value(QLatin1String(name)).toObject().value("data");
    if (data.isNull() || data.isUndefined())
        return {};
    const QJsonObject ref = data.toObject();
    const QString type = ref.value("type").toString();
    const QString id = idOf(ref.value("id"));
    if (type.isEmpty() || id.isEmpty())
        return {std::nullopt, QStringLiteral("its %1 link is malformed").arg(QLatin1String(name))};
    const auto it = index.constFind(type + '/' + id);
    if (it == index.constEnd())
        return {std::nullopt, QStringLiteral("its %1 (%2 %3) wasn't in Planning Center's answer").arg(QLatin1String(name), type, id)};
    if (!it->value("attributes").isObject())
        return {std::nullopt, QStringLiteral("its %1 (%2 %3) has no attributes").arg(QLatin1String(name), type, id)};
    return {*it, {}};
}

QString itemName(const PcoItem &item)
{
    return item.title.isEmpty() ? QStringLiteral("Item %1").arg(item.id)
                                : QStringLiteral("Item %1 (\"%2\")").arg(item.id, item.title);
}

} // namespace

QList<PcoServiceType> serviceTypesFrom(const PcoDocument &doc, QStringList *skipped)
{
    QList<PcoServiceType> out;
    for (qsizetype i = 0; i < doc.data.size(); ++i) {
        const std::optional<Record> r = record(doc.data[i], i, "ServiceType", skipped);
        if (!r)
            continue;
        const QString name = str(r->attributes, "name");
        if (name.isEmpty()) {
            if (skipped)
                *skipped << QStringLiteral("ServiceType %1: has no name").arg(r->id);
            continue;
        }
        out.append({r->id, name});
    }
    return out;
}

QList<PcoPlan> plansFrom(const PcoDocument &doc, const QString &serviceTypeId, QStringList *skipped)
{
    QList<PcoPlan> out;
    for (qsizetype i = 0; i < doc.data.size(); ++i) {
        const std::optional<Record> r = record(doc.data[i], i, "Plan", skipped);
        if (!r)
            continue;
        const QJsonObject &a = r->attributes;
        PcoPlan p;
        p.id = r->id;
        p.serviceTypeId = serviceTypeId;
        p.sortDate = QDateTime::fromString(str(a, "sort_date"), Qt::ISODate).toUTC();
        // The date orders plans and picks "the next one"; a plan without one can't be placed.
        if (!p.sortDate.isValid()) {
            if (skipped)
                *skipped << QStringLiteral("Plan %1: has no valid date").arg(p.id);
            continue;
        }
        p.title = str(a, "title");
        p.seriesTitle = str(a, "series_title");
        p.dates = str(a, "dates");
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
    for (qsizetype i = 0; i < doc.data.size(); ++i) {
        const std::optional<Record> r = record(doc.data[i], i, "Item", &out.skipped);
        if (!r) {
            const QString id = doc.data[i].isObject() ? idOf(doc.data[i].toObject().value("id")) : QString();
            if (id.isEmpty())
                out.skippedWithoutId = true;
            else
                out.unreadableIds << id;
            continue;
        }
        const QJsonObject &a = r->attributes;
        PcoItem item;
        item.id = r->id;
        item.title = str(a, "title");
        // Its place in the plan and its kind can't be guessed: made-up defaults would put a
        // blank item first.
        if (!isNumber(a.value("sequence")) || !a.value("item_type").isString() || !a.value("title").isString()) {
            out.skipped << QStringLiteral("%1: is missing its position, kind, or title").arg(itemName(item));
            out.unreadableIds << item.id;
            continue;
        }
        item.type = itemType(str(a, "item_type"));
        item.position = intOf(a.value("sequence"));
        item.servicePosition = servicePosition(str(a, "service_position"));
        item.lengthSeconds = intOf(a.value("length"));
        item.description = str(a, "description");
        item.htmlDetails = a.value("html_details").toString();
        item.keyName = str(a, "key_name");

        // A broken link keeps the item, without what it pointed at, and says so.
        const auto problem = [&](const QString &what) {
            out.skipped << QStringLiteral("%1: %2").arg(itemName(item), what);
            if (!out.unreadableIds.contains(item.id))
                out.unreadableIds << item.id;
        };
        // Its own section order, checked like an arrangement's: emptying a malformed one
        // would quietly change what's sung (#103 review).
        if (const QJsonValue own = a.value("custom_arrangement_sequence"); validSequence(own))
            item.customArrangementSequence = stringList(own);
        else
            problem(QStringLiteral("its section order is malformed"));
        if (const Related s = related(r->resource, "song", index); !s.problem.isEmpty()) {
            problem(s.problem);
        } else if (s.resource) {
            const QJsonObject sa = attrs(*s.resource);
            if (!sa.value("title").isString())
                problem(QStringLiteral("its song has no title"));
            else
                item.song = PcoSong{idOf(s.resource->value("id")), str(sa, "title"), idOf(sa.value("ccli_number")),
                                    str(sa, "author"), str(sa, "copyright")};
        }
        if (const Related ar = related(r->resource, "arrangement", index); !ar.problem.isEmpty()) {
            problem(ar.problem);
        } else if (ar.resource) {
            const QJsonObject ra = attrs(*ar.resource);
            const QJsonValue sequence = ra.value("sequence");
            if (!validSequence(sequence))
                problem(QStringLiteral("its arrangement's section order is malformed"));
            else
                item.arrangement = PcoArrangement{idOf(ar.resource->value("id")), str(ra, "name"), stringList(sequence),
                                                  intOf(ra.value("bpm")), str(ra, "meter"), intOf(ra.value("length"))};
        }
        if (const Related k = related(r->resource, "key", index); !k.problem.isEmpty()) {
            problem(k.problem);
        } else if (k.resource) {
            const QJsonObject ka = attrs(*k.resource);
            item.key = PcoKey{idOf(k.resource->value("id")), str(ka, "name"), str(ka, "starting_key")};
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

namespace {
constexpr qint64 kOneDaySeconds = 24 * 60 * 60;

// A non-negative whole number from a header, saturating instead of overflowing; nullopt when
// it's absent or not a number.
std::optional<qint64> wideNumber(const QByteArray &raw)
{
    const QByteArray v = raw.trimmed();
    if (v.isEmpty() || !std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
    bool ok = false;
    const qint64 n = v.toLongLong(&ok);
    return ok ? n : std::numeric_limits<qint64>::max();
}

qint64 secondsToMs(qint64 seconds)
{
    return std::min(seconds, kOneDaySeconds) * 1000;
}
} // namespace

PcoRateLimit pcoRateLimitFrom(const PcoHeaders &headers)
{
    return {wideNumber(header(headers, "X-PCO-API-Request-Rate-Count")).value_or(0),
            wideNumber(header(headers, "X-PCO-API-Request-Rate-Limit")).value_or(0),
            wideNumber(header(headers, "X-PCO-API-Request-Rate-Period")).value_or(0)};
}

std::optional<qint64> pcoRetryDelayMs(const PcoHeaders &headers)
{
    const QByteArray retryAfter = header(headers, "Retry-After");
    if (!retryAfter.isEmpty()) {
        // Present but unreadable (an HTTP date, garbage): the server asked for a wait we
        // can't measure, so it's too long, not absent.
        const std::optional<qint64> seconds = wideNumber(retryAfter);
        return seconds ? secondsToMs(*seconds) : kPcoWaitTooLongMs;
    }
    if (const qint64 period = pcoRateLimitFrom(headers).periodSeconds; period > 0)
        return secondsToMs(period);
    return std::nullopt;
}

qint64 pcoPacingDelayMs(const PcoRateLimit &rate)
{
    if (rate.limit > 0 && rate.count >= rate.limit && rate.periodSeconds > 0)
        return secondsToMs(rate.periodSeconds);
    return 0;
}

} // namespace visualis
