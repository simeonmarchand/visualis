#include "library/library.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using visualis::Library;
using visualis::Migration;
using E = Library::OpenError;

namespace {

QVariant pragma(const Library &lib, const char *name)
{
    QSqlQuery q(lib.database());
    q.exec(QStringLiteral("PRAGMA %1").arg(name));
    return q.next() ? q.value(0) : QVariant();
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Writes a plain SQLite file that isn't a Visualis library.
void makeForeignDatabase(const QString &path)
{
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "foreign");
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery(db).exec("CREATE TABLE photos (id INTEGER PRIMARY KEY, name TEXT)");
        db.close();
    }
    QSqlDatabase::removeDatabase("foreign");
}

const std::vector<Migration> v1 = {{1, {"CREATE TABLE note (id INTEGER PRIMARY KEY, text TEXT NOT NULL)"}}};
const std::vector<Migration> v1v2 = {v1[0], {2, {"ALTER TABLE note ADD COLUMN pinned INTEGER NOT NULL DEFAULT 0"}}};

} // namespace

class LibraryTest : public QObject
{
    Q_OBJECT

private slots:
    void createsANewLibrary()
    {
        QTemporaryDir tmp;
        const QString folder = tmp.filePath("Church Library");   // doesn't exist yet
        auto r = Library::open(folder);
        QCOMPARE(r.error, E::None);
        QVERIFY(r.library);
        QVERIFY(QFileInfo(folder + "/library.sqlite").isFile());
        QVERIFY(QFileInfo(folder + "/media").isDir());
        QCOMPARE(r.library->schemaVersion(), Library::latestVersion());
        QCOMPARE(pragma(*r.library, "journal_mode").toString(), QStringLiteral("wal"));
        QCOMPARE(pragma(*r.library, "foreign_keys").toInt(), 1);
        QCOMPARE(pragma(*r.library, "synchronous").toInt(), 2);   // FULL
        QCOMPARE(pragma(*r.library, "application_id").toInt(), 0x5649534C);
    }

    void settingsPersistAcrossReopen()
    {
        QTemporaryDir tmp;
        {
            auto r = Library::open(tmp.path());
            QVERIFY(r.library->setSetting("church.name", "Grace Fellowship"));
            QVERIFY(r.library->setSetting("church.name", "Grace Church"));   // overwrite
            QCOMPARE(r.library->setting("missing", "fallback"), QStringLiteral("fallback"));
            QVERIFY(r.library->setSetting("empty", QString()));   // a null QString is stored as ""
            QCOMPARE(r.library->setting("empty", "fallback"), QString(""));
        }
        auto r = Library::open(tmp.path());
        QCOMPARE(r.error, E::None);
        QCOMPARE(r.library->setting("church.name"), QStringLiteral("Grace Church"));
    }

    void twoLibrariesOpenAtOnce()
    {
        QTemporaryDir a, b;
        auto ra = Library::open(a.path());
        auto rb = Library::open(b.path());
        ra.library->setSetting("k", "a");
        rb.library->setSetting("k", "b");
        QCOMPARE(ra.library->setting("k"), QStringLiteral("a"));
        QCOMPARE(rb.library->setting("k"), QStringLiteral("b"));
    }

