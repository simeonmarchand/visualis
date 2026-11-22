#include "library/library.h"
#include "song/songmatch.h"
#include "song/songstore.h"

#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace visualis;
using Kind = SongMatch::Kind;
using By = SongMatch::By;

namespace {

// Public-domain titles; synthetic ids.
SongCandidate song(const QString &title, const QString &ccli = {}, const QString &authors = {}, const QString &pco = {})
{
    return SongCandidate{QUuid::createUuid(), title, ccli, authors, pco};
}

PcoSong pco(const QString &title, const QString &ccli = {}, const QString &author = {}, const QString &id = "p1")
{
    return PcoSong{id, title, ccli, author, {}};
}

} // namespace

class SongMatchTest : public QObject
{
    Q_OBJECT

private slots:
    void normalizesTitles_data()
    {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("normalized");
        QTest::newRow("case and spaces") << "  Amazing   GRACE " << "amazing grace";
        QTest::newRow("punctuation") << "Holy, Holy, Holy!" << "holy holy holy";
        QTest::newRow("apostrophe") << "Lord’s Prayer" << "lords prayer";
        QTest::newRow("leading The") << "The Old Rugged Cross" << "old rugged cross";
        QTest::newRow("ampersand") << "Jesus & Me" << "jesus and me";
        QTest::newRow("accents") << "Señor, Ten Piedad" << "senor ten piedad";
        QTest::newRow("trailing note") << "Be Thou My Vision (Live)" << "be thou my vision";
        QTest::newRow("two notes") << "Be Thou My Vision (Live) [Acoustic]" << "be thou my vision";
        QTest::newRow("all note") << "(Untitled)" << "untitled";
        QTest::newRow("hyphen") << "Joy-to-the-World" << "joy to the world";
    }

    void normalizesTitles()
    {
        QFETCH(QString, title);
        QFETCH(QString, normalized);
        QCOMPARE(normalizedTitle(title), normalized);
    }

    // 1. The song linked last week wins over everything else.
    void aLinkedSongMatchesFirst()
    {
        const QList<SongCandidate> lib{song("Amazing Grace"), song("Grace Renamed", {}, {}, "p1")};
        const SongMatch m = matchSong(pco("Amazing Grace"), lib);
        QCOMPARE(m.kind, Kind::Linked);
        QCOMPARE(m.by, By::PcoSongId);
        QCOMPARE(m.songId, lib[1].id);
    }

    // 2. CCLI, ignoring how the number is written.
    void ccliMatchesNext()
    {
        const QList<SongCandidate> lib{song("Amazing Grace"), song("Amazing Grace (My Chains)", "22025")};
        const SongMatch m = matchSong(pco("Amazing Grace", " 22025 "), lib);
        QCOMPARE(m.kind, Kind::Linked);
        QCOMPARE(m.by, By::Ccli);
        QCOMPARE(m.songId, lib[1].id);
    }

    void aSharedCcliIsSettledByTitleOrConfirmed()
    {
        const QList<SongCandidate> lib{song("Amazing Grace", "22025"), song("Gracia Sublime", "22025"),
                                       song("Amazing Grace Chorus Only", "22025")};
        const SongMatch byTitle = matchSong(pco("Amazing Grace", "22025"), lib);
        QCOMPARE(byTitle.kind, Kind::Linked);
        QCOMPARE(byTitle.by, By::Ccli);
        QCOMPARE(byTitle.songId, lib[0].id);
        const SongMatch none = matchSong(pco("Something Else", "22025"), lib);
        QCOMPARE(none.kind, Kind::Confirm);
        QCOMPARE(none.candidates.size(), 3);
    }

    // A note in brackets is set aside, so "(Spanish)" makes two equal titles: confirmed.
    void aSharedCcliAndTitleIsConfirmed()
    {
        const QList<SongCandidate> lib{song("Amazing Grace", "22025"), song("Amazing Grace (Spanish)", "22025")};
        const SongMatch m = matchSong(pco("Amazing Grace", "22025"), lib);
        QCOMPARE(m.kind, Kind::Confirm);
        QCOMPARE(m.candidates.size(), 2);
    }

    // 3. Simeon's decision on #55: one exactly equal title links, shown as "by title".
    void aUniqueTitleLinksByTitle()
    {
        const QList<SongCandidate> lib{song("Holy, Holy, Holy"), song("Amazing Grace")};
        const SongMatch m = matchSong(pco("amazing grace!"), lib);
        QCOMPARE(m.kind, Kind::Linked);
        QCOMPARE(m.by, By::Title);
        QCOMPARE(m.songId, lib[1].id);
    }

