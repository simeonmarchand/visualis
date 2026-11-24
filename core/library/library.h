#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

class QLockFile;
class QSqlQuery;

namespace visualis {

// One schema step. `statements` run in order inside a single transaction with the version bump,
// so a step either applies completely or not at all.
struct Migration {
    int version;
    QStringList statements;
};

// The church's library: a folder holding library.sqlite and media/ (plan 5.9). Every write
// commits immediately; there is no Save. Open it with Library::open().
//
// One Visualis process at a time (#109): an open Library holds library.lock in its folder, and
// a second process is refused. Everything built on Library may assume no other Visualis
// process is using the folder. That settles the #104 findings where two processes could delete
// each other's media (one importer unlinking a file the other just saved, the startup sweep
// removing an active import's staged copies) or write the same service. Other programs
// (sqlite3, backup tools) can still open the database, so a write can still find it locked.
class Library
{
public:
    enum class OpenError {
        None,
        NotADirectory,     // the path exists and is a file
        CannotCreate,      // the folder or database can't be created (permissions, disk)
        NotALibrary,       // a SQLite file, but not one Visualis made
        Corrupt,           // not a database, or the integrity check failed
        TooNew,            // made by a newer Visualis; opening it could lose data
        MigrationFailed,   // a schema step failed and was rolled back
        InUse,             // another Visualis process has it open (#109)
    };

    struct OpenResult {
        std::unique_ptr<Library> library;  // null unless error == None
        OpenError error = OpenError::None;
        QString message;                   // for the operator: what's wrong and what was left alone
    };

    // Opens the library in `folder`, creating it if the folder doesn't exist or is empty,
    // and brings its schema up to date. Before migrating an existing library it saves a copy
    // next to it (library.sqlite.before-v<N>). Refused with InUse, touching nothing, while
    // another Visualis process has it open; the lock of one that crashed is taken over.
    static OpenResult open(const QString &folder);
    // Same, with a given schema history. For tests.
    static OpenResult open(const QString &folder, const std::vector<Migration> &migrations);

    static const std::vector<Migration> &migrations();
    // Why `migrations` isn't versions 1, 2, …, N in order (N ≥ 1), or empty when it is. open() refuses
    // such a list: a library is migrated past every version at or below its own, so a gap
    // filled in later would be skipped on it forever.
    static QString migrationOrderError(const std::vector<Migration> &migrations);
    // How long a write waits for another connection's lock before failing.
    static constexpr int kBusyTimeoutMs = 5000;
    static int latestVersion();

    // One prepared statement run without waiting on another program's lock (#88), so a write
    // from the GUI thread can't hold up the operator window for the busy timeout. The timeout
    // is 0 while it runs and kBusyTimeoutMs again after. The SQLite codes are read only here.
    struct WriteNow {
        enum class Outcome { Ok, Busy, UniqueViolation, Failed };
        Outcome outcome = Outcome::Ok;
        QString error;          // the driver's text, unless Ok
        int rowsAffected = 0;   // read before the timeout is restored, which resets SQLite's count
        bool ok() const { return outcome == Outcome::Ok; }
        bool busy() const { return outcome == Outcome::Busy; }
    };
    static WriteNow writeNow(QSqlDatabase db, QSqlQuery &query);

    ~Library();
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;

    QString folder() const { return m_folder; }
    QString databasePath() const;
    QString mediaPath() const;
    int schemaVersion() const;
    QSqlDatabase database() const;

    // Library-wide settings (plan 5.9): church name, CCLI number, defaults. Writes commit at once.
    QString setting(const QString &key, const QString &fallback = {}) const;
    bool setSetting(const QString &key, const QString &value);

    // A transaction on the library's connection that rolls back unless commit() is called, so
    // every early return leaves the library as it was. With Lock::NowOrBusy it takes the write
    // lock up front without waiting: if another program holds it, ok() is false and busy() true,
    // so the caller can try again later instead of holding up the operator window (#76).
    class Transaction
    {
    public:
        enum class Lock { Wait, NowOrBusy };
        explicit Transaction(Library &library, Lock lock = Lock::Wait);
        ~Transaction();
        Transaction(const Transaction &) = delete;
        Transaction &operator=(const Transaction &) = delete;

        bool ok() const { return m_open; }
        bool busy() const { return m_busy; }
        QString error() const { return m_error; }
        // Empty when committed; otherwise why not, rolled back.
        QString commit();

    private:
        QSqlDatabase m_db;
        bool m_open = false;
        bool m_busy = false;
        QString m_error;
    };

private:
    Library(const QString &folder, const QString &connectionName, std::unique_ptr<QLockFile> lock);

    QString m_folder;
    QString m_connectionName;
    std::unique_ptr<QLockFile> m_lock;   // released after the destructor closes the connection
};

} // namespace visualis