    void migratesAnOlderLibraryAndKeepsACopy()
    {
        QTemporaryDir tmp;
        {
            auto r = Library::open(tmp.path(), v1);
            QSqlQuery(r.library->database()).exec("INSERT INTO note (text) VALUES ('keep me')");
        }
        auto r = Library::open(tmp.path(), v1v2);
        QCOMPARE(r.error, E::None);
        QCOMPARE(r.library->schemaVersion(), 2);
        QSqlQuery q(r.library->database());
        QVERIFY(q.exec("SELECT text, pinned FROM note"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("keep me"));
        QCOMPARE(q.value(1).toInt(), 0);
        QVERIFY(QFile::exists(tmp.filePath("library.sqlite.before-v2")));
    }

    void failedMigrationRollsBack()
    {
        QTemporaryDir tmp;
        {
            auto r = Library::open(tmp.path(), v1);
            QSqlQuery(r.library->database()).exec("INSERT INTO note (text) VALUES ('keep me')");
        }
        // Step 2 makes one good change, then fails: the good change must not survive.
        std::vector<Migration> broken = {v1[0], {2, {"CREATE TABLE half_done (x)", "THIS IS NOT SQL"}}};
        auto r = Library::open(tmp.path(), broken);
        QCOMPARE(r.error, E::MigrationFailed);
        QVERIFY(!r.library);
        QVERIFY2(r.message.contains("still at schema 1"), qPrintable(r.message));

        auto again = Library::open(tmp.path(), v1);
        QCOMPARE(again.error, E::None);
        QCOMPARE(again.library->schemaVersion(), 1);
        QSqlQuery q(again.library->database());
        QVERIFY(!q.exec("SELECT * FROM half_done"));
        QVERIFY(q.exec("SELECT text FROM note") && q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("keep me"));
    }

    void refusesANewerLibrary()
    {
        QTemporaryDir tmp;
        { Library::open(tmp.path(), v1v2); }
        auto r = Library::open(tmp.path(), v1);
        QCOMPARE(r.error, E::TooNew);
        auto again = Library::open(tmp.path(), v1v2);   // still usable by the newer version
        QCOMPARE(again.error, E::None);
    }

    void refusesAFileThatIsNotADatabase()
    {
        QTemporaryDir tmp;
        const QString db = tmp.filePath("library.sqlite");
        QFile f(db);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("These are not the bytes you're looking for.");
        f.close();
        const QByteArray before = readAll(db);

        auto r = Library::open(tmp.path());
        QCOMPARE(r.error, E::Corrupt);
        QCOMPARE(readAll(db), before);   // left unchanged
        QVERIFY(!QFile::exists(db + "-wal"));
    }

    void refusesAnotherProgramsDatabase()
    {
        QTemporaryDir tmp;
        makeForeignDatabase(tmp.filePath("library.sqlite"));
        auto r = Library::open(tmp.path());
        QCOMPARE(r.error, E::NotALibrary);
    }

    void detectsADamagedLibrary()
    {
        QTemporaryDir tmp;
        int rootPage = 0, pageSize = 0;
        {
            auto r = Library::open(tmp.path());
            QSqlDatabase db = r.library->database();
            db.transaction();
            for (int i = 0; i < 3000; ++i)
                r.library->setSetting(QStringLiteral("key-%1").arg(i), QString(60, QChar('x')));
            db.commit();
            // Find the settings table's own pages, so later tables can't move the target.
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT rootpage FROM sqlite_schema WHERE name = 'setting'") && q.next());
            rootPage = q.value(0).toInt();
            pageSize = pragma(*r.library, "page_size").toInt();
        }
        // Overwrite the table's whole root page. (Partial scribbles can land in a page's free
        // space and leave the data intact; SQLite is then right to call the file fine.)
        QVERIFY(rootPage > 1);
        QFile f(tmp.filePath("library.sqlite"));
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.seek(qint64(rootPage - 1) * pageSize);
        f.write(QByteArray(pageSize, '\xA5'));
        f.close();

        auto r = Library::open(tmp.path());
        QCOMPARE(r.error, E::Corrupt);
        QVERIFY2(r.message.contains("integrity"), qPrintable(r.message));
    }

    void refusesAFileInPlaceOfTheFolder()
    {
        QTemporaryDir tmp;
        QFile f(tmp.filePath("not-a-folder"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QCOMPARE(Library::open(f.fileName()).error, E::NotADirectory);
    }

    void reportsAFolderThatCannotBeCreated()
    {
        QCOMPARE(Library::open("/System/visualis-test-library").error, E::CannotCreate);
    }

    void closedLibraryIsOneCompleteFile()
    {
        QTemporaryDir tmp;
        {
            auto r = Library::open(tmp.path());
            r.library->setSetting("church.name", "Grace Church");
        }
        // Copy only library.sqlite, as a sync tool might, and the data must be in it.
        const QString copy = tmp.filePath("copy");
        QVERIFY(QDir().mkpath(copy));
        QVERIFY(QFile::copy(tmp.filePath("library.sqlite"), copy + "/library.sqlite"));
        auto r = Library::open(copy);
        QCOMPARE(r.error, E::None);
        QCOMPARE(r.library->setting("church.name"), QStringLiteral("Grace Church"));
    }

    // #90: the checkpoint on close is best-effort, so it must never wait on another
    // program that holds the database (it used to wait out the 5 s busy timeout).
    void closingNeverWaitsOnAnotherConnection_data()
    {
        QTest::addColumn<QString>("hold");
        QTest::newRow("write lock") << "BEGIN IMMEDIATE";
        QTest::newRow("read transaction") << "BEGIN";
    }

    void closingNeverWaitsOnAnotherConnection()
    {
        QFETCH(QString, hold);
        QTemporaryDir tmp;
        auto r = Library::open(tmp.path());
        QCOMPARE(r.error, E::None);
        r.library->setSetting("church.name", "Grace Church");
        {
            QSqlDatabase other = QSqlDatabase::addDatabase("QSQLITE", "other");
            other.setDatabaseName(tmp.filePath("library.sqlite"));
            QVERIFY(other.open());
            QSqlQuery q(other);
            QVERIFY(q.exec(hold));
            QVERIFY(q.exec("SELECT count(*) FROM sqlite_master"));   // starts the read
            q.finish();

            QElapsedTimer t;
            t.start();
            r.library.reset();
            QVERIFY2(t.elapsed() < 100, qPrintable(QStringLiteral("%1 ms").arg(t.elapsed())));

            QVERIFY(q.exec("ROLLBACK"));
            other.close();
        }
        QSqlDatabase::removeDatabase("other");

        auto again = Library::open(tmp.path());
        QCOMPARE(again.error, E::None);
        QCOMPARE(again.library->setting("church.name"), QStringLiteral("Grace Church"));
    }

    void openingTwiceSkipsTheBackup()
    {
        QTemporaryDir tmp;
        { Library::open(tmp.path()); }
        { Library::open(tmp.path()); }   // already current: no migration, so no copy
        const QStringList copies = QDir(tmp.path()).entryList({"library.sqlite.before-*"});
        QVERIFY2(copies.isEmpty(), qPrintable(copies.join(", ")));
    }
};

QTEST_GUILESS_MAIN(LibraryTest)
#include "tst_library.moc"
