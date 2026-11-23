#include "library.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QSysInfo>
#include <QUuid>
#include <QVariant>

namespace visualis {

namespace {

Q_LOGGING_CATEGORY(lcLibrary, "visualis.library")

// Stamped into every library file (SQLite's application_id) so Visualis never migrates some
// other program's database by mistake. ASCII "VISL".
constexpr int kApplicationId = 0x5649534C;

const char *kDatabaseFile = "library.sqlite";
const char *kMediaFolder = "media";
const char *kLockFile = "library.lock";

struct Step {
    bool ok = true;
    QString error;
};

Step exec(QSqlDatabase &db, const QString &sql, QVariant *firstValue = nullptr)
{
    QSqlQuery q(db);
    if (!q.exec(sql))
        return {false, q.lastError().text()};
    if (firstValue)
        *firstValue = q.next() ? q.value(0) : QVariant();
    return {};
}

bool isSqliteFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.read(16) == QByteArray("SQLite format 3\0", 16);
}

Library::OpenResult failure(Library::OpenError error, const QString &message)
{
    Library::OpenResult r;
    r.error = error;
    r.message = message;
    return r;
}

// Why `lock` couldn't be taken.
Library::OpenResult lockFailure(const QLockFile &lock, const QString &root)
{
    using E = Library::OpenError;
    if (lock.error() != QLockFile::LockFailedError)
        return failure(E::CannotCreate, QStringLiteral("Can't lock the library folder %1.").arg(root));
    // A folder synced between Macs: say which one has it, since it's never taken over.
    QString host;
    if (lock.getLockInfo(nullptr, &host, nullptr) && !host.isEmpty() && host != QSysInfo::machineHostName())
        return failure(E::InUse, QStringLiteral("This library is open in Visualis on %1.").arg(host));
    return failure(E::InUse, QStringLiteral("This library is open in another Visualis window."));
}

// Opens or creates the database on an existing connection and migrates it. Returns an error
// result, or a result with error None and no library (the caller builds the Library).
Library::OpenResult prepare(const QString &connectionName, const QString &dbPath, bool existed,
                            const std::vector<Migration> &migrations)
{
    using E = Library::OpenError;
    QSqlDatabase db = QSqlDatabase::database(connectionName, false);
    if (!db.open())
        return failure(E::CannotCreate, QStringLiteral("Can't open %1: %2").arg(dbPath, db.lastError().text()));

    QVariant appId, version, tables;
    if (Step s = exec(db, "PRAGMA application_id", &appId); !s.ok)
        return failure(E::Corrupt, QStringLiteral("%1 can't be read as a database: %2").arg(dbPath, s.error));
    exec(db, "PRAGMA user_version", &version);
    exec(db, "SELECT count(*) FROM sqlite_schema", &tables);
    const bool empty = version.toInt() == 0 && tables.toInt() == 0;

    if (existed && !empty && appId.toInt() != kApplicationId)
        return failure(E::NotALibrary, QStringLiteral("%1 is a database from another program, not a Visualis library. "
                                                      "It was left unchanged.").arg(dbPath));
    if (existed && !empty) {
        QVariant result;
        Step s = exec(db, "PRAGMA integrity_check", &result);
        if (!s.ok || result.toString() != "ok")
            return failure(E::Corrupt, QStringLiteral("%1 failed its integrity check (%2). It was left unchanged; "
                                                      "restore it from a backup.")
                                           .arg(dbPath, s.ok ? result.toString() : s.error));
    }

    const int latest = migrations.empty() ? 0 : migrations.back().version;
    if (version.toInt() > latest)
        return failure(E::TooNew, QStringLiteral("This library was made by a newer Visualis (schema %1; this version "
                                                 "knows up to %2). It was left unchanged.")
                                      .arg(version.toInt()).arg(latest));

    // WAL lets the UI read while a write commits; FULL sync makes each commit survive power loss.
    QVariant mode;
    exec(db, "PRAGMA journal_mode = WAL", &mode);
    if (mode.toString().toLower() != "wal")
        return failure(E::CannotCreate, QStringLiteral("%1 can't use write-ahead logging (got '%2').")
                                            .arg(dbPath, mode.toString()));
    exec(db, "PRAGMA synchronous = FULL");
    exec(db, "PRAGMA foreign_keys = ON");
    if (empty)
        exec(db, QStringLiteral("PRAGMA application_id = %1").arg(kApplicationId));

    if (version.toInt() == latest)
        return {};

    if (!empty) {
        // A full copy before changing the schema, so a bad migration can always be undone.
        QString backup = dbPath + QStringLiteral(".before-v%1").arg(latest);
        for (int n = 2; QFile::exists(backup); ++n)
            backup = dbPath + QStringLiteral(".before-v%1-%2").arg(latest).arg(n);
        QSqlQuery q(db);
        q.prepare("VACUUM INTO ?");
        q.addBindValue(backup);
        if (!q.exec())
            return failure(E::CannotCreate, QStringLiteral("Couldn't save a copy before updating the library: %1. "
                                                           "The library was left unchanged.").arg(q.lastError().text()));
    }

    for (const Migration &m : migrations) {
        if (m.version <= version.toInt())
            continue;
        if (!db.transaction())
            return failure(E::MigrationFailed, db.lastError().text());
        Step s;
        for (const QString &sql : m.statements) {
            if (s = exec(db, sql); !s.ok)
                break;
        }
        if (s.ok)
            s = exec(db, QStringLiteral("PRAGMA user_version = %1").arg(m.version));
        if (!s.ok || !db.commit()) {
            const QString why = s.ok ? db.lastError().text() : s.error;
            db.rollback();
            QVariant now;
            exec(db, "PRAGMA user_version", &now);
            return failure(E::MigrationFailed, QStringLiteral("Updating the library to schema %1 failed: %2. "
                                                              "It was rolled back and is still at schema %3.")
                                                   .arg(m.version).arg(why).arg(now.toInt()));
        }
    }
    return {};
}

} // namespace

