#include "library/library.h"
#include "pco/planimport.h"
#include "pco/songlink.h"
#include "song/songstore.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace visualis;
using Kind = ServiceItem::Kind;

namespace {

Group group(const QString &label, const QString &text) { return Group{QUuid::createUuid(), label, {}, {text}}; }

// Shaped like a ProPresenter import: groups, one arrangement, no credits or links (PP finding 5).
Song libSong(const QString &title, QList<Group> groups)
{
    Song s;
    s.title = title;
    s.groups = std::move(groups);
    QList<QUuid> order;
    for (const Group &g : s.groups)
        order << g.id;
    s.arrangements = {Arrangement{QUuid::createUuid(), "Default", order, {}}};
    return s;
}

PcoItem songItem(const QString &id, const QString &title, int position, QStringList sequence,
                 const QString &ccli = {}, const QString &pcoSongId = {})
{
    PcoItem it;
    it.id = id;
    it.type = PcoItemType::Song;
    it.title = title;
    it.position = position;
    it.song = PcoSong{pcoSongId.isEmpty() ? "ps" + id : pcoSongId, title, ccli, "Traditional", "Public Domain"};
    it.arrangement = PcoArrangement{"pa" + id, "Default", std::move(sequence), 0, {}, 0};
    return it;
}

Service imported(const QList<PcoItem> &items)
{
    PcoPlanItems p;
    p.items = items;
    PcoPlan plan;
    plan.id = "70001";
    return serviceFromPlan({"900", "Sunday"}, plan, p, QDateTime::currentDateTimeUtc()).service;
}

} // namespace

class SongLinkTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_n = 0;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<SongStore> m_songs;

