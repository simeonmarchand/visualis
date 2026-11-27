#include "songstore.h"
#include "library/library.h"
#include "songlimits.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

QString overridesJson(const QList<QJsonObject> &overrides)
{
    QJsonArray a;
    for (const QJsonObject &o : overrides)
        a.append(o);
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

QList<QJsonObject> overridesFromJson(const QVariant &v)
{
    QList<QJsonObject> out;
    for (const QJsonValue &o : QJsonDocument::fromJson(v.toString().toUtf8()).array())
        out << o.toObject();
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

// The song's slide overrides as the library has them now, so a write can carry one stored
// before the limits (#95) without anything writing a new one.
StoredOverrides storedOverrides(QSqlDatabase db, const QUuid &songId)
{
    StoredOverrides out;
    if (songId.isNull())
        return out;
    QSqlQuery q(db);
    q.prepare("SELECT slide_overrides FROM song_group WHERE song_id = ?");
    q.addBindValue(key(songId));
    q.exec();
    while (q.next()) {
        for (const QJsonObject &o : overridesFromJson(q.value(0))) {
            if (o.isEmpty())
                continue;
            const QByteArray bytes = QJsonDocument(o).toJson(QJsonDocument::Compact);
            out.total += bytes.size();
            out.oversized.insert(bytes);
        }
    }
    return out;
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
    // Before anything else, so no caller (paste, the editor, an import) can make this thread
    // write an unbounded song (#83).
    if (QString tooBig = songLimitError(song); !tooBig.isEmpty())
        return {false, tooBig};
    if (QString tooBig = overrideLimitError(song, storedOverrides(m_library.database(), song.id)); !tooBig.isEmpty())
        return {false, tooBig};
    // Work on a copy so a failed save leaves the caller's song exactly as it was.
    Song s = song;
    if (s.id.isNull())
        s.id = QUuid::createUuid();
    for (Group &g : s.groups) {
        if (g.id.isNull())
            g.id = QUuid::createUuid();
        g.fitSlideOverrides();   // exactly one per slide, whatever the caller left (#92)
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

    // A savepoint, not BEGIN: alone it is a transaction of its own; inside a Batch it nests,
    // so one song that fails rolls back without losing the others.
    if (QString err = run(db, "SAVEPOINT song_save", {}); !err.isEmpty())
        return {false, err};
    auto fail = [&](const QString &error) {
        run(db, "ROLLBACK TO song_save", {});
        run(db, "RELEASE song_save", {});
        return Result{false, QStringLiteral("Couldn't save \"%1\": %2").arg(s.title, error)};
    };

    QString err = run(db,
        "INSERT INTO song (id, title, ccli_number, authors, copyright, pco_song_id, default_arrangement, "
        "default_template, default_background, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET title = excluded.title, ccli_number = excluded.ccli_number, "
        "authors = excluded.authors, copyright = excluded.copyright, pco_song_id = excluded.pco_song_id, "
        "default_arrangement = excluded.default_arrangement, default_template = excluded.default_template, "
        "default_background = excluded.default_background, updated_at = excluded.updated_at",
        {songId, s.title, text(s.credits.ccliNumber), text(s.credits.authors), text(s.credits.copyright), text(s.pcoSongId),
         s.defaultArrangement.isNull() ? QVariant() : QVariant(key(s.defaultArrangement)),
         s.defaultTemplate.isNull() ? QStringLiteral("") : key(s.defaultTemplate),
         s.defaultBackground.isNull() ? QStringLiteral("") : key(s.defaultBackground),
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
            "INSERT INTO song_group (id, song_id, position, label, color, slides, slide_overrides, default_background) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(id) DO UPDATE SET position = excluded.position, label = excluded.label, "
            "color = excluded.color, slides = excluded.slides, slide_overrides = excluded.slide_overrides, "
            "default_background = excluded.default_background",
            {key(g.id), songId, i, text(g.label), text(g.color), slidesJson(g.slides), overridesJson(g.slideOverrides),
             g.defaultBackground.isNull() ? QStringLiteral("") : key(g.defaultBackground)});
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

    if (const QString released = run(db, "RELEASE song_save", {}); !released.isEmpty())
        return fail(released);
    song = s;
    return {};
}

SongStore::Result SongStore::saveAll(QList<Song> &songs, Library::Transaction::Lock lock)
{
    Library::Transaction t(m_library, lock);
    if (!t.ok())
        return {false, t.error(), t.busy()};
    QList<Song> saved = songs;   // save() updates its argument before the commit
    for (Song &s : saved) {
        if (Result r = save(s); !r.ok)
            return r;
    }
    if (const QString committed = t.commit(); !committed.isEmpty())
        return {false, QStringLiteral("Couldn't save: %1").arg(committed)};
    songs = saved;
    return {};
}

SongStore::Result SongStore::saveSlideOverrides(Song &song, Library::Transaction::Lock lock)
{
    if (QString tooBig = songLimitError(song); !tooBig.isEmpty())
        return {false, tooBig};
    for (Group &g : song.groups)
        g.fitSlideOverrides();   // the same song: fitted overrides compare equal
    Library::Transaction t(m_library, lock);
    if (!t.ok())
        return {false, t.error(), t.busy()};
    // Under the lock, against what's stored now.
    if (QString tooBig = overrideLimitError(song, storedOverrides(m_library.database(), song.id)); !tooBig.isEmpty())
        return {false, tooBig};
    QSqlDatabase db = m_library.database();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    auto fail = [&](const QString &error) { return Result{false, QStringLiteral("Couldn't save \"%1\": %2").arg(song.title, error)}; };
    int rows = 0;
    if (QString err = run(db, "UPDATE song SET updated_at = ? WHERE id = ?", {timestamp(now), key(song.id)}, &rows); !err.isEmpty())
        return fail(err);
    if (rows != 1)
        return {false, QStringLiteral("\"%1\" isn't in the library.").arg(song.title)};
    for (const Group &g : song.groups) {
        // Only while its slides are the ones the overrides were made for.
        if (QString err = run(db, "UPDATE song_group SET slide_overrides = ? WHERE id = ? AND song_id = ? AND slides = ?",
                              {overridesJson(g.slideOverrides), key(g.id), key(song.id), slidesJson(g.slides)}, &rows);
            !err.isEmpty())
            return fail(err);
        if (rows != 1)
            return {false, QStringLiteral("\"%1\" changed in the library since it was opened; open it again.").arg(song.title)};
    }
    if (QString err = t.commit(); !err.isEmpty())
        return fail(err);
    song.updated = now;
    return {};
}

SongStore::Result SongStore::updatePcoLink(const Song &song)
{
    QSqlDatabase db = m_library.database();
    // A savepoint, as in save(): nests inside the caller's transaction.
    if (QString err = run(db, "SAVEPOINT pco_link", {}); !err.isEmpty())
        return {false, err};
    int rows = 0;
    QString err = run(db, "UPDATE song SET pco_song_id = ?, ccli_number = ?, authors = ?, copyright = ? WHERE id = ?",
                      {text(song.pcoSongId), text(song.credits.ccliNumber), text(song.credits.authors),
                       text(song.credits.copyright), key(song.id)},
                      &rows);
    if (err.isEmpty() && rows != 1)
        err = QStringLiteral("\"%1\" isn't in the library.").arg(song.title);
    if (err.isEmpty())
        err = run(db, "RELEASE pco_link", {});
    if (!err.isEmpty()) {
        run(db, "ROLLBACK TO pco_link", {});
        run(db, "RELEASE pco_link", {});
        return {false, err};
    }
    return {};
}

SongStore::Batch::Batch(SongStore &store)
    : m_db(store.m_library.database())
{
    m_open = m_db.transaction();
}

SongStore::Batch::~Batch()
{
    if (m_open)
        m_db.rollback();
}

SongStore::Result SongStore::Batch::commit()
{
    if (!m_open)
        return {false, m_db.lastError().text()};
    m_open = false;
    if (!m_db.commit()) {
        const QString error = m_db.lastError().text();
        m_db.rollback();
        return {false, error};
    }
    return {};
}

std::optional<Song> SongStore::load(const QUuid &id) const
{
    QSqlDatabase db = m_library.database();
    const QString songId = key(id);
    QSqlQuery q(db);
    q.prepare("SELECT title, ccli_number, authors, copyright, pco_song_id, default_arrangement, created_at, "
              "updated_at, default_template, default_background FROM song WHERE id = ?");
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
    s.defaultTemplate = uuid(q.value(8));
    s.defaultBackground = uuid(q.value(9));

    QSqlQuery g(db);
    g.prepare("SELECT id, label, color, slides, slide_overrides, default_background FROM song_group WHERE song_id = ? ORDER BY position");
    g.addBindValue(songId);
    g.exec();
    while (g.next()) {
        Group group{uuid(g.value(0)), g.value(1).toString(), g.value(2).toString(), slidesFromJson(g.value(3))};
        group.slideOverrides = overridesFromJson(g.value(4));
        group.fitSlideOverrides();
        group.defaultBackground = uuid(g.value(5));
        s.groups << group;
    }

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

QList<SongCandidate> SongStore::matchCandidates() const
{
    QList<SongCandidate> out;
    QSqlQuery q(m_library.database());
    q.exec("SELECT id, title, ccli_number, authors, pco_song_id FROM song ORDER BY title COLLATE NOCASE, id");
    while (q.next())
        out << SongCandidate{uuid(q.value(0)), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
                             q.value(4).toString()};
    return out;
}

QStringList SongStore::titles() const
{
    QStringList out;
    QSqlQuery q(m_library.database());
    q.setForwardOnly(true);
    q.exec("SELECT title FROM song");
    while (q.next())
        out << q.value(0).toString();
    return out;
}

} // namespace visualis
