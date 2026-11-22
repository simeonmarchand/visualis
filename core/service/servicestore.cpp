#include "service/servicestore.h"

#include "library/library.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace visualis {

namespace {

QString key(const QUuid &id) { return id.isNull() ? QStringLiteral("") : id.toString(QUuid::WithoutBraces); }
QString text(const QString &s) { return s.isNull() ? QStringLiteral("") : s; }
QUuid uuid(const QVariant &v) { return QUuid::fromString(v.toString()); }
QString timestamp(const QDateTime &t) { return t.isValid() ? t.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral(""); }
QDateTime fromTimestamp(const QVariant &v) { return QDateTime::fromString(v.toString(), Qt::ISODateWithMs); }

QString json(const QJsonObject &o) { return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)); }
QString json(const QStringList &l) { return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(l)).toJson(QJsonDocument::Compact)); }
QJsonObject objectFrom(const QVariant &v) { return QJsonDocument::fromJson(v.toString().toUtf8()).object(); }
QStringList listFrom(const QVariant &v)
{
    QStringList out;
    for (const QJsonValue &s : QJsonDocument::fromJson(v.toString().toUtf8()).array())
        out << s.toString();
    return out;
}

QString run(QSqlDatabase db, const QString &sql, const QVariantList &values)
{
    QSqlQuery q(db);
    if (!q.prepare(sql))
        return q.lastError().text();
    for (const QVariant &v : values)
        q.addBindValue(v);
    return q.exec() ? QString() : q.lastError().text();
}

bool songExists(QSqlDatabase db, const QUuid &id)
{
    QSqlQuery q(db);
    q.prepare("SELECT 1 FROM song WHERE id = ?");
    q.addBindValue(key(id));
    return q.exec() && q.next();
}

} // namespace

ServiceStore::ServiceStore(Library &library)
    : m_library(library)
{
}

ServiceStore::Result ServiceStore::save(Service &service)
{
    return save(service, false);
}

ServiceStore::Result ServiceStore::saveNewPlan(Service &service)
{
    return save(service, true);
}

ServiceStore::Result ServiceStore::save(Service &service, bool newPlan)
{
    Service s = service;   // a failed save leaves the caller's service as it was
    if (s.title.trimmed().isEmpty())
        return {false, QStringLiteral("A service needs a title.")};
    if (s.id.isNull())
        s.id = QUuid::createUuid();
    QSet<QUuid> itemIds;
    for (ServiceItem &item : s.items) {
        if (item.id.isNull())
            item.id = QUuid::createUuid();
        if (itemIds.contains(item.id))
            return {false, QStringLiteral("\"%1\" has two items with the same id.").arg(s.title)};
        itemIds.insert(item.id);
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!s.created.isValid())
        s.created = now;
    s.updated = now;

    QSqlDatabase db = m_library.database();
    const QString serviceId = key(s.id);
    // An item id must not already belong to another service.
    for (const QUuid &id : std::as_const(itemIds)) {
        QSqlQuery q(db);
        q.prepare("SELECT 1 FROM service_item WHERE id = ? AND service_id <> ?");
        q.addBindValue(key(id));
        q.addBindValue(serviceId);
        if (q.exec() && q.next())
            return {false, QStringLiteral("\"%1\" uses an item that belongs to another service.").arg(s.title)};
    }

    if (newPlan) {
        // Take the write lock now, without waiting for it, so the duplicate check below
        // holds until the commit.
        QSqlQuery q(db);
        q.exec("PRAGMA busy_timeout = 0");
        const bool begun = q.exec("BEGIN IMMEDIATE");
        const QSqlError error = q.lastError();
        q.exec(QStringLiteral("PRAGMA busy_timeout = %1").arg(Library::kBusyTimeoutMs));
        if (!begun) {
            const bool busy = error.nativeErrorCode() == QLatin1String("5");   // SQLITE_BUSY
            return {false, busy ? QStringLiteral("The library is in use by another program.") : error.text(), busy};
        }
        if (const auto existing = findByPcoPlan(s.pcoPlanId)) {
            db.rollback();
            return {false, QStringLiteral("This plan is already in the library as \"%1\".").arg(existing->title), false,
                    existing->title};
        }
    } else if (!db.transaction()) {
        return {false, db.lastError().text()};
    }
    auto fail = [&](const QString &error) {
        db.rollback();
        return Result{false, QStringLiteral("Couldn't save \"%1\": %2").arg(s.title, error)};
    };

    QString err = run(db,
        "INSERT INTO service (id, title, date, pco_plan_id, pco_service_type_id, pco_plan_url, last_imported_at, created_at, "
        "updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET title = excluded.title, date = excluded.date, "
        "pco_plan_id = excluded.pco_plan_id, pco_service_type_id = excluded.pco_service_type_id, pco_plan_url = excluded.pco_plan_url, "
        "last_imported_at = excluded.last_imported_at, updated_at = excluded.updated_at",
        {serviceId, s.title, s.date.isValid() ? s.date.toString(Qt::ISODate) : QStringLiteral(""), text(s.pcoPlanId),
         text(s.pcoServiceTypeId), text(s.pcoPlanUrl), timestamp(s.lastImportedAt), timestamp(s.created), timestamp(s.updated)});
    if (!err.isEmpty())
        return fail(err);

    {
        QSqlQuery q(db);
        q.prepare("SELECT id FROM service_item WHERE service_id = ?");
        q.addBindValue(serviceId);
        q.exec();
        QList<QUuid> gone;
        while (q.next()) {
            if (!itemIds.contains(uuid(q.value(0))))
                gone << uuid(q.value(0));
        }
        for (const QUuid &id : gone) {
            if (!(err = run(db, "DELETE FROM service_item WHERE id = ?", {key(id)})).isEmpty())
                return fail(err);
        }
    }

    for (int i = 0; i < s.items.size(); ++i) {
        const ServiceItem &it = s.items[i];
        err = run(db,
            "INSERT INTO service_item (id, service_id, position, kind, title, ref_id, arrangement_id, custom_sequence, "
            "body, service_position, pco_item_id, overrides, import_snapshot) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(id) DO UPDATE SET position = excluded.position, kind = excluded.kind, title = excluded.title, "
            "ref_id = excluded.ref_id, arrangement_id = excluded.arrangement_id, custom_sequence = excluded.custom_sequence, "
            "body = excluded.body, service_position = excluded.service_position, pco_item_id = excluded.pco_item_id, "
            "overrides = excluded.overrides, import_snapshot = excluded.import_snapshot",
            {key(it.id), serviceId, i, kindName(it.kind), text(it.title), key(it.ref), key(it.arrangement),
             json(it.customSequence), text(it.body), text(it.servicePosition), text(it.pcoItemId), json(it.overrides),
             json(it.importSnapshot)});
        if (!err.isEmpty())
            return fail(err);
    }

    if (!db.commit())
        return fail(db.lastError().text());
    service = s;
    return {};
}

