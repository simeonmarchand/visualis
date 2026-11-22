#include "library/library.h"
#include "song/songjson.h"
#include "song/songstore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QtTest>

using namespace visualis;

namespace {

const QString fixtures = QStringLiteral(FIXTURES_DIR "/songs");

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

Song fixture(const QString &name)
{
    const SongJsonResult r = songFromJson(readFile(fixtures + "/" + name + ".visualis-song.json"));
    if (!r.song)
        qFatal("fixture %s: %s", qPrintable(name), qPrintable(r.error));
    return *r.song;
}

// A saved-looking song: every id and both timestamps set.
Song fullSong()
{
    Song s = fixture("Amazing Grace");
    s.id = QUuid::createUuid();
    s.credits.ccliNumber = "22025";
    s.pcoSongId = "31176997";
    s.arrangements[0].pcoArrangementId = "36584590";
    s.created = QDateTime(QDate(2026, 11, 21), QTime(9, 30, 15, 123), QTimeZone::UTC);
    s.updated = s.created.addSecs(3600);
    return s;
}

QString errorFor(const QByteArray &json)
{
    const SongJsonResult r = songFromJson(json);
    return r.song ? QString() : r.error;
}

} // namespace

class SongJsonTest : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsExactly()
    {
        const Song s = fullSong();
        const SongJsonResult r = songFromJson(songToJson(s));
        QVERIFY2(r.song, qPrintable(r.error));
        QCOMPARE(*r.song, s);
    }

    void isHumanReadable()
    {
        const QString json = QString::fromUtf8(songToJson(fullSong()));
        QVERIFY(json.contains("\"title\": \"Amazing Grace\""));
        QVERIFY(json.contains("\"label\": \"Verse 1\""));
        QVERIFY(json.contains("That saved a wretch like me"));
        QVERIFY(json.count('\n') > 20);   // indented, one field per line
    }

    void handWrittenFixturesUseLabels()
    {
        const Song s = fixture("Amazing Grace");
        QCOMPARE(s.groups.size(), 4);
        QVERIFY(!s.groups[0].id.isNull());   // generated
        QCOMPARE(s.arrangements[0].sequence.size(), 5);
        QCOMPARE(s.arrangements[0].sequence[1], s.groups[1].id);
        QCOMPARE(s.arrangements[0].sequence[4], s.groups[1].id);   // Verse 1 again
        QCOMPARE(s.defaultArrangement, s.arrangements[0].id);      // named "Sunday"
        QCOMPARE(s.credits.authors, QStringLiteral("John Newton"));

        const Song holy = fixture("Holy Holy Holy");
        QCOMPARE(holy.title, QStringLiteral("Holy, Holy, Holy"));
        QCOMPARE(holy.groups[2].slides[0], QString::fromUtf8("¡Santo, santo, santo!\nGrande, compasivo"));
        QVERIFY(holy.arrangements.isEmpty());
    }

    void fixturesSaveToTheLibrary()
    {
        QTemporaryDir tmp;
        auto lib = Library::open(tmp.path()).library;
        SongStore store(*lib);
        for (const QString &name : {"Amazing Grace", "Holy Holy Holy"}) {
            Song s = fixture(name);
            const SongStore::Result r = store.save(s);
            QVERIFY2(r.ok, qPrintable(r.error));
            QCOMPARE(*store.load(s.id), s);
        }
    }

    void unknownFieldsAreIgnored()
    {
        const QByteArray json = R"({"format": "visualis-song", "version": 1, "title": "T", "futureField": [1, 2],
                                    "groups": [{"label": "Verse", "slides": ["a"], "fontSize": 40}]})";
        const SongJsonResult r = songFromJson(json);
        QVERIFY2(r.song, qPrintable(r.error));
        QCOMPARE(r.song->groups[0].slides, QStringList{"a"});
    }

    void rejectsWhatItCannotRead_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("error");
        QTest::newRow("not json") << QByteArray("{title: oops") << "not valid JSON";
        QTest::newRow("array") << QByteArray("[]") << "not a Visualis song file";
        QTest::newRow("other format") << QByteArray(R"({"format": "propresenter", "version": 1, "title": "T"})") << "format is";
        QTest::newRow("no version") << QByteArray(R"({"format": "visualis-song", "title": "T"})") << "no valid version";
        QTest::newRow("newer") << QByteArray(R"({"format": "visualis-song", "version": 2, "title": "T"})") << "newer Visualis";
        QTest::newRow("no title") << QByteArray(R"({"format": "visualis-song", "version": 1, "title": "  "})") << "no title";
        QTest::newRow("unknown group") << QByteArray(R"({"format": "visualis-song", "version": 1, "title": "T",
            "groups": [{"label": "Verse 1"}], "arrangements": [{"name": "A", "sequence": ["Bridge"]}]})") << "no group \"Bridge\"";
        QTest::newRow("ambiguous label") << QByteArray(R"({"format": "visualis-song", "version": 1, "title": "T",
            "groups": [{"label": "Chorus"}, {"label": "Chorus"}], "arrangements": [{"name": "A", "sequence": ["Chorus"]}]})")
            << "more than one group";
        QTest::newRow("unknown default") << QByteArray(R"({"format": "visualis-song", "version": 1, "title": "T",
            "arrangements": [{"name": "A", "sequence": []}], "defaultArrangement": "B"})") << "default arrangement";
        QTest::newRow("duplicate ids") << QByteArray(R"({"format": "visualis-song", "version": 1, "title": "T",
            "groups": [{"id": "4c3a1c56-0b44-4b4f-9a7e-0d6f1b5d2a10"}, {"id": "4c3a1c56-0b44-4b4f-9a7e-0d6f1b5d2a10"}]})")
            << "two groups have the id";
    }
    void rejectsWhatItCannotRead()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, error);
        const QString got = errorFor(json);
        QVERIFY2(got.contains(error), qPrintable(QStringLiteral("got: '%1'").arg(got)));
    }

    void fileNames()
    {
        QCOMPARE(songFileName("Amazing Grace"), QStringLiteral("Amazing Grace.visualis-song.json"));
        QCOMPARE(songFileName("AC/DC: Live?"), QStringLiteral("AC-DC- Live-.visualis-song.json"));
        QCOMPARE(songFileName("  "), QStringLiteral("Untitled.visualis-song.json"));
        QCOMPARE(songFileName("..hidden"), QStringLiteral("hidden.visualis-song.json"));
        QVERIFY(songFileName(QString(300, 'x')).size() < 150);
    }

    void exportsAndImportsALibrary()
    {
        QTemporaryDir a, b, out;
        auto libA = Library::open(a.path()).library;
        SongStore storeA(*libA);
        QList<Song> songs = {fixture("Amazing Grace"), fixture("Holy Holy Holy"), fixture("Amazing Grace")};
        songs[2].title = "amazing grace";   // same title, different case: must not overwrite the first file
        for (Song &s : songs)
            QVERIFY(storeA.save(s).ok);

        const SongExportReport exported = exportSongs(storeA, out.path());
        QCOMPARE(exported.written.size(), 3);
        QVERIFY(exported.failed.isEmpty());
        QCOMPARE(QDir(out.path()).entryList({"*.visualis-song.json"}).size(), 3);

        auto libB = Library::open(b.path()).library;
        SongStore storeB(*libB);
        const SongImportReport imported = importSongs(out.path(), storeB);
        QCOMPARE(imported.imported.size(), 3);
        for (const Song &s : songs) {
            std::optional<Song> back = storeB.load(s.id);   // same ids
            QVERIFY(back);
            QCOMPARE(back->groups, s.groups);
            QCOMPARE(back->arrangements, s.arrangements);
            QCOMPARE(back->created, s.created);
        }

        // Importing again never overwrites.
        const SongImportReport again = importSongs(out.path(), storeB);
        QVERIFY(again.imported.isEmpty());
        QCOMPARE(again.skipped.size(), 3);
    }

    void importReportsBadFilesAndTitleClashes()
    {
        QTemporaryDir lib, in;
        auto library = Library::open(lib.path()).library;
        SongStore store(*library);
        Song existing = fixture("Holy Holy Holy");
        QVERIFY(store.save(existing).ok);

        QFile::copy(fixtures + "/Holy Holy Holy.visualis-song.json", in.filePath("Holy Holy Holy.visualis-song.json"));
        QFile bad(in.filePath("Broken.visualis-song.json"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ not json");
        bad.close();
        QFile other(in.filePath("notes.json"));
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.write("{}");
        other.close();

        const SongImportReport r = importSongs(in.path(), store);
        QCOMPARE(r.imported, QStringList{"Holy, Holy, Holy"});   // no id in the file, so it's a new song
        QCOMPARE(r.warnings.size(), 1);                          // ...whose title is already taken
        QCOMPARE(r.failed.size(), 1);
        QCOMPARE(r.failed[0].first, QStringLiteral("Broken.visualis-song.json"));
        QCOMPARE(store.list().size(), 2);
    }

    // #80 review: "both are kept" only for a song that was kept.
    void aTitleClashThatFailsToSaveIsNotReportedAsKept()
    {
        QTemporaryDir lib, in;
        auto library = Library::open(lib.path()).library;
        SongStore store(*library);
        Song existing = fixture("Holy Holy Holy");
        QVERIFY(store.save(existing).ok);

        // The fixture has no id, so it's a new song with a taken title. A read-only database
        // makes its save fail.
        QFile::copy(fixtures + "/Holy Holy Holy.visualis-song.json", in.filePath("Clash.visualis-song.json"));
        QVERIFY(QSqlQuery(library->database()).exec("PRAGMA query_only = ON"));

        const SongImportReport r = importSongs(in.path(), store);
        QVERIFY(r.imported.isEmpty());
        QCOMPARE(r.failed.size(), 1);
        QCOMPARE(r.failed[0].first, QStringLiteral("Clash.visualis-song.json"));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join('\n')));
        QCOMPARE(store.list().size(), 1);
    }

    // #69
    void importSkipsFilesTooLargeToBeSongsAndReportsProgress()
    {
        QTemporaryDir lib, in;
        auto library = Library::open(lib.path()).library;
        SongStore store(*library);
        QFile::copy(fixtures + "/Amazing Grace.visualis-song.json", in.filePath("Amazing Grace.visualis-song.json"));
        QFile huge(in.filePath("Huge.visualis-song.json"));
        QVERIFY(huge.open(QIODevice::WriteOnly));
        QVERIFY(huge.resize(kMaxSongFileBytes + 1));
        huge.close();

        QList<std::pair<int, int>> calls;
        const SongImportReport r = importSongs(in.path(), store, [&](int done, int total) {
            calls << std::pair{done, total};
            return true;
        });
        QCOMPARE(r.imported, QStringList{"Amazing Grace"});
        QCOMPARE(r.skipped.size(), 1);
        QCOMPARE(r.skipped[0].first, QStringLiteral("Huge.visualis-song.json"));
        QCOMPARE(calls, (QList<std::pair<int, int>>{{0, 2}, {1, 2}, {2, 2}}));
    }
};

QTEST_GUILESS_MAIN(SongJsonTest)
#include "tst_songjson.moc"