private slots:
    void init()
    {
        auto r = Library::open(m_dir.filePath(QString::number(++m_n)));
        QVERIFY2(r.library, qPrintable(r.message));
        m_library = std::move(r.library);
        m_songs = std::make_unique<SongStore>(*m_library);
    }

    void cleanup()
    {
        m_songs.reset();
        m_library.reset();
    }

    void linksWhatItIsSureOfAndCountsTheRest()
    {
        Song grace = libSong("Amazing Grace", {group("Verse 1", "Amazing grace")});
        Song holy = libSong("Holy, Holy, Holy", {group("Verse 1", "Holy, holy, holy")});
        Song joy1 = libSong("Joy to the World", {group("Verse 1", "Joy")});
        Song joy2 = libSong("Joy to the World", {group("Verse 1", "Joy (another)")});
        holy.credits.ccliNumber = "1156";
        for (Song *s : {&grace, &holy, &joy1, &joy2})
            QVERIFY(m_songs->save(*s).ok);

        Service s = imported({songItem("1", "Amazing Grace", 1, {"Verse 1"}),
                              songItem("2", "Holy Holy Holy (Hymn)", 2, {"Verse 1"}, "1156"),
                              songItem("3", "Joy to the World", 3, {"Verse 1"}),
                              songItem("4", "Be Thou My Vision", 4, {"Verse 1"})});
        s.items.insert(0, ServiceItem{});   // a plain item stays as it is
        s.items[0].title = "Welcome";

        const SongLinkReport r = linkSongs(s, *m_songs);
        QCOMPARE(r.linked, 1);       // by CCLI
        QCOMPARE(r.byTitle, 1);
        QCOMPARE(r.toConfirm, 1);    // two songs called Joy to the World
        QCOMPARE(r.placeholders, 1);
        QCOMPARE(r.summary(), QStringLiteral("2 songs matched (1 by title), 1 to confirm, 1 not in the library."));

        QCOMPARE(s.items[0].kind, Kind::Placeholder);
        QCOMPARE(s.items[1].kind, Kind::Song);
        QCOMPARE(s.items[1].ref, grace.id);
        QCOMPARE(s.items[2].ref, holy.id);
        QCOMPARE(s.items[3].kind, Kind::Placeholder);
        QCOMPARE(s.items[4].kind, Kind::Placeholder);
        QCOMPARE(s.items[1].customSequence, QStringList{"Verse 1"});
        QCOMPARE(s.items[1].pcoItemId, QStringLiteral("1"));
    }

    // Linking saves the Planning Center id, so next week matches at once, by id.
    void nextWeekMatchesByPlanningCenterId()
    {
        Song grace = libSong("Amazing Grace", {group("Verse 1", "Amazing grace")});
        QVERIFY(m_songs->save(grace).ok);
        Service week1 = imported({songItem("1", "Amazing Grace", 1, {"Verse 1"}, {}, "p-grace")});
        QCOMPARE(linkSongs(week1, *m_songs).byTitle, 1);

        const Song saved = *m_songs->load(grace.id);
        QCOMPARE(saved.pcoSongId, QStringLiteral("p-grace"));
        QCOMPARE(saved.credits.authors, QStringLiteral("Traditional"));   // filled in: ProPresenter had none
        QCOMPARE(saved.credits.copyright, QStringLiteral("Public Domain"));

        // Renamed in Planning Center: still the same song.
        Service week2 = imported({songItem("9", "Amazing Grace (My Chains Are Gone)", 1, {"Verse 1"}, {}, "p-grace")});
        const SongLinkReport r = linkSongs(week2, *m_songs);
        QCOMPARE(r.linked, 1);
        QCOMPARE(r.byTitle, 0);
        QCOMPARE(week2.items[0].ref, grace.id);
    }

    // Review of #103 (M6): staged, nothing is written until the caller saves it with the service.
    void stagedLinksWaitForTheCaller()
    {
        Song grace = libSong("Amazing Grace", {group("Verse 1", "Amazing grace")});
        QVERIFY(m_songs->save(grace).ok);
        Service s = imported({songItem("1", "Amazing Grace", 1, {"Verse 1"}, {}, "p-grace"),
                              songItem("2", "Amazing Grace", 2, {"Verse 1"}, {}, "p-grace")});
        QList<Song> staged;
        const SongLinkReport r = linkSongs(s, *m_songs, &staged);
        QCOMPARE(r.byTitle, 1);
        QCOMPARE(r.linked, 1);   // the second by the id the first remembered, unsaved
        QCOMPARE(staged.size(), 1);
        QCOMPARE(staged[0].pcoSongId, QStringLiteral("p-grace"));
        QVERIFY(m_songs->load(grace.id)->pcoSongId.isEmpty());   // not written
    }

    // Second review of #103 (H2): a re-import links only what it's told to.
    void onlyTheItemsNamedAreLinked()
    {
        Song grace = libSong("Amazing Grace", {group("Verse 1", "Amazing grace")});
        Song holy = libSong("Holy, Holy, Holy", {group("Verse 1", "Holy")});
        QVERIFY(m_songs->save(grace).ok);
        QVERIFY(m_songs->save(holy).ok);
        Service s = imported({songItem("1", "Amazing Grace", 1, {"Verse 1"}), songItem("2", "Holy, Holy, Holy", 2, {"Verse 1"})});
        for (ServiceItem &it : s.items)
            it.id = QUuid::createUuid();
        const QSet<QUuid> only{s.items[1].id};
        QList<Song> staged;
        const SongLinkReport r = linkSongs(s, *m_songs, &staged, &only);
        QCOMPARE(s.items[0].kind, Kind::Placeholder);   // left alone
        QCOMPARE(s.items[1].kind, Kind::Song);
        QCOMPARE(r.items, (QList<SongLinkReport::Linked>{{s.items[1].id, "Holy, Holy, Holy", true}}));
    }

    void creditsTheSongHasAreKept()
    {
        Song s = libSong("Amazing Grace", {group("Verse 1", "Amazing grace")});
        s.credits = Credits{{}, "John Newton", {}};
        QVERIFY(rememberPcoSong(s, PcoSong{"p1", "Amazing Grace", "22025", "Traditional", "Public Domain"}));
        QCOMPARE(s.credits.authors, QStringLiteral("John Newton"));
        QCOMPARE(s.credits.ccliNumber, QStringLiteral("22025"));
        QVERIFY(!rememberPcoSong(s, PcoSong{"p2", "Amazing Grace", "1", "X", "Y"}));   // nothing empty left
        QCOMPARE(s.pcoSongId, QStringLiteral("p1"));   // its link isn't replaced
    }

    // One library song, two Planning Center songs with its title: only the first links.
    void aSongLinkedEarlierInTheServiceIsNotLinkedToAnother()
    {
        Song grace = libSong("Amazing Grace", {group("Verse 1", "Amazing grace")});
        QVERIFY(m_songs->save(grace).ok);
        Service s = imported({songItem("1", "Amazing Grace", 1, {"Verse 1"}, {}, "p-a"),
                              songItem("2", "Amazing Grace", 2, {"Verse 1"}, {}, "p-b")});
        const SongLinkReport r = linkSongs(s, *m_songs);
        QCOMPARE(r.byTitle, 1);
        QCOMPARE(r.toConfirm, 1);
        QCOMPARE(s.items[1].kind, Kind::Placeholder);
    }

    void theArrangementLinkedToPlanningCentersIsUsed()
    {
        Song grace = libSong("Amazing Grace", {group("Verse 1", "Amazing grace")});
        grace.arrangements << Arrangement{QUuid::createUuid(), "Short", {grace.groups[0].id}, "pa1"};
        QVERIFY(m_songs->save(grace).ok);
        Service s = imported({songItem("1", "Amazing Grace", 1, {"Verse 1"})});   // arrangement "pa1"
        linkSongs(s, *m_songs);
        QCOMPARE(s.items[0].arrangement, grace.arrangements[1].id);

        Service other = imported({songItem("2", "Amazing Grace", 1, {"Verse 1"})});   // "pa2": no such
        linkSongs(other, *m_songs);
        QVERIFY(other.items[0].arrangement.isNull());
    }

    // ProPresenter spike finding 8: forgiving labels match, and only real gaps are listed.
    void unmatchedLabelsAreListedPerSong()
    {
        Song song = libSong("Example Song", {group("Intro", ""), group("Verse 1", "a"), group("PreChorus", "b"),
                                             group("Chorus", "c"), group("Bridge 1", "d")});
        QVERIFY(m_songs->save(song).ok);
        Service s = imported({songItem("1", "Example Song", 1,
                                       {"Intro", "Verse 1", "Pre Chorus 1", "Chorus", "Turnaround", "Bridge 1",
                                        "Bridge 2", "Bridge 3", "Bridge 2", "Chorus", "Ending"})});
        const SongLinkReport r = linkSongs(s, *m_songs);
        QCOMPARE(r.unmatched, QStringList{"Example Song: Bridge 2, Bridge 3"});
        QCOMPARE(unmatchedLabels(s.items[0], song), (QStringList{"Bridge 2", "Bridge 3"}));
    }

    void theOperatorsChoiceTakesTheId()
    {
        Song s = libSong("Amazing Grace", {group("Verse 1", "a")});
        s.pcoSongId = "p-old";
        s.credits.authors = "John Newton";
        QVERIFY(takePcoSong(s, PcoSong{"p-new", "Amazing Grace", "22025", "Traditional", {}}));
        QCOMPARE(s.pcoSongId, QStringLiteral("p-new"));
        QCOMPARE(s.credits.authors, QStringLiteral("John Newton"));   // credits stay fill-only
        QCOMPARE(s.credits.ccliNumber, QStringLiteral("22025"));
        QVERIFY(!takePcoSong(s, PcoSong{"p-new", "Amazing Grace", {}, {}, {}}));

        QVERIFY(!releasePcoSong(s, "p-other"));
        QVERIFY(!releasePcoSong(s, ""));
        QVERIFY(releasePcoSong(s, "p-new"));
        QVERIFY(s.pcoSongId.isEmpty());
    }

    void anEmptyReportSaysNothing()
    {
        QVERIFY(SongLinkReport{}.summary().isEmpty());
        QCOMPARE((SongLinkReport{0, 0, 0, 2, {}}.summary()), QStringLiteral("0 songs matched, 2 not in the library."));
        QCOMPARE((SongLinkReport{1, 0, 0, 0, {}}.summary()), QStringLiteral("1 song matched."));
    }
};

QTEST_GUILESS_MAIN(SongLinkTest)
#include "tst_songlink.moc"
