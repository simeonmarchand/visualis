#include "library/library.h"

#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QElapsedTimer>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "childprocess.h"

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

    // This binary as a second Visualis process holding `folder`; true once it has it.
    static bool holdLibrary(QProcess &process, const QString &folder)
    {
        return childprocess::start(process, {"--hold-library", folder}, "locked").has_value();
    }

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

    // At merge time a PR's migration must be main's highest version + 1: a library opened
    // between a v6 and a later v5 would skip v5 forever.
    void theSchemaHistoryRunsOneToNWithoutGaps()
    {
        QCOMPARE(Library::migrationOrderError(Library::migrations()), QString());
        QCOMPARE(Library::migrations().back().version, Library::latestVersion());
    }

    void refusesAMigrationListOutOfOrder_data()
    {
        QTest::addColumn<QList<int>>("versions");
        QTest::addColumn<QString>("error");
        QTest::newRow("gap") << QList<int>{1, 3} << "step 2 is version 3, expected 2";
        QTest::newRow("duplicate") << QList<int>{1, 2, 2} << "step 3 is version 2, expected 3";
        QTest::newRow("swapped") << QList<int>{2, 1} << "step 1 is version 2, expected 1";
        QTest::newRow("not from 1") << QList<int>{2} << "step 1 is version 2, expected 1";
        QTest::newRow("zero") << QList<int>{0, 1} << "step 1 is version 0, expected 1";
        QTest::newRow("negative") << QList<int>{-1} << "step 1 is version -1, expected 1";
        QTest::newRow("empty") << QList<int>{} << "there are none";
    }
    void refusesAMigrationListOutOfOrder()
    {
        QFETCH(QList<int>, versions);
        QFETCH(QString, error);
        std::vector<Migration> list;
        for (int v : versions)
            list.push_back({v, {QStringLiteral("CREATE TABLE t%1 (x)").arg(v)}});
        QCOMPARE(Library::migrationOrderError(list), error);

        QTemporaryDir tmp;
        { QVERIFY(Library::open(tmp.path(), v1).library); }
        QTest::ignoreMessage(QtCriticalMsg, qPrintable("schema migrations out of order: " + error));
        auto r = Library::open(tmp.path(), list);
        QCOMPARE(r.error, E::MigrationFailed);
        QVERIFY(!r.library);
        QVERIFY2(r.message.contains(error), qPrintable(r.message));
        auto again = Library::open(tmp.path(), v1);   // untouched: no step ran, no copy was made
        QCOMPARE(again.library->schemaVersion(), 1);
        QVERIFY(QDir(tmp.path()).entryList({"library.sqlite.before-*"}, QDir::Files).isEmpty());

        // A folder that doesn't exist yet isn't created, nor its media/.
        const QString fresh = tmp.filePath("new library");
        QTest::ignoreMessage(QtCriticalMsg, qPrintable("schema migrations out of order: " + error));
        QCOMPARE(Library::open(fresh, list).error, E::MigrationFailed);
        QVERIFY(!QFileInfo::exists(fresh));
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

    // #109: one Visualis process per library. A second open is refused and touches nothing.
    void aSecondOpenIsRefusedAndChangesNothing()
    {
        QTemporaryDir tmp;
        auto first = Library::open(tmp.path(), v1);
        QVERIFY(first.library);
        QVERIFY(QSqlQuery(first.library->database()).exec("INSERT INTO note (text) VALUES ('keep me')"));
        const QString db = tmp.filePath("library.sqlite");
        QFile f(db);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        f.close();
        const QDateTime modified = QFileInfo(db).lastModified();
        const QStringList listing = QDir(tmp.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);

        // A newer schema, so a second open that got through would copy and migrate it.
        auto second = Library::open(tmp.path(), v1v2);
        QCOMPARE(second.error, E::InUse);
        QVERIFY(!second.library);
        QCOMPARE(second.message, QStringLiteral("This library is open in another Visualis window."));

        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), bytes);
        QCOMPARE(QFileInfo(db).lastModified(), modified);
        QCOMPARE(QDir(tmp.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), listing);
        QCOMPARE(first.library->schemaVersion(), 1);
        QSqlQuery q(first.library->database());
        QVERIFY(q.exec("SELECT text FROM note") && q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("keep me"));
    }

    void closingReleasesTheLibraryForTheNextOpen()
    {
        QTemporaryDir tmp;
        {
            auto first = Library::open(tmp.path(), v1);
            QVERIFY(first.library);
        }
        auto again = Library::open(tmp.path(), v1);
        QCOMPARE(again.error, E::None);
        QVERIFY(again.library);
    }

    // #109 with a real second process: one holding the library keeps this one out.
    void anotherVisualisProcessKeepsTheLibrary()
    {
        QTemporaryDir tmp;
        QProcess other;
        QVERIFY(holdLibrary(other, tmp.path()));
        auto r = Library::open(tmp.path());
        QCOMPARE(r.error, E::InUse);
        QCOMPARE(r.message, QStringLiteral("This library is open in another Visualis window."));
        other.kill();
        other.waitForFinished();
    }

    // #109: a crash leaves the lock file behind. The next start takes it over, or crash
    // recovery (#25) couldn't open the library.
    void aCrashedProcesssLockIsTakenOver()
    {
        QTemporaryDir tmp;
        QProcess crashed;
        QVERIFY(holdLibrary(crashed, tmp.path()));
        crashed.kill();   // SIGKILL: no destructors, no unlock
        QVERIFY(crashed.waitForFinished(5000));
        QVERIFY(QFile::exists(tmp.filePath("library.lock")));

        auto r = Library::open(tmp.path());
        QCOMPARE(r.error, E::None);
        QVERIFY(r.library);
    }

    // A library in a folder synced between Macs: the operator is told which Mac has it.
    // QLockFile can't see another host's processes, so this lock is never taken over.
    void aLockFromAnotherMacNamesIt()
    {
        QTemporaryDir tmp;
        { QVERIFY(Library::open(tmp.path(), v1).library); }
        QFile lock(tmp.filePath("library.lock"));   // what QLockFile writes on that Mac
        QVERIFY(lock.open(QIODevice::WriteOnly));
        lock.write("4242\nVisualis\nsanctuary-mac.local\n"
                   "11111111-2222-3333-4444-555555555555\n66666666-7777-8888-9999-000000000000\n");
        lock.close();

        auto r = Library::open(tmp.path(), v1);
        QCOMPARE(r.error, E::InUse);
        QCOMPARE(r.message, QStringLiteral("This library is open in Visualis on sanctuary-mac.local."));
        QVERIFY(lock.exists());   // left for that Mac
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

// With --hold-library <folder>, this binary stands in for a second Visualis process (#109):
// it opens the library and holds it until it's killed.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && qstrcmp(argv[1], "--hold-library") == 0) {
        auto r = Library::open(QString::fromLocal8Bit(argv[2]));
        if (!r.library) {
            std::fprintf(stderr, "%s\n", qPrintable(r.message));
            return 1;
        }
        return childprocess::readyUntilParentGoes("locked");
    }
    LibraryTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "tst_library.moc"
