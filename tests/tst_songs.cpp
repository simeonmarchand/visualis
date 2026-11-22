#include "library/library.h"
#include "song/song.h"
#include "song/songstore.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace visualis;

namespace {

Group group(const QString &label, const QStringList &slides, const QString &color = {})
{
    return Group{QUuid::createUuid(), label, color, slides};
}

// A song shaped like the church's library: colored groups, multi-line slides, a blank
// instrumental group, and two arrangements that repeat groups.
Song sampleSong()
{
    Song s;
    s.title = QStringLiteral("I Thank God");
    s.credits = {"7169680", "Aaron Moses, Chuck Butler", "Bethel Worship Publishing"};
    s.pcoSongId = "31176997";
    s.groups = {
        group("Intro", {""}, "#b3a624"),
        group("Verse 1", {"Wandering into the night\nWanting a place to hide", "This weary soul\nThis bag of bones"}, "#0078cc"),
        group("Chorus", {"You picked me up turned me around\nPlaced my feet on solid ground"}, "#cc004f"),
        group("Puente", {"¡Él me levantó!"}),   // non-ASCII, no color
    };
    const auto &g = s.groups;
    s.arrangements = {
        Arrangement{QUuid::createUuid(), "Maverick City", {g[0].id, g[1].id, g[2].id, g[2].id}, "36584590"},
        Arrangement{QUuid::createUuid(), "Short", {g[1].id, g[2].id}, {}},
    };
    s.defaultArrangement = s.arrangements[1].id;
    return s;
}

int count(Library &lib, const QString &sql)
{
    QSqlQuery q(lib.database());
    return q.exec(sql) && q.next() ? q.value(0).toInt() : -1;
}

} // namespace

// Fails with the store's own message, so a failed save says why.
#define SAVE_OK(store, song)                                    \
    do {                                                        \
        const SongStore::Result r_ = (store).save(song);        \
        QVERIFY2(r_.ok, qPrintable(r_.error));                  \
    } while (false)

class SongsTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    std::unique_ptr<Library> m_lib;

