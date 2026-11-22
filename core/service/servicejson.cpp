#include "service/servicejson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace visualis {

namespace {
constexpr int kVersion = 1;
QString idText(const QUuid &id) { return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces); }
QString time(const QDateTime &t) { return t.isValid() ? t.toUTC().toString(Qt::ISODateWithMs) : QString(); }
} // namespace

QByteArray serviceToJson(const Service &s)
{
    QJsonArray items;
    for (const ServiceItem &it : s.items) {
        items.append(QJsonObject{
            {"id", idText(it.id)}, {"kind", kindName(it.kind)}, {"title", it.title}, {"ref", idText(it.ref)},
            {"arrangement", idText(it.arrangement)}, {"customSequence", QJsonArray::fromStringList(it.customSequence)},
            {"body", it.body}, {"servicePosition", it.servicePosition}, {"pcoItemId", it.pcoItemId},
            {"overrides", it.overrides}, {"importSnapshot", it.importSnapshot}, {"pcoRemoved", it.pcoRemoved},
        });
    }
    QJsonArray removed;
    for (const RemovedPcoItem &r : s.removedPcoItems)
        removed.append(QJsonObject{{"pcoItemId", r.pcoItemId}, {"importSnapshot", r.importSnapshot}});
    const QJsonObject o{
        {"format", "visualis-service"}, {"version", kVersion}, {"id", idText(s.id)}, {"title", s.title},
        {"date", s.date.isValid() ? s.date.toString(Qt::ISODate) : QString()}, {"pcoPlanId", s.pcoPlanId},
        {"pcoServiceTypeId", s.pcoServiceTypeId}, {"pcoPlanUrl", s.pcoPlanUrl}, {"lastImportedAt", time(s.lastImportedAt)},
        {"created", time(s.created)}, {"updated", time(s.updated)}, {"items", items},
        {"removedPcoItems", removed},
    };
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

ServiceJsonResult serviceFromJson(const QByteArray &json)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject())
        return {{}, QStringLiteral("not JSON: %1").arg(parseError.errorString()), {}};
    const QJsonObject o = doc.object();
    if (o.value("format").toString() != "visualis-service")
        return {{}, QStringLiteral("not a Visualis service file"), {}};
    if (o.value("version").toInt() > kVersion)
        return {{}, QStringLiteral("made by a newer Visualis"), {}};
    if (o.value("title").toString().trimmed().isEmpty())
        return {{}, QStringLiteral("the service has no title"), {}};

    ServiceJsonResult r;
    Service s;
    s.id = QUuid::fromString(o.value("id").toString());
    s.title = o.value("title").toString();
    s.date = QDate::fromString(o.value("date").toString(), Qt::ISODate);
    s.pcoPlanId = o.value("pcoPlanId").toString();
    s.pcoServiceTypeId = o.value("pcoServiceTypeId").toString();
    s.pcoPlanUrl = o.value("pcoPlanUrl").toString();
    s.lastImportedAt = QDateTime::fromString(o.value("lastImportedAt").toString(), Qt::ISODateWithMs);
    s.created = QDateTime::fromString(o.value("created").toString(), Qt::ISODateWithMs);
    s.updated = QDateTime::fromString(o.value("updated").toString(), Qt::ISODateWithMs);
    for (const QJsonValue &v : o.value("items").toArray()) {
        const QJsonObject i = v.toObject();
        ServiceItem it;
        it.id = QUuid::fromString(i.value("id").toString());
        const QString kind = i.value("kind").toString();
        it.kind = kindFromName(kind).value_or(ServiceItem::Kind::Placeholder);
        if (!kindFromName(kind))
            r.warnings << QStringLiteral("\"%1\" has an unknown kind \"%2\"; it's a placeholder.").arg(i.value("title").toString(), kind);
        it.title = i.value("title").toString();
        it.ref = QUuid::fromString(i.value("ref").toString());
        it.arrangement = QUuid::fromString(i.value("arrangement").toString());
        for (const QJsonValue &label : i.value("customSequence").toArray())
            it.customSequence << label.toString();
        it.body = i.value("body").toString();
        it.servicePosition = i.value("servicePosition").toString();
        it.pcoItemId = i.value("pcoItemId").toString();
        it.overrides = i.value("overrides").toObject();
        it.importSnapshot = i.value("importSnapshot").toObject();
        it.pcoRemoved = i.value("pcoRemoved").toBool();
        s.items << it;
    }
    for (const QJsonValue &v : o.value("removedPcoItems").toArray()) {
        const QJsonObject x = v.toObject();
        s.removedPcoItems << RemovedPcoItem{x.value("pcoItemId").toString(), x.value("importSnapshot").toObject()};
    }
    r.service = s;
    return r;
}

} // namespace visualis