std::optional<Service> ServiceStore::load(const QUuid &id) const
{
    QSqlDatabase db = m_library.database();
    QSqlQuery q(db);
    q.prepare("SELECT title, date, pco_plan_id, pco_service_type_id, last_imported_at, created_at, updated_at, "
              "pco_plan_url FROM service WHERE id = ?");
    q.addBindValue(key(id));
    if (!q.exec() || !q.next())
        return std::nullopt;

    Service s;
    s.id = id;
    s.title = q.value(0).toString();
    s.date = QDate::fromString(q.value(1).toString(), Qt::ISODate);
    s.pcoPlanId = q.value(2).toString();
    s.pcoServiceTypeId = q.value(3).toString();
    s.lastImportedAt = fromTimestamp(q.value(4));
    s.created = fromTimestamp(q.value(5));
    s.updated = fromTimestamp(q.value(6));
    s.pcoPlanUrl = q.value(7).toString();

    QSqlQuery items(db);
    items.prepare("SELECT id, kind, title, ref_id, arrangement_id, custom_sequence, body, service_position, "
                  "pco_item_id, overrides, import_snapshot FROM service_item WHERE service_id = ? ORDER BY position");
    items.addBindValue(key(id));
    items.exec();
    while (items.next()) {
        ServiceItem it;
        it.id = uuid(items.value(0));
        it.kind = kindFromName(items.value(1).toString()).value_or(ServiceItem::Kind::Placeholder);
        it.title = items.value(2).toString();
        it.ref = uuid(items.value(3));
        it.arrangement = uuid(items.value(4));
        it.customSequence = listFrom(items.value(5));
        it.body = items.value(6).toString();
        it.servicePosition = items.value(7).toString();
        it.pcoItemId = items.value(8).toString();
        it.overrides = objectFrom(items.value(9));
        it.importSnapshot = objectFrom(items.value(10));
        if (it.kind == ServiceItem::Kind::Song && !songExists(db, it.ref)) {
            // The song was deleted from the library: keep the place, ask for a song again.
            it.kind = ServiceItem::Kind::Placeholder;
            it.ref = QUuid();
            it.arrangement = QUuid();
        }
        s.items << it;
    }
    return s;
}

bool ServiceStore::remove(const QUuid &id)
{
    QSqlQuery q(m_library.database());
    q.prepare("DELETE FROM service WHERE id = ?");
    q.addBindValue(key(id));
    return q.exec() && q.numRowsAffected() > 0;
}

QList<ServiceSummary> ServiceStore::list() const
{
    QList<ServiceSummary> out;
    QSqlQuery q(m_library.database());
    q.exec("SELECT id, title, date FROM service ORDER BY date DESC, title COLLATE NOCASE");
    while (q.next())
        out << ServiceSummary{uuid(q.value(0)), q.value(1).toString(), QDate::fromString(q.value(2).toString(), Qt::ISODate)};
    return out;
}

std::optional<ServiceSummary> ServiceStore::findByPcoPlan(const QString &pcoPlanId) const
{
    if (pcoPlanId.isEmpty())
        return std::nullopt;
    QSqlQuery q(m_library.database());
    q.prepare("SELECT id, title, date FROM service WHERE pco_plan_id = ? ORDER BY created_at LIMIT 1");
    q.addBindValue(pcoPlanId);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return ServiceSummary{uuid(q.value(0)), q.value(1).toString(), QDate::fromString(q.value(2).toString(), Qt::ISODate)};
}

} // namespace visualis