QString Library::migrationOrderError(const std::vector<Migration> &migrations)
{
    if (migrations.empty())
        return QStringLiteral("there are none");
    for (std::size_t i = 0; i < migrations.size(); ++i) {
        const int expected = int(i) + 1;
        if (migrations[i].version != expected)
            return QStringLiteral("step %1 is version %2, expected %3").arg(i + 1).arg(migrations[i].version).arg(expected);
    }
    return {};
}

const std::vector<Migration> &Library::migrations()
{
    // Append only. Never edit a step that has shipped: libraries in the field already ran it.
    static const std::vector<Migration> all = {
        {1, {"CREATE TABLE setting (key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID"}},
        // Songs (plan 5.2). Deleting a song removes its groups and arrangements; deleting a group
        // removes it from every arrangement, as in ProPresenter.
        {2, {"CREATE TABLE song ("
             " id TEXT PRIMARY KEY, title TEXT NOT NULL,"
             " ccli_number TEXT NOT NULL DEFAULT '', authors TEXT NOT NULL DEFAULT '',"
             " copyright TEXT NOT NULL DEFAULT '', pco_song_id TEXT NOT NULL DEFAULT '',"
             " default_arrangement TEXT REFERENCES arrangement(id) ON DELETE SET NULL DEFERRABLE INITIALLY DEFERRED,"
             " created_at TEXT NOT NULL, updated_at TEXT NOT NULL)",
             "CREATE INDEX song_title ON song(title COLLATE NOCASE)",
             "CREATE TABLE song_group ("
             " id TEXT PRIMARY KEY, song_id TEXT NOT NULL REFERENCES song(id) ON DELETE CASCADE,"
             " position INTEGER NOT NULL, label TEXT NOT NULL, color TEXT NOT NULL,"
             " slides TEXT NOT NULL)",   // JSON array of slide texts
             "CREATE INDEX song_group_song ON song_group(song_id, position)",
             "CREATE TABLE arrangement ("
             " id TEXT PRIMARY KEY, song_id TEXT NOT NULL REFERENCES song(id) ON DELETE CASCADE,"
             " position INTEGER NOT NULL, name TEXT NOT NULL, pco_arrangement_id TEXT NOT NULL DEFAULT '')",
             "CREATE INDEX arrangement_song ON arrangement(song_id, position)",
             "CREATE TABLE arrangement_step ("
             " arrangement_id TEXT NOT NULL REFERENCES arrangement(id) ON DELETE CASCADE,"
             " position INTEGER NOT NULL,"
             " group_id TEXT NOT NULL REFERENCES song_group(id) ON DELETE CASCADE,"
             " PRIMARY KEY (arrangement_id, position)) WITHOUT ROWID",
             "CREATE INDEX arrangement_step_group ON arrangement_step(group_id)"}},
        // Services (plan 5.2, #50). An item refers to what it shows by id (`ref_id`), not by a
        // foreign key, because it may be a song, media, or a presentation; a song item whose
        // song is gone loads as a placeholder (ServiceStore).
        {3, {"CREATE TABLE service ("
             " id TEXT PRIMARY KEY, title TEXT NOT NULL, date TEXT NOT NULL DEFAULT '',"
             " pco_plan_id TEXT NOT NULL DEFAULT '', pco_service_type_id TEXT NOT NULL DEFAULT '',"
             " last_imported_at TEXT NOT NULL DEFAULT '',"
             " created_at TEXT NOT NULL, updated_at TEXT NOT NULL)",
             "CREATE INDEX service_date ON service(date)",
             "CREATE TABLE service_item ("
             " id TEXT PRIMARY KEY, service_id TEXT NOT NULL REFERENCES service(id) ON DELETE CASCADE,"
             " position INTEGER NOT NULL, kind TEXT NOT NULL, title TEXT NOT NULL DEFAULT '',"
             " ref_id TEXT NOT NULL DEFAULT '', arrangement_id TEXT NOT NULL DEFAULT '',"
             " custom_sequence TEXT NOT NULL DEFAULT '[]',"   // JSON array of group labels
             " body TEXT NOT NULL DEFAULT '', service_position TEXT NOT NULL DEFAULT '',"
             " pco_item_id TEXT NOT NULL DEFAULT '',"
             " overrides TEXT NOT NULL DEFAULT '{}', import_snapshot TEXT NOT NULL DEFAULT '{}')",
             "CREATE INDEX service_item_service ON service_item(service_id, position)",
             "CREATE INDEX service_item_pco ON service_item(pco_item_id)"}},
        // The plan's link back to Planning Center, for "Open in Planning Center" (#54).
        {4, {"ALTER TABLE service ADD COLUMN pco_plan_url TEXT NOT NULL DEFAULT ''"}},
        // Templates (plan 5.4, #92). The body is the template's JSON (templatejson.h); name and
        // kind are copied out for listing. Built-in templates live in code, not here. A song's
        // default template is a plain id, not a foreign key: one that's gone falls back to the
        // built-in for its kind. Slide overrides are a JSON array parallel to the group's slides.
        {5, {"CREATE TABLE template ("
             " id TEXT PRIMARY KEY, name TEXT NOT NULL, kind TEXT NOT NULL, body TEXT NOT NULL,"
             " created_at TEXT NOT NULL, updated_at TEXT NOT NULL)",
             "CREATE INDEX template_name ON template(name COLLATE NOCASE)",
             "ALTER TABLE song ADD COLUMN default_template TEXT NOT NULL DEFAULT ''",
             "ALTER TABLE song_group ADD COLUMN slide_overrides TEXT NOT NULL DEFAULT '[]'"}},
        // Re-import (#56): items Planning Center took out stay, flagged, until the operator
        // removes them; imported items the operator deleted are remembered, so they stay deleted.
        {6, {"ALTER TABLE service_item ADD COLUMN pco_removed INTEGER NOT NULL DEFAULT 0",
             "CREATE TABLE service_removed_pco_item ("
             " service_id TEXT NOT NULL REFERENCES service(id) ON DELETE CASCADE,"
             " pco_item_id TEXT NOT NULL, import_snapshot TEXT NOT NULL DEFAULT '{}',"
             " PRIMARY KEY (service_id, pco_item_id))"}},
        // The media library (plan 5.9, #97). Files live in media/ under their SHA-256; the row
        // holds what probing found.
        {7, {"CREATE TABLE media_asset ("
             " id TEXT PRIMARY KEY, content_hash TEXT NOT NULL UNIQUE, file TEXT NOT NULL,"
             " original_name TEXT NOT NULL DEFAULT '', kind TEXT NOT NULL,"
             " width INTEGER NOT NULL DEFAULT 0, height INTEGER NOT NULL DEFAULT 0,"
             " duration_ms INTEGER NOT NULL DEFAULT 0, frame_rate REAL NOT NULL DEFAULT 0,"
             " codec TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL)"}},
    };
    return all;
}

