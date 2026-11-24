#include "templatestore.h"
#include "templatejson.h"
#include "library/library.h"

#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <limits>

namespace visualis {

namespace {

QString key(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }

QString validate(const Template &t)
{
    if (t.name.trimmed().isEmpty())
        return QStringLiteral("A template needs a name.");
    if (t.regions.size() > templatelimits::kRegions)
        return QStringLiteral("\"%1\" has %2 regions; the limit is %3.").arg(t.name).arg(t.regions.size()).arg(templatelimits::kRegions);
    if (t.name.trimmed().size() > templatelimits::kTextLength)
        return QStringLiteral("The template's name is longer than %1 characters.").arg(templatelimits::kTextLength);
    if (t.revision < 0 || t.revision == std::numeric_limits<int>::max())
        return QStringLiteral("\"%1\" has an invalid revision; duplicate it instead.").arg(t.name);
    // The same rules templateFromJson reads ids with, so a saved region never loads renamed or skipped.
    QSet<QString> ids;
    for (const TextRegion &r : t.regions) {
        const QString id = r.id.trimmed();
        if (id.isEmpty())
            return QStringLiteral("\"%1\" has a region without an id.").arg(t.name);
        if (id != r.id)
            return QStringLiteral("\"%1\" has a region id with spaces around it (\"%2\").").arg(t.name, r.id);
        if (id.size() > templatelimits::kTextLength)
            return QStringLiteral("\"%1\" has a region id longer than %2 characters.").arg(t.name).arg(templatelimits::kTextLength);
        if (ids.contains(id))
            return QStringLiteral("\"%1\" has two regions with the id \"%2\".").arg(t.name, id);
        ids.insert(id);
    }
    return {};
}

// One statement that writes, without waiting on another program's lock (#88).
TemplateStore::Result write(Library &library, QSqlQuery &q, int *rowsAffected = nullptr)
{
    const Library::WriteNow w = Library::writeNow(library.database(), q);
    if (rowsAffected)
        *rowsAffected = w.rowsAffected;
    if (w.busy())
        return {false, QStringLiteral("the library is in use by another program"), true};
    return {w.ok(), w.error};
}

} // namespace

TemplateStore::TemplateStore(Library &library)
    : m_library(library)
{
}

TemplateStore::Result TemplateStore::save(Template &tmpl, Lock lock)
{
    if (isBuiltInTemplate(tmpl.id))
        return {false, QStringLiteral("\"%1\" is built in and can't be changed; duplicate it instead.").arg(tmpl.name)};
    Template t = tmpl;   // a failed save leaves the caller's template as it was
    if (t.id.isNull())
        t.id = QUuid::createUuid();
    if (QString error = validate(t); !error.isEmpty())
        return {false, error};
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!t.created.isValid())
        t.created = now;
    t.updated = now;
    ++t.revision;

    QByteArray body = templateToJson(t);
    if (body.size() > templatelimits::kBytes)
        return {false, QStringLiteral("\"%1\" is too large to save.").arg(t.name)};
    // A value the reader would replace isn't saved. What it only tidies (trimmed text,
    // lowercase colors) is, and the caller gets the tidied template: the one that loads back.
    const TemplateJsonResult back = templateFromJson(body);
    if (!back.tmpl || !back.warnings.isEmpty())
        return {false, QStringLiteral("Couldn't save \"%1\": %2").arg(t.name, back.tmpl ? back.warnings.first() : back.error)};
    t = *back.tmpl;
    body = templateToJson(t);   // stored tidied too

    QSqlQuery q(m_library.database());
    q.prepare("INSERT INTO template (id, name, kind, body, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?) "
              "ON CONFLICT(id) DO UPDATE SET name = excluded.name, kind = excluded.kind, body = excluded.body, "
              "updated_at = excluded.updated_at");
    q.addBindValue(key(t.id));
    q.addBindValue(t.name);
    q.addBindValue(templateKindName(t.kind));
    q.addBindValue(QString::fromUtf8(body));
    q.addBindValue(t.created.toString(Qt::ISODateWithMs));
    q.addBindValue(t.updated.toString(Qt::ISODateWithMs));
    const Result r = lock == Lock::Wait ? Result{q.exec(), q.lastError().text()} : write(m_library, q);
    if (!r.ok)
        return {false, QStringLiteral("Couldn't save \"%1\": %2").arg(t.name, r.error), r.busy};
    tmpl = t;
    return {};
}

std::optional<Template> TemplateStore::load(const QUuid &id, QStringList *warnings) const
{
    if (std::optional<Template> builtIn = builtInTemplate(id))
        return builtIn;
    QSqlQuery q(m_library.database());
    q.prepare("SELECT body FROM template WHERE id = ?");
    q.addBindValue(key(id));
    if (!q.exec() || !q.next())
        return std::nullopt;
    TemplateJsonResult r = templateFromJson(q.value(0).toString().toUtf8());
    if (!r.tmpl) {
        if (warnings)
            *warnings << r.error;
        return std::nullopt;
    }
    if (warnings)
        *warnings << r.warnings;
    r.tmpl->id = id;   // the row's id is the one everything refers to
    return r.tmpl;
}

TemplateStore::Result TemplateStore::remove(const QUuid &id)
{
    if (isBuiltInTemplate(id))
        return {false, QStringLiteral("A built-in template can't be deleted.")};
    QSqlQuery q(m_library.database());
    q.prepare("DELETE FROM template WHERE id = ?");
    q.addBindValue(key(id));
    int rows = 0;
    if (const Result r = write(m_library, q, &rows); !r.ok)
        return {false, QStringLiteral("Couldn't delete the template: %1").arg(r.error), r.busy};
    if (rows == 0)
        return {false, QStringLiteral("That template is no longer in the library.")};
    return {};
}

QList<TemplateSummary> TemplateStore::list() const
{
    QList<TemplateSummary> out;
    for (const Template &t : builtInTemplates())
        out << TemplateSummary{t.id, t.name, t.kind, true};
    QSqlQuery q(m_library.database());
    q.exec("SELECT id, name, kind FROM template ORDER BY name COLLATE NOCASE, id");
    while (q.next()) {
        const QUuid id = QUuid::fromString(q.value(0).toString());
        if (isBuiltInTemplate(id))
            continue;   // a row can't shadow a built-in
        out << TemplateSummary{id, q.value(1).toString(),
                               templateKindFromName(q.value(2).toString()).value_or(TemplateKind::Lyrics), false};
    }
    return out;
}

} // namespace visualis