private slots:
    void init()
    {
        QVERIFY(m_tmp.isValid());
        m_lib = Library::open(m_tmp.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces))).library;
        QVERIFY(m_lib);
    }
    void cleanup() { m_lib.reset(); }

    void roundTripsAWholeSong()
    {
        SongStore store(*m_lib);
        Song s = sampleSong();
        SAVE_OK(store, s);
        QVERIFY(!s.id.isNull());
        QVERIFY(s.created.isValid());
        QCOMPARE(s.updated, s.created);

        const std::optional<Song> loaded = store.load(s.id);
        QVERIFY(loaded);
        QCOMPARE(*loaded, s);   // every field, including slide text, colors, and step order
    }

    // #77: imports save many songs per transaction, one disk sync per batch.
    void aBatchCommitsItsSavesTogether()
    {
        SongStore store(*m_lib);
        {
            SongStore::Batch batch(store);
            for (const char *title : {"One", "Two", "Three"}) {
                Song s = sampleSong();
                s.title = title;
                SAVE_OK(store, s);
            }
            QVERIFY(batch.commit().ok);
        }
        QCOMPARE(store.list().size(), 3);

        {
            SongStore::Batch batch(store);
            Song s = sampleSong();
            s.title = "Never committed";
            SAVE_OK(store, s);
        }   // destroyed uncommitted: rolled back
        QCOMPARE(store.list().size(), 3);
    }

    void aSongThatFailsInABatchDoesNotLoseTheOthers()
    {
        QSqlQuery trigger(m_lib->database());
        QVERIFY(trigger.exec("CREATE TEMP TRIGGER refuse BEFORE INSERT ON song_group "
                             "WHEN NEW.label = 'Refused' BEGIN SELECT RAISE(ABORT, 'refused'); END"));
        SongStore store(*m_lib);
        SongStore::Batch batch(store);
        Song before = sampleSong();
        before.title = "Before";
        SAVE_OK(store, before);
        Song bad = sampleSong();
        bad.title = "Bad";
        bad.groups[0].label = "Refused";   // fails after the song row is written
        const Song unchanged = bad;
        const SongStore::Result r = store.save(bad);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("refused"));
        QCOMPARE(bad, unchanged);
        Song after = sampleSong();
        after.title = "After";
        SAVE_OK(store, after);
        QVERIFY(batch.commit().ok);

        QStringList titles;
        for (const SongSummary &s : store.list())
            titles << s.title;
        QCOMPARE(titles, (QStringList{"After", "Before"}));   // none of Bad, not even its song row
    }

    void savingGivesIdsToNewParts()
    {
        SongStore store(*m_lib);
        Song s;
        s.title = "New Song";
        s.groups = {Group{{}, "Verse", {}, {"line"}}};
        s.arrangements = {Arrangement{{}, "Default", {}, {}}};
        SAVE_OK(store, s);
        QVERIFY(!s.groups[0].id.isNull());
        QVERIFY(!s.arrangements[0].id.isNull());
    }

    void updatingKeepsCreatedAndAdvancesUpdated()
    {
        SongStore store(*m_lib);
        Song s = sampleSong();
        SAVE_OK(store, s);
        const QDateTime created = s.created;
        QTest::qWait(5);
        s.title = "I Thank God (Live)";
        SAVE_OK(store, s);
        QCOMPARE(s.created, created);
        QVERIFY(s.updated > created);
        QCOMPARE(store.load(s.id)->title, QStringLiteral("I Thank God (Live)"));
    }

    void editingAGroupChangesEveryPlaceItIsUsed()
    {
        SongStore store(*m_lib);
        Song s = sampleSong();
        SAVE_OK(store, s);
        s.group(s.groups[2].id)->slides = {"New chorus words"};
        SAVE_OK(store, s);

        const Song loaded = *store.load(s.id);
        const QList<QUuid> order = loaded.playOrder(loaded.arrangements[0].id);
        QCOMPARE(order.count(loaded.groups[2].id), 2);   // the chorus appears twice...
        QCOMPARE(loaded.group(order[2])->slides, QStringList{"New chorus words"});   // ...as one group
        QCOMPARE(loaded.group(order[3])->slides, QStringList{"New chorus words"});
    }

    void removingAGroupRemovesItFromArrangements()
    {
        SongStore store(*m_lib);
        Song s = sampleSong();
        SAVE_OK(store, s);
        const QUuid chorus = s.groups[2].id;

        s.removeGroup(chorus);
        QCOMPARE(s.arrangements[0].sequence.size(), 2);   // Intro, Verse 1
        SAVE_OK(store, s);

        const Song loaded = *store.load(s.id);
        QCOMPARE(loaded.groups.size(), 3);
        QVERIFY(!loaded.arrangements[0].sequence.contains(chorus));
        QVERIFY(!loaded.arrangements[1].sequence.contains(chorus));
        QCOMPARE(count(*m_lib, "SELECT count(*) FROM arrangement_step WHERE group_id NOT IN (SELECT id FROM song_group)"), 0);
    }

    void deletingAGroupInTheDatabaseCascades()
    {
        // Even if a group row is deleted directly, foreign keys keep arrangements consistent.
        SongStore store(*m_lib);
        Song s = sampleSong();
        SAVE_OK(store, s);
        QSqlQuery q(m_lib->database());
        q.prepare("DELETE FROM song_group WHERE id = ?");
        q.addBindValue(s.groups[2].id.toString(QUuid::WithoutBraces));
        QVERIFY(q.exec());
        QCOMPARE(store.load(s.id)->arrangements[0].sequence.size(), 2);
    }

    void removingASongRemovesEverythingItOwns()
    {
        SongStore store(*m_lib);
        Song s = sampleSong();
        Song other = sampleSong();
        SAVE_OK(store, s);
        SAVE_OK(store, other);
        QVERIFY(store.remove(s.id));
        QVERIFY(!store.load(s.id));
        QVERIFY(!store.remove(s.id));   // already gone
        QCOMPARE(count(*m_lib, "SELECT count(*) FROM song_group"), other.groups.size());
        QCOMPARE(count(*m_lib, "SELECT count(*) FROM arrangement"), other.arrangements.size());
        QCOMPARE(count(*m_lib, "SELECT count(*) FROM arrangement_step"), 6);
    }

    void removingTheDefaultArrangementClearsIt()
    {
        SongStore store(*m_lib);
        Song s = sampleSong();
        SAVE_OK(store, s);
        s.arrangements.removeLast();   // the default
        s.defaultArrangement = QUuid();
        SAVE_OK(store, s);
        QVERIFY(store.load(s.id)->defaultArrangement.isNull());
    }

    void rejectsArrangementsThatUseOtherGroups()
    {
        SongStore store(*m_lib);
        Song s = sampleSong();
        s.arrangements[0].sequence << QUuid::createUuid();   // not one of this song's groups
        const SongStore::Result r = store.save(s);
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains("Maverick City"), qPrintable(r.error));
        QVERIFY(s.id.isNull());   // the caller's song is untouched by a failed save
        QCOMPARE(store.list().size(), 0);
    }

    void rejectsGroupsThatBelongToAnotherSong()
    {
        SongStore store(*m_lib);
        Song a = sampleSong();
        SAVE_OK(store, a);
        Song b = sampleSong();
        b.groups[0].id = a.groups[0].id;   // copy-paste gone wrong
        b.arrangements = {};
        b.defaultArrangement = QUuid();
        QVERIFY(!store.save(b).ok);
        QCOMPARE(store.load(a.id)->groups.size(), 4);   // song A is unharmed
    }

    void rejectsInvalidSongs()
    {
        SongStore store(*m_lib);
        Song untitled = sampleSong();
        untitled.title = "   ";
        QVERIFY(!store.save(untitled).ok);

        Song badDefault = sampleSong();
        badDefault.defaultArrangement = QUuid::createUuid();
        QVERIFY(!store.save(badDefault).ok);

        Song duplicateGroup = sampleSong();
        duplicateGroup.groups << duplicateGroup.groups[0];
        QVERIFY(!store.save(duplicateGroup).ok);
    }

    void listsSongsByTitleIgnoringCase()
    {
        SongStore store(*m_lib);
        for (const char *t : {"way maker", "Amazing Grace", "Build My Life", "ancient words"}) {
            Song s;
            s.title = t;
            SAVE_OK(store, s);
        }
        QStringList titles;
        for (const SongSummary &s : store.list())
            titles << s.title;
        QCOMPARE(titles, (QStringList{"Amazing Grace", "ancient words", "Build My Life", "way maker"}));
    }

    void songsSurviveReopeningTheLibrary()
    {
        const QString folder = m_lib->folder();
        Song s = sampleSong();
        SAVE_OK(SongStore(*m_lib), s);
        m_lib.reset();
        m_lib = Library::open(folder).library;
        QVERIFY(m_lib);
        QCOMPARE(*SongStore(*m_lib).load(s.id), s);
    }

    void playOrder()
    {
        Song s = sampleSong();
        const auto &g = s.groups;
        // Explicit arrangement, then the default, then the first, then groups in order.
        QCOMPARE(s.playOrder(s.arrangements[0].id), (QList<QUuid>{g[0].id, g[1].id, g[2].id, g[2].id}));
        QCOMPARE(s.playOrder(), (QList<QUuid>{g[1].id, g[2].id}));
        s.defaultArrangement = QUuid();
        QCOMPARE(s.playOrder(), s.arrangements[0].sequence);
        QCOMPARE(s.playOrder(QUuid::createUuid()), s.arrangements[0].sequence);   // unknown id
        s.arrangements.clear();
        QCOMPARE(s.playOrder(), (QList<QUuid>{g[0].id, g[1].id, g[2].id, g[3].id}));
    }
};

QTEST_GUILESS_MAIN(SongsTest)
#include "tst_songs.moc"
