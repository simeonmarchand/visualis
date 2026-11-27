#include "media/mediastore.h"
#include "library/library.h"
#include "media/mediafiles.h"

#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace visualis {

namespace {
QString key(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }
QString text(const QString &s) { return s.isNull() ? QStringLiteral("") : s; }
QString timestamp(const QDateTime &t) { return t.toUTC().toString(Qt::ISODateWithMs); }

const char *kColumns = "id, content_hash, file, original_name, kind, width, height, duration_ms, frame_rate, codec, "
                       "created_at";

MediaAsset fromRow(const QSqlQuery &q)
{
    MediaAsset a;
    a.id = QUuid::fromString(q.value(0).toString());
    a.contentHash = q.value(1).toString();
    a.file = q.value(2).toString();
    if (!isValidMediaFile(a.file, a.contentHash))
        a.file.clear();   // never joined to the media folder
    a.originalName = q.value(3).toString();
    a.kind = q.value(4).toString() == QLatin1String("video") ? MediaAsset::Kind::Video : MediaAsset::Kind::Image;
    a.width = q.value(5).toInt();
    a.height = q.value(6).toInt();
    a.durationMs = q.value(7).toLongLong();
    a.frameRate = q.value(8).toDouble();
    a.codec = q.value(9).toString();
    a.created = QDateTime::fromString(q.value(10).toString(), Qt::ISODateWithMs);
    return a;
}

std::optional<MediaAsset> one(QSqlDatabase db, const QString &where, const QString &value)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT %1 FROM media_asset WHERE %2 = ?").arg(QLatin1String(kColumns), where));
    q.addBindValue(value);
    if (q.exec() && q.next())
        return fromRow(q);
    return std::nullopt;
}

// One statement that writes, without waiting on another program's lock (#88).
MediaStore::Result write(QSqlDatabase db, QSqlQuery &q, int *rowsAffected = nullptr)
{
    const Library::WriteNow w = Library::writeNow(db, q);
    if (rowsAffected)
        *rowsAffected = w.rowsAffected;
    switch (w.outcome) {
    case Library::WriteNow::Outcome::Ok:
        return {};
    case Library::WriteNow::Outcome::Busy:
        return {false, QStringLiteral("The library is in use by another program."), true};
    case Library::WriteNow::Outcome::UniqueViolation:
        return {false, QStringLiteral("This file is already in the library.")};
    case Library::WriteNow::Outcome::Failed:
        break;
    }
    return {false, w.error};
}
} // namespace

MediaStore::MediaStore(Library &library)
    : m_library(library)
{
}