int Library::latestVersion()
{
    return migrations().back().version;
}

Library::OpenResult Library::open(const QString &folder)
{
    return open(folder, migrations());
}

Library::OpenResult Library::open(const QString &folder, const std::vector<Migration> &migrations)
{
    // A bug in Visualis, not in the library: refuse before creating or touching anything.
    if (const QString bad = migrationOrderError(migrations); !bad.isEmpty()) {
        qCCritical(lcLibrary).noquote() << "schema migrations out of order:" << bad;
        return failure(OpenError::MigrationFailed,
                       QStringLiteral("This version of Visualis can't update libraries safely (its schema steps are "
                                      "out of order: %1). The library was left unchanged.").arg(bad));
    }
    const QFileInfo info(folder);
    if (info.exists() && !info.isDir())
        return failure(OpenError::NotADirectory, QStringLiteral("%1 is a file, not a library folder.").arg(folder));
    const QString root = info.absoluteFilePath();
    if (!QDir().mkpath(root))
        return failure(OpenError::CannotCreate, QStringLiteral("Can't create the library folder %1.").arg(root));
    // Before anything else in the folder is touched: one Visualis process per library (#109).
    auto lock = std::make_unique<QLockFile>(QDir(root).filePath(kLockFile));
    lock->setStaleLockTime(0);   // stale only when its process is gone, never by age
    if (!lock->tryLock(0))
        return lockFailure(*lock, root);
    if (!QDir(root).mkpath(kMediaFolder))
        return failure(OpenError::CannotCreate, QStringLiteral("Can't create the library folder %1.").arg(root));

    const QString dbPath = QDir(root).filePath(kDatabaseFile);
    const bool existed = QFileInfo(dbPath).size() > 0;
    if (existed && !isSqliteFile(dbPath))
        return failure(OpenError::Corrupt, QStringLiteral("%1 isn't a database file. It was left unchanged; "
                                                          "restore it from a backup.").arg(dbPath));

    const QString connection = "visualis-library-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(dbPath);
        // A write waits this long for another connection's transaction (an import on a
        // worker thread, #69) instead of failing at once.
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=%1").arg(kBusyTimeoutMs));
    }
    OpenResult r = prepare(connection, dbPath, existed, migrations);
    if (r.error != OpenError::None) {
        QSqlDatabase::database(connection, false).close();
        QSqlDatabase::removeDatabase(connection);
        return r;
    }
    r.library.reset(new Library(root, connection, std::move(lock)));
    return r;
}

