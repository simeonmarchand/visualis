#include "songstore.h"
#include "library/library.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace visualis {

namespace {

QString key(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }
// Qt binds a null QString as SQL NULL; text columns are NOT NULL, so bind empty text as ''.
QString text(const QString &s) { return s.isNull() ? QStringLiteral("") : s; }
QUuid uuid(const QVariant &v) { return QUuid::fromString(v.toString()); }
QString timestamp(const QDateTime &t) { return t.toUTC().toString(Qt::ISODateWithMs); }
QDateTime fromTimestamp(const QVariant &v) { return QDateTime::fromString(v.toString(), Qt::ISODateWithMs); }

QString slidesJson(const QStringList &slides)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(slides)).toJson(QJsonDocument::Compact));
}

QStringList slidesFromJson(const QVariant &v)
{
    QStringList out;
    for (const QJsonValue &s : QJsonDocument::fromJson(v.toString().toUtf8()).array())
        out << s.toString();
    return out;
}

// Runs one statement with positional values; returns the error text, or empty on success.
QString run(QSqlDatabase db, const QString &sql, const QVariantList &values, int *rowsAffected = nullptr)
{
    QSqlQuery q(db);
    if (!q.prepare(sql))
        return q.lastError().text();
    for (const QVariant &v : values)
        q.addBindValue(v);
    if (!q.exec())
        return q.lastError().text();
    if (rowsAffected)
        *rowsAffected = q.numRowsAffected();
    return {};
}

QList<QUuid> ids(QSqlDatabase db, const QString &sql, const QString &songId)
{
    QList<QUuid> out;
    QSqlQuery q(db);
    q.prepare(sql);
    q.addBindValue(songId);
    q.exec();
    while (q.next())
        out << uuid(q.value(0));
    return out;
}

bool ownedByAnotherSong(QSqlDatabase db, const char *table, const QUuid &id, const QString &songId)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE id = ? AND song_id <> ?").arg(QLatin1String(table)));
    q.addBindValue(key(id));
    q.addBindValue(songId);
    return q.exec() && q.next();
}

QString validate(const Song &song)
{
    if (song.title.trimmed().isEmpty())
        return QStringLiteral("A song needs a title.");
    QSet<QUuid> groupIds;
    for (const Group &g : song.groups) {
        if (groupIds.contains(g.id))
            return QStringLiteral("\"%1\" has two groups with the same id.").arg(song.title);
        groupIds.insert(g.id);
    }
    QSet<QUuid> arrangementIds;
    for (const Arrangement &a : song.arrangements) {
        if (arrangementIds.contains(a.id))
            return QStringLiteral("\"%1\" has two arrangements with the same id.").arg(song.title);
        arrangementIds.insert(a.id);
        for (const QUuid &step : a.sequence) {
            if (!groupIds.contains(step))
                return QStringLiteral("Arrangement \"%1\" of \"%2\" uses a group that isn't part of the song.")
                    .arg(a.name, song.title);
        }
    }
    if (!song.defaultArrangement.isNull() && !arrangementIds.contains(song.defaultArrangement))
        return QStringLiteral("\"%1\" has a default arrangement that isn't one of its arrangements.").arg(song.title);
    return {};
}

} // namespace

SongStore::SongStore(Library &library)
    : m_library(library)
{
}

