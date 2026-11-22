#include "media/mediastore.h"
#include "library/library.h"
#include "media/mediafiles.h"

#include <QDir>
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
} // namespace

MediaStore::MediaStore(Library &library)
    : m_library(library)
{
}

MediaStore::Result MediaStore::save(MediaAsset &asset)
{
    if (asset.contentHash.isEmpty() || asset.file.isEmpty())
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
    if (!q.exec()) {
        if (q.lastError().nativeErrorCode() == QLatin1String("2067"))   // SQLITE_CONSTRAINT_UNIQUE
            return {false, QStringLiteral("This file is already in the library.")};
        return {false, q.lastError().text()};
    }
    if (!isNew && q.numRowsAffected() == 0)
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

QStringList MediaStore::usedBy(const QUuid &id) const
{
    QStringList out;
    QSqlQuery q(m_library.database());
    q.prepare("SELECT DISTINCT s.title FROM service_item i JOIN service s ON s.id = i.service_id "
              "WHERE i.kind = 'media' AND i.ref_id = ? ORDER BY s.date DESC, s.title");
    q.addBindValue(key(id));
    q.exec();
    while (q.next())
        out << q.value(0).toString();
    return out;
}

bool MediaStore::remove(const QUuid &id)
{
    const std::optional<MediaAsset> asset = load(id);
    if (!asset)
        return false;
    QSqlQuery q(m_library.database());
    q.prepare("DELETE FROM media_asset WHERE id = ?");
    q.addBindValue(key(id));
    if (!q.exec() || q.numRowsAffected() == 0)
        return false;
    // The row is the truth; a file left behind by a failed delete is harmless.
    QFile::remove(filePath(*asset));
    QFile::remove(thumbnailPath(*asset));
    return true;
}

QString MediaStore::filePath(const MediaAsset &asset) const
{
    return QDir(m_library.mediaPath()).filePath(asset.file);
}

QString MediaStore::thumbnailPath(const MediaAsset &asset) const
{
    return QDir(m_library.mediaPath()).filePath(thumbnailFile(asset.contentHash));
}

} // namespace visualis