MediaStore::Result MediaStore::save(MediaAsset &asset)
{
    if (!isValidMediaFile(asset.file, asset.contentHash))
        return {false, QStringLiteral("A media asset needs its file.")};
    const bool isNew = asset.id.isNull();
    MediaAsset saved = asset;
    if (isNew) {
        saved.id = QUuid::createUuid();
        saved.created = QDateTime::currentDateTimeUtc();
    }
    QSqlQuery q(m_library.database());
    q.prepare(isNew ? QStringLiteral("INSERT INTO media_asset (%1) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
                          .arg(QLatin1String(kColumns))
                    : QStringLiteral("UPDATE media_asset SET id = ?, content_hash = ?, file = ?, original_name = ?, "
                                     "kind = ?, width = ?, height = ?, duration_ms = ?, frame_rate = ?, codec = ?, "
                                     "created_at = ? WHERE id = ?"));
    for (const QVariant &v : QVariantList{key(saved.id), saved.contentHash, saved.file, text(saved.originalName),
                                          kindName(saved.kind), saved.width, saved.height, saved.durationMs,
                                          saved.frameRate, text(saved.codec), timestamp(saved.created)})
        q.addBindValue(v);
    if (!isNew)
        q.addBindValue(key(saved.id));
    int rows = 0;
    if (Result r = write(m_library.database(), q, &rows); !r.ok)
        return r;
    if (!isNew && rows == 0)
        return {false, QStringLiteral("This media is no longer in the library.")};
    asset = saved;
    return {};
}

std::optional<MediaAsset> MediaStore::load(const QUuid &id) const
{
    return one(m_library.database(), QStringLiteral("id"), key(id));
}

std::optional<MediaAsset> MediaStore::findByHash(const QString &hash) const
{
    return one(m_library.database(), QStringLiteral("content_hash"), hash);
}

QList<MediaAsset> MediaStore::list() const
{
    QList<MediaAsset> out;
    QSqlQuery q(m_library.database());
    q.exec(QStringLiteral("SELECT %1 FROM media_asset ORDER BY original_name COLLATE NOCASE, created_at")
               .arg(QLatin1String(kColumns)));
    while (q.next())
        out << fromRow(q);
    return out;
}

MediaStore::UsedBy MediaStore::usedBy(const QUuid &id) const
{
    // Ids are stored without braces, here and in the override JSON (overrides.h).
    UsedBy out;
    QSqlQuery q(m_library.database());
    q.prepare("SELECT s.title FROM service s WHERE s.default_background = :id OR EXISTS ("
              " SELECT 1 FROM service_item i WHERE i.service_id = s.id AND ("
              "  (i.kind = 'media' AND i.ref_id = :id)"
              "  OR CASE WHEN json_valid(i.overrides) THEN json_extract(i.overrides, '$.background') END = :id))"
              " ORDER BY s.date DESC, s.title");
    q.bindValue(":id", key(id));
    q.exec();
    while (q.next())
        out.services << q.value(0).toString();
    q.prepare("SELECT s.title FROM song s WHERE s.default_background = :id OR EXISTS ("
              " SELECT 1 FROM song_group g WHERE g.song_id = s.id AND g.default_background = :id)"
              " ORDER BY s.title COLLATE NOCASE");
    q.bindValue(":id", key(id));
    q.exec();
    while (q.next())
        out.songs << q.value(0).toString();
    return out;
}

bool MediaStore::isReferenced(const QString &file) const
{
    QSqlQuery q(m_library.database());
    q.prepare("SELECT 1 FROM media_asset WHERE file = ? LIMIT 1");
    q.addBindValue(file);
    return q.exec() && q.next();
}

std::optional<QSet<QString>> MediaStore::contentHashes() const
{
    QSqlQuery q(m_library.database());
    if (!q.exec("SELECT content_hash FROM media_asset"))
        return std::nullopt;
    QSet<QString> out;
    while (q.next())
        out.insert(q.value(0).toString());
    if (q.lastError().isValid())   // a read that stopped part way
        return std::nullopt;
    return out;
}

MediaStore::Result MediaStore::remove(const QUuid &id)
{
    const std::optional<MediaAsset> asset = load(id);
    if (!asset)
        return {false, QStringLiteral("This media is no longer in the library.")};
    QSqlQuery q(m_library.database());
    q.prepare("DELETE FROM media_asset WHERE id = ?");
    q.addBindValue(key(id));
    int rows = 0;
    if (Result r = write(m_library.database(), q, &rows); !r.ok)
        return r;
    if (rows == 0)
        return {false, QStringLiteral("This media is no longer in the library.")};
    // The row is the truth: a file left behind by a failed delete is harmless, and a file
    // another row still uses (hashes are unique, so only a hand-edited database) is kept.
    if (!asset->file.isEmpty() && !isReferenced(asset->file)) {
        if (const QString path = containedPath(m_library.mediaPath(), asset->file); !path.isEmpty())
            QFile::remove(path);
        if (const QString path = thumbnailPath(*asset); !path.isEmpty())
            QFile::remove(path);
    }
    return {};
}

QString MediaStore::thumbnailPath(const MediaAsset &asset) const
{
    if (!isValidHash(asset.contentHash))
        return {};
    return containedPath(m_library.mediaPath(), thumbnailFile(asset.contentHash));
}

} // namespace visualis