SongStore::Result SongStore::save(Song &song)
{
    // Work on a copy so a failed save leaves the caller's song exactly as it was.
    Song s = song;
    if (s.id.isNull())
        s.id = QUuid::createUuid();
    for (Group &g : s.groups) {
        if (g.id.isNull())
            g.id = QUuid::createUuid();
    }
    for (Arrangement &a : s.arrangements) {
        if (a.id.isNull())
            a.id = QUuid::createUuid();
    }
    if (QString error = validate(s); !error.isEmpty())
        return {false, error};

    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!s.created.isValid())
        s.created = now;
    s.updated = now;

    QSqlDatabase db = m_library.database();
    const QString songId = key(s.id);

    // Group and arrangement ids must not belong to a different song already.
    for (const Group &g : s.groups) {
        if (ownedByAnotherSong(db, "song_group", g.id, songId))
            return {false, QStringLiteral("\"%1\" uses a group that belongs to another song.").arg(s.title)};
    }
    for (const Arrangement &a : s.arrangements) {
        if (ownedByAnotherSong(db, "arrangement", a.id, songId))
            return {false, QStringLiteral("\"%1\" uses an arrangement that belongs to another song.").arg(s.title)};
    }

    if (!db.transaction())
        return {false, db.lastError().text()};
    auto fail = [&](const QString &error) {
        db.rollback();
        return Result{false, QStringLiteral("Couldn't save \"%1\": %2").arg(s.title, error)};
    };

    QString err = run(db,
        "INSERT INTO song (id, title, ccli_number, authors, copyright, pco_song_id, default_arrangement, "
        "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET title = excluded.title, ccli_number = excluded.ccli_number, "
        "authors = excluded.authors, copyright = excluded.copyright, pco_song_id = excluded.pco_song_id, "
        "default_arrangement = excluded.default_arrangement, updated_at = excluded.updated_at",
        {songId, s.title, text(s.credits.ccliNumber), text(s.credits.authors), text(s.credits.copyright), text(s.pcoSongId),
         s.defaultArrangement.isNull() ? QVariant() : QVariant(key(s.defaultArrangement)),
         timestamp(s.created), timestamp(s.updated)});
    if (!err.isEmpty())
        return fail(err);

    // Groups and arrangements no longer in the song are deleted; foreign keys remove their steps.
    QSet<QUuid> keepGroups, keepArrangements;
    for (const Group &g : s.groups) keepGroups.insert(g.id);
    for (const Arrangement &a : s.arrangements) keepArrangements.insert(a.id);
    for (const QUuid &old : ids(db, "SELECT id FROM arrangement WHERE song_id = ?", songId)) {
        if (!keepArrangements.contains(old) && !(err = run(db, "DELETE FROM arrangement WHERE id = ?", {key(old)})).isEmpty())
            return fail(err);
    }
    for (const QUuid &old : ids(db, "SELECT id FROM song_group WHERE song_id = ?", songId)) {
        if (!keepGroups.contains(old) && !(err = run(db, "DELETE FROM song_group WHERE id = ?", {key(old)})).isEmpty())
            return fail(err);
    }

    for (int i = 0; i < s.groups.size(); ++i) {
        const Group &g = s.groups[i];
        err = run(db,
            "INSERT INTO song_group (id, song_id, position, label, color, slides) VALUES (?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(id) DO UPDATE SET position = excluded.position, label = excluded.label, "
            "color = excluded.color, slides = excluded.slides",
            {key(g.id), songId, i, text(g.label), text(g.color), slidesJson(g.slides)});
        if (!err.isEmpty())
            return fail(err);
    }
    for (int i = 0; i < s.arrangements.size(); ++i) {
        const Arrangement &a = s.arrangements[i];
        err = run(db,
            "INSERT INTO arrangement (id, song_id, position, name, pco_arrangement_id) VALUES (?, ?, ?, ?, ?) "
            "ON CONFLICT(id) DO UPDATE SET position = excluded.position, name = excluded.name, "
            "pco_arrangement_id = excluded.pco_arrangement_id",
            {key(a.id), songId, i, text(a.name), text(a.pcoArrangementId)});
        if (err.isEmpty())
            err = run(db, "DELETE FROM arrangement_step WHERE arrangement_id = ?", {key(a.id)});
        for (int step = 0; err.isEmpty() && step < a.sequence.size(); ++step)
            err = run(db, "INSERT INTO arrangement_step (arrangement_id, position, group_id) VALUES (?, ?, ?)",
                      {key(a.id), step, key(a.sequence[step])});
        if (!err.isEmpty())
            return fail(err);
    }

    if (!db.commit())
        return fail(db.lastError().text());
    song = s;
    return {};
}

std::optional<Song> SongStore::load(const QUuid &id) const
{
    QSqlDatabase db = m_library.database();
    const QString songId = key(id);
    QSqlQuery q(db);
    q.prepare("SELECT title, ccli_number, authors, copyright, pco_song_id, default_arrangement, created_at, "
              "updated_at FROM song WHERE id = ?");
    q.addBindValue(songId);
    if (!q.exec() || !q.next())
        return std::nullopt;

    Song s;
    s.id = id;
    s.title = q.value(0).toString();
    s.credits = {q.value(1).toString(), q.value(2).toString(), q.value(3).toString()};
    s.pcoSongId = q.value(4).toString();
    s.defaultArrangement = q.value(5).isNull() ? QUuid() : uuid(q.value(5));
    s.created = fromTimestamp(q.value(6));
    s.updated = fromTimestamp(q.value(7));

    QSqlQuery g(db);
    g.prepare("SELECT id, label, color, slides FROM song_group WHERE song_id = ? ORDER BY position");
    g.addBindValue(songId);
    g.exec();
    while (g.next())
        s.groups << Group{uuid(g.value(0)), g.value(1).toString(), g.value(2).toString(), slidesFromJson(g.value(3))};

    QSqlQuery a(db);
    a.prepare("SELECT id, name, pco_arrangement_id FROM arrangement WHERE song_id = ? ORDER BY position");
    a.addBindValue(songId);
    a.exec();
    while (a.next()) {
        Arrangement arr{uuid(a.value(0)), a.value(1).toString(), {}, a.value(2).toString()};
        QSqlQuery st(db);
        st.prepare("SELECT group_id FROM arrangement_step WHERE arrangement_id = ? ORDER BY position");
        st.addBindValue(key(arr.id));
        st.exec();
        while (st.next())
            arr.sequence << uuid(st.value(0));
        s.arrangements << arr;
    }
    return s;
}

bool SongStore::remove(const QUuid &id)
{
    int rows = 0;
    return run(m_library.database(), "DELETE FROM song WHERE id = ?", {key(id)}, &rows).isEmpty() && rows > 0;
}

QList<SongSummary> SongStore::list() const
{
    QList<SongSummary> out;
    QSqlQuery q(m_library.database());
    q.exec("SELECT id, title FROM song ORDER BY title COLLATE NOCASE, id");
    while (q.next())
        out << SongSummary{uuid(q.value(0)), q.value(1).toString()};
    return out;
}

} // namespace visualis