    // Never guess between two songs.
    void equalTitlesAreNeverAutoMatched()
    {
        const QList<SongCandidate> lib{song("Amazing Grace", {}, "Traditional"), song("Amazing Grace", {}, "John Newton")};
        const SongMatch m = matchSong(pco("Amazing Grace", {}, "John Newton"), lib);
        QCOMPARE(m.kind, Kind::Confirm);
        QCOMPARE(m.by, By::Title);
        QCOMPARE(m.candidates, (QList<QUuid>{lib[1].id, lib[0].id}));   // the author puts Newton's first
    }

    void aTitleLinkedToAnotherPlanningCenterSongIsOnlySuggested()
    {
        const QList<SongCandidate> lib{song("Amazing Grace", {}, {}, "p-other")};
        const SongMatch m = matchSong(pco("Amazing Grace"), lib);
        QCOMPARE(m.kind, Kind::Confirm);
        QCOMPARE(m.candidates, (QList<QUuid>{lib[0].id}));
    }

    void aTitleWithAnotherCcliIsOnlySuggested()
    {
        const QList<SongCandidate> lib{song("Amazing Grace", "111")};
        const SongMatch m = matchSong(pco("Amazing Grace", "222"), lib);
        QCOMPARE(m.kind, Kind::Confirm);
    }

    // 4. Similar titles are suggested, closest first, at most three.
    void similarTitlesAreSuggested()
    {
        const QList<SongCandidate> lib{song("Amazing Grace My Chains Are Gone"), song("Amazng Grace"), song("Joy to the World"),
                                       song("Amazing Grace Medley"), song("Amazing Grace Reprise"), song("Amazing")};
        const SongMatch m = matchSong(pco("Amazing Grace"), lib);
        QCOMPARE(m.kind, Kind::Confirm);
        QCOMPARE(m.by, By::SimilarTitle);
        QCOMPARE(m.candidates.size(), 3);
        QCOMPARE(m.candidates.first(), lib[1].id);   // the typo is closest
        QVERIFY(!m.candidates.contains(lib[2].id));
        QVERIFY(!m.candidates.contains(lib[5].id));   // one word isn't a start that counts
    }

    // 5. Nothing close.
    void nothingCloseIsAPlaceholder()
    {
        const QList<SongCandidate> lib{song("Holy, Holy, Holy"), song("Joy to the World")};
        QCOMPARE(matchSong(pco("Be Thou My Vision"), lib), SongMatch{});
        QCOMPARE(matchSong(pco("Anything"), {}), SongMatch{});
        QCOMPARE(matchSong(pco(""), lib), SongMatch{});
    }

    void shortTitlesNeedToBeExact()
    {
        const QList<SongCandidate> lib{song("Joy")};
        QCOMPARE(matchSong(pco("Boy"), lib).kind, Kind::None);
    }

    void readsTheSongFromAnImportSnapshot()
    {
        const QJsonObject snap{{"itemType", "song"},
                               {"song", QJsonObject{{"id", "p9"}, {"title", "Amazing Grace"}, {"ccliNumber", "22025"},
                                                   {"author", "John Newton"}, {"copyright", "Public Domain"}}}};
        const std::optional<PcoSong> s = snapshotSong(snap);
        QVERIFY(s);
        QCOMPARE(s->id, QStringLiteral("p9"));
        QCOMPARE(s->ccliNumber, QStringLiteral("22025"));
        QVERIFY(!snapshotSong(QJsonObject{{"itemType", "item"}}));
    }

    void theStoreListsCandidates()
    {
        QTemporaryDir dir;
        auto lib = Library::open(dir.path());
        QVERIFY2(lib.library, qPrintable(lib.message));
        SongStore store(*lib.library);
        Song s;
        s.title = "Amazing Grace";
        s.credits = Credits{"22025", "John Newton", "Public Domain"};
        s.pcoSongId = "p1";
        QVERIFY(store.save(s).ok);
        QCOMPARE(store.matchCandidates(), (QList<SongCandidate>{{s.id, "Amazing Grace", "22025", "John Newton", "p1"}}));
    }
};

QTEST_GUILESS_MAIN(SongMatchTest)
#include "tst_songmatch.moc"
