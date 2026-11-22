#include "library/library.h"
#include "song/songjson.h"
#include "song/songlimits.h"
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

    // #65: the collision suffix used to be cut off by the length limit, so the loop never
    // found a free name and export hung.
    void longTitlesThatCollideExportAsSeparateFiles()
    {
        QTemporaryDir lib, out;
        auto library = Library::open(lib.path()).library;
        SongStore store(*library);
        const QString longTitle = QString(130, 'x');
        for (int i = 0; i < 3; ++i) {
            Song s = fixture("Amazing Grace");
            s.title = longTitle;
            QVERIFY(store.save(s).ok);
        }
        const SongExportReport exported = exportSongs(store, out.path());
        QVERIFY(exported.failed.isEmpty());
        QCOMPARE(exported.written.size(), 3);
        QCOMPARE(QSet<QString>(exported.written.begin(), exported.written.end()).size(), 3);
        QCOMPARE(QDir(out.path()).entryList({"*.visualis-song.json"}).size(), 3);
        QVERIFY(exported.written.contains(QString(120, 'x') + " (2).visualis-song.json"));
    }

    // Truncation never splits a character: an emoji straddling the limit is left out whole.
    void truncationKeepsCharactersWhole_data()
    {
        QTest::addColumn<QString>("title");
        const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");   // U+1F600, a surrogate pair
        QTest::newRow("emoji at the limit") << QString(118, 'x') + emoji + "tail";
        QTest::newRow("115 x and emoji") << QString(115, 'x') + emoji + emoji + emoji;
        QTest::newRow("all emoji") << emoji.repeated(40);
        QTest::newRow("accents") << QString::fromUtf8("\xC3\xA9").repeated(80);   // é, 2 bytes each
    }
    void truncationKeepsCharactersWhole()
    {
        QFETCH(QString, title);
        const QString name = songFileName(title);
        for (qsizetype i = 0; i < name.size(); ++i) {
            if (name[i].isHighSurrogate()) {
                QVERIFY2(i + 1 < name.size() && name[i + 1].isLowSurrogate(), qPrintable(name));
                ++i;   // the pair's second half
            } else {
                QVERIFY2(!name[i].isLowSurrogate(), qPrintable(name));
            }
        }
        QCOMPARE(QString::fromUtf8(name.toUtf8()), name);   // survives a UTF-8 round trip
        QVERIFY(name.toUtf8().size() <= 120 + 19);
        QVERIFY(title.startsWith(name.chopped(19)));
    }

    // Names are limited in bytes, not characters: long CJK and emoji titles, colliding, still
    // export (APFS and ext4 refuse names over 255 UTF-8 bytes).
    void wideCharacterTitlesExportWithinTheByteLimit_data()
    {
        QTest::addColumn<QString>("title");
        QTest::newRow("CJK") << QString::fromUtf8("\xE8\xAF\x97").repeated(130);   // 3 bytes each
        QTest::newRow("emoji") << QString(115, 'x') + QString::fromUtf8("\xF0\x9F\x98\x80").repeated(20);
    }
    void wideCharacterTitlesExportWithinTheByteLimit()
    {
        QFETCH(QString, title);
        QTemporaryDir lib, out;
        auto library = Library::open(lib.path()).library;
        SongStore store(*library);
        for (int i = 0; i < 3; ++i) {
            Song s = fixture("Amazing Grace");
            s.title = title;
            QVERIFY(store.save(s).ok);
        }
        const SongExportReport exported = exportSongs(store, out.path());
        QVERIFY2(exported.failed.isEmpty(), qPrintable(exported.failed.value(0).second));
        QCOMPARE(QDir(out.path()).entryList({"*.visualis-song.json"}).size(), 3);
        for (const QString &name : exported.written) {
            QVERIFY2(name.toUtf8().size() <= 255, qPrintable(name));
            QCOMPARE(QString::fromUtf8(name.toUtf8()), name);
        }
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

    // #77 review: a batch whose commit fails reports its songs failed, and files skipped as
    // duplicates of them too; skips for songs already in the library stand; order is kept.
    void importWriterReportsARolledBackBatchAsFailed()
    {
        QTemporaryDir lib;
        auto library = Library::open(lib.path()).library;
        SongStore store(*library);
        ImportWriter writer(store, ImportWriter::Duplicates::SkipSameTitle);
        writer.add({"first.json", fixture("Amazing Grace"), {}, {}, {}});
        const Song holy = fixture("Holy Holy Holy");
        {
            SongStore::Batch batch(store);
            writer.startBatch();
            writer.add({"b1.json", holy, {}, {}, {"a warning"}});
            writer.add({"b2.json", holy, {}, {}, {}});                      // a duplicate of b1
            writer.add({"notes.json", {}, "not a song", {}, {}});
            writer.add({"c.json", fixture("Amazing Grace"), {}, {}, {}});   // a duplicate of the library's
        }   // the commit never happened: rolled back
        writer.batchFailed("disk full");
        QCOMPARE(store.list().size(), 1);
        QCOMPARE(writer.report().imported, QStringList{"Amazing Grace"});
        QCOMPARE(writer.report().failed,
                 (QList<std::pair<QString, QString>>{{"b1.json", "disk full"}, {"b2.json", "disk full"}}));
        QCOMPARE(writer.report().skipped.size(), 2);
        QCOMPARE(writer.report().skipped[0].first, QStringLiteral("notes.json"));
        QCOMPARE(writer.report().skipped[1].first, QStringLiteral("c.json"));
        // What the parser noticed stands (#80); it was about the file, not the save.
        QCOMPARE(writer.report().warnings, QStringList{"b1.json: a warning"});
        writer.add({"again.json", holy, {}, {}, {}});   // its title is free again
        QCOMPARE(writer.report().imported.size(), 2);
    }

    // #80 with #77: "both are kept" goes when the batch that kept the song rolls back; the
    // parser's warnings for skipped and failed files stay.
    void importWriterDropsBothKeptForARolledBackSong()
    {
        QTemporaryDir lib;
        auto library = Library::open(lib.path()).library;
        SongStore store(*library);
        Song existing = fixture("Holy Holy Holy");
        QVERIFY(store.save(existing).ok);
        ImportWriter writer(store, ImportWriter::Duplicates::SkipSameIdWarnSameTitle);
        {
            SongStore::Batch batch(store);
            writer.startBatch();
            writer.add({"clash.json", fixture("Holy Holy Holy"), {}, {}, {"its own warning"}});
            QCOMPARE(writer.report().warnings.size(), 2);
            writer.add({"notes.json", {}, "not a song", {}, {"no groups"}});
            writer.add({"bad.json", {}, {}, "unreadable", {"odd header"}});
        }
        writer.batchFailed("disk full");
        // The song's own parser warning stays; only "both are kept" goes with the save.
        QCOMPARE(writer.report().warnings,
                 (QStringList{"clash.json: its own warning", "notes.json: no groups", "bad.json: odd header"}));
        QCOMPARE(writer.report().failed,
                 (QList<std::pair<QString, QString>>{{"clash.json", "disk full"}, {"bad.json", "unreadable"}}));
        QCOMPARE(store.list().size(), 1);
    }

    // #77 review: the operator can add, rename, or delete songs between batches; each batch
    // sees the library as it is.
    void importWriterSeesTitlesChangedBetweenBatches()
    {
        QTemporaryDir lib;
        auto library = Library::open(lib.path()).library;
        SongStore store(*library);
        ImportWriter pro(store, ImportWriter::Duplicates::SkipSameTitle);
        ImportWriter json(store, ImportWriter::Duplicates::SkipSameIdWarnSameTitle);

        Song operators = fixture("Holy Holy Holy");   // the operator pastes it mid-import
        QVERIFY(store.save(operators).ok);
        pro.startBatch();
        pro.add({"holy.pro", fixture("Holy Holy Holy"), {}, {}, {}});
        QCOMPARE(pro.report().skipped.size(), 1);
        json.startBatch();
        json.add({"holy.json", fixture("Holy Holy Holy"), {}, {}, {}});
        QCOMPARE(json.report().warnings.size(), 1);   // a different song with that title

        operators.title = "Holy (Short)";   // renamed...
        QVERIFY(store.save(operators).ok);
        for (const SongSummary &s : store.list()) {   // ...and the imported copy deleted
            if (s.id != operators.id)
                QVERIFY(store.remove(s.id));
        }
        pro.startBatch();
        pro.add({"holy again.pro", fixture("Holy Holy Holy"), {}, {}, {}});
        QCOMPARE(pro.report().imported, QStringList{"Holy, Holy, Holy"});
    }

    // #83: the worker's reader refuses a song over the limits, so it never reaches a save.
    void anOversizedSongIsSkippedWhenRead()
    {
        QTemporaryDir files, lib;
        Song huge = fixture("Amazing Grace");
        huge.title = "Huge";
        const Group verse = huge.groups[0];
        huge.groups.clear();
        for (int i = 0; i <= songlimits::kGroups; ++i) {
            Group g = verse;
            g.id = QUuid::createUuid();
            huge.groups << g;
        }
        for (Arrangement &a : huge.arrangements)
            a.sequence = {huge.groups[0].id};
        QFile f(files.filePath(songFileName(huge.title)));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(songToJson(huge));
        f.close();

        const ImportFile read = readSongFile(QFileInfo(f.fileName()));
        QVERIFY(!read.song);
        QVERIFY(read.error.isEmpty());
        QCOMPARE(read.skipped, QStringLiteral("too many groups: 501; the limit is 500"));

        auto library = Library::open(lib.path()).library;
        SongStore store(*library);
        const SongImportReport r = importSongs(files.path(), store);
        QVERIFY(r.imported.isEmpty());
        QCOMPARE(r.skipped, (QList<std::pair<QString, QString>>{{QFileInfo(f).fileName(), read.skipped}}));
        QVERIFY(store.list().isEmpty());
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
