#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

namespace visualis {

// One schema step. `statements` run in order inside a single transaction with the version bump,
// so a step either applies completely or not at all.
struct Migration {
    int version;
    QStringList statements;
};

// The church's library: a folder holding library.sqlite and media/ (plan 5.9). Every write
// commits immediately; there is no Save. Open it with Library::open().
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
    };

    struct OpenResult {
        std::unique_ptr<Library> library;  // null unless error == None
        OpenError error = OpenError::None;
        QString message;                   // for the operator: what's wrong and what was left alone
    };

    // Opens the library in `folder`, creating it if the folder doesn't exist or is empty,
    // and brings its schema up to date. Before migrating an existing library it saves a copy
    // next to it (library.sqlite.before-v<N>).
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

private:
    explicit Library(const QString &folder, const QString &connectionName);

    QString m_folder;
    QString m_connectionName;
};

} // namespace visualis