Library::Library(const QString &folder, const QString &connectionName, std::unique_ptr<QLockFile> lock)
    : m_folder(folder)
    , m_connectionName(connectionName)
    , m_lock(std::move(lock))
{
}

Library::~Library()
{
    {
        // Fold the write-ahead log back into library.sqlite, so a closed library is one
        // complete file that can be copied or synced on its own. Best-effort: the WAL is
        // durable without it. So it never waits on another program holding the database;
        // closing runs on the GUI thread, and waiting froze it for the 5 s timeout (#90).
        QSqlDatabase db = database();
        exec(db, "PRAGMA busy_timeout = 0");
        QSqlQuery q(db);
        // One row: busy, frames in the log, frames checkpointed.
        if (!q.exec("PRAGMA wal_checkpoint(TRUNCATE)"))
            qCInfo(lcLibrary).noquote() << "closed without a checkpoint:" << q.lastError().text();
        else if (q.next() && q.value(0).toInt() != 0)
            qCInfo(lcLibrary) << "closed without a full checkpoint: another connection is using the library";
    }
    QSqlDatabase::database(m_connectionName, false).close();
    QSqlDatabase::removeDatabase(m_connectionName);
}

QString Library::databasePath() const
{
    return QDir(m_folder).filePath(kDatabaseFile);
}

QString Library::mediaPath() const
{
    return QDir(m_folder).filePath(kMediaFolder);
}

QSqlDatabase Library::database() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

int Library::schemaVersion() const
{
    QSqlDatabase db = database();
    QVariant v;
    exec(db, "PRAGMA user_version", &v);
    return v.toInt();
}

QString Library::setting(const QString &key, const QString &fallback) const
{
    QSqlQuery q(database());
    q.prepare("SELECT value FROM setting WHERE key = ?");
    q.addBindValue(key);
    return q.exec() && q.next() ? q.value(0).toString() : fallback;
}

bool Library::setSetting(const QString &key, const QString &value)
{
    QSqlQuery q(database());
    q.prepare("INSERT INTO setting (key, value) VALUES (?, ?) "
              "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    q.addBindValue(key);
    q.addBindValue(value.isNull() ? QStringLiteral("") : value);   // Qt binds a null QString as NULL
    return q.exec();
}

Library::Transaction::Transaction(Library &library, Lock lock)
    : m_db(library.database())
{
    if (lock == Lock::Wait) {
        m_open = m_db.transaction();
        if (!m_open)
            m_error = m_db.lastError().text();
        return;
    }
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA busy_timeout = 0"));
    m_open = q.exec(QStringLiteral("BEGIN IMMEDIATE"));
    const QSqlError error = q.lastError();
    q.exec(QStringLiteral("PRAGMA busy_timeout = %1").arg(kBusyTimeoutMs));
    if (!m_open) {
        m_busy = error.nativeErrorCode() == QLatin1String("5");   // SQLITE_BUSY
        m_error = m_busy ? QStringLiteral("The library is in use by another program.") : error.text();
    }
}

Library::Transaction::~Transaction()
{
    if (m_open)
        m_db.rollback();
}

QString Library::Transaction::commit()
{
    if (!m_open)
        return m_error.isEmpty() ? QStringLiteral("no transaction") : m_error;
    m_open = false;
    if (m_db.commit())
        return {};
    const QString error = m_db.lastError().text();
    m_db.rollback();
    return error;
}

} // namespace visualis
