#include "song/lyricspaste.h"
#include "song/slidelist.h"
#include "song/songlimits.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QtTest>

using namespace visualis;

namespace {

// A song like the church's: blank Intro, a two-slide verse, a chorus, and an arrangement
// that repeats the chorus.
Song sample()
{
    Song s;
    s.title = "Joy to the World";   // public domain
    s.groups = {
        Group{QUuid::createUuid(), "Intro", "#b3a624", {""}},
        Group{QUuid::createUuid(), "Verse 1", "#0078cc", {"Joy to the world\nThe Lord is come", "Let earth\nReceive her King"}},
        Group{QUuid::createUuid(), "Chorus", "#cc004f", {"And heaven and nature sing"}},
    };
    const auto &g = s.groups;
    s.arrangements = {Arrangement{QUuid::createUuid(), "Default", {g[0].id, g[1].id, g[2].id, g[2].id}, {}}};
    return s;
}

QStringList texts(const QList<SongSlide> &slides)
{
    QStringList out;
    for (const SongSlide &s : slides)
        out << s.text;
    return out;
}


// One group of `slides` slides, played `steps` times: structurally small, huge expanded.
Song repeated(int slides, int steps)
{
    Song s;
    s.title = "Huge";
    QStringList text;
    for (int i = 0; i < slides; ++i)
        text << QString::number(i);
    s.groups = {Group{QUuid::createUuid(), "Verse", "", text}};
    s.arrangements = {Arrangement{QUuid::createUuid(), "Default", QList<QUuid>(steps, s.groups[0].id), {}}};
    return s;
}

} // namespace

class SlideListTest : public QObject
{
    Q_OBJECT

private slots:
    void followsTheArrangementWithRepeats()
    {
        const Song s = sample();
        const QList<SongSlide> slides = songSlides(s);
        QCOMPARE(texts(slides), (QStringList{"", "Joy to the world\nThe Lord is come", "Let earth\nReceive her King",
                                             "And heaven and nature sing", "And heaven and nature sing"}));
        // The two chorus slides are the same group at different steps.
        QCOMPARE(slides[3].groupId, slides[4].groupId);
        QCOMPARE(slides[3].step, 2);
        QCOMPARE(slides[4].step, 3);
    }

    void eachSlideKnowsItsGroup()
    {
        const QList<SongSlide> slides = songSlides(sample());
        const SongSlide &second = slides[2];
        QCOMPARE(second.groupLabel, QStringLiteral("Verse 1"));
        QCOMPARE(second.groupColor, QStringLiteral("#0078cc"));
        QCOMPARE(second.indexInGroup, 1);
        QCOMPARE(second.groupSlideCount, 2);
        QCOMPARE(second.step, 1);
        QVERIFY(slides[0].isBlank());
        QVERIFY(!second.isBlank());
    }

    void noArrangementPlaysGroupsInOrder()
    {
        Song s = sample();
        s.arrangements.clear();
        QCOMPARE(songSlides(s).size(), 4);   // Intro, two verse slides, one chorus
    }

    void choosesTheRequestedArrangement()
    {
        Song s = sample();
        s.arrangements << Arrangement{QUuid::createUuid(), "Short", {s.groups[2].id}, {}};
        QCOMPARE(songSlides(s, s.arrangements[1].id).size(), 1);
        QCOMPARE(songSlides(s).size(), 5);   // default: the first arrangement
    }

    void groupWithNoSlidesGivesOneBlankSlide()
    {
        Song s = sample();
        s.groups[0].slides.clear();
        const QList<SongSlide> slides = songSlides(s);
        QCOMPARE(slides[0].text, QString());
        QCOMPARE(slides[0].groupLabel, QStringLiteral("Intro"));
        QCOMPARE(slides[0].groupSlideCount, 1);
    }

    void stepsForMissingGroupsAreSkipped()
    {
        // An arrangement built before a group was deleted elsewhere: no crash, no gap.
        Song s = sample();
        s.groups.removeAt(1);   // Verse 1, without updating the arrangement
        const QList<SongSlide> slides = songSlides(s);
        QCOMPARE(texts(slides), (QStringList{"", "And heaven and nature sing", "And heaven and nature sing"}));
        QCOMPARE(slides[1].step, 2);   // steps keep their arrangement positions
    }

    void emptyArrangementGivesNoSlides()
    {
        Song s = sample();
        s.arrangements[0].sequence.clear();
        QVERIFY(songSlides(s).isEmpty());
    }

    void songWithNoGroups()
    {
        Song s;
        s.title = "Empty";
        QVERIFY(songSlides(s).isEmpty());
    }

    void pastedLyricsExpandInPastedOrder()
    {
        const Song s = songFromPastedLyrics("Joy", "Verse 1\nJoy to the world\n\nChorus (x2)\nAnd heaven and nature sing\n");
        const QList<SongSlide> slides = songSlides(s);
        QCOMPARE(slides.size(), 3);
        QCOMPARE(slides[1].groupLabel, QStringLiteral("Chorus"));
        QCOMPARE(slides[2].groupLabel, QStringLiteral("Chorus"));
    }

    // #54: a service's Planning Center order, by label. Lyric-less sections the song has no
    // group for become blank slides (P3 finding 9); other unknown labels are skipped.
    void aLabelSequencePlaysInThatOrder()
    {
        Song s = sample();
        s.groups.removeFirst();   // no Intro group of its own
        const QList<SongSlide> slides =
            songSlides(s, {"Intro", "Verse 1", "Chorus", "Turnaround", "Bridge 2", "Chorus", "Interlude", "Ending"});
        QCOMPARE(texts(slides), (QStringList{"", "Joy to the world\nThe Lord is come", "Let earth\nReceive her King",
                                             "And heaven and nature sing", "", "And heaven and nature sing", "", ""}));
        QCOMPARE(slides[0].groupLabel, QStringLiteral("Intro"));
        QVERIFY(slides[0].groupId.isNull());
        QVERIFY(slides[0].isBlank());
        QCOMPARE(slides[4].groupLabel, QStringLiteral("Turnaround"));
        QCOMPARE(slides[4].step, 3);
        QCOMPARE(slides[5].step, 5);   // "Bridge 2" (step 4) has no group and lyrics, so it's skipped
        QCOMPARE(slides[7].groupLabel, QStringLiteral("Ending"));
    }

    // A "Blank" step (a deliberate pause) gives a blank slide, not nothing.
    void aBlankStepGivesABlankSlide()
    {
        Song s = sample();
        const QList<SongSlide> slides = songSlides(s, {"Chorus", "Blank", "Chorus"});
        QCOMPARE(texts(slides), (QStringList{"And heaven and nature sing", "", "And heaven and nature sing"}));
        QCOMPARE(slides[1].groupLabel, QStringLiteral("Blank"));
        QCOMPARE(slides[1].step, 1);
    }

    void aLyriclessLabelUsesTheSongsGroupWhenItHasOne()
    {
        const Song s = sample();
        const QList<SongSlide> slides = songSlides(s, {"Intro", "Chorus"});
        QCOMPARE(slides.size(), 2);
        QCOMPARE(slides[0].groupId, s.groups[0].id);
        QCOMPARE(slides[0].groupColor, QStringLiteral("#b3a624"));
    }

    void lyriclessLabels()
    {
        for (const char *label : {"Intro", "Turnaround", "Interlude", "Ending", "Instrumental", "Outro", "Interlude 2", "Blank"})
            QVERIFY2(isLyricless(label), label);
        for (const char *label : {"Verse 1", "Chorus", "Tag", "Pre Chorus 1", "Bridge", ""})
            QVERIFY2(!isLyricless(label), label);
    }

    // #83: limits far above real songs, with the operator told which one and by how much.
    void songLimitsNameWhatIsOver()
    {
        QVERIFY(songLimitError(repeated(30, 40)).isEmpty());
        Song many;
        for (int i = 0; i < 40'000; ++i)
            many.groups << Group{QUuid::createUuid(), "Verse", "", {"x"}};
        QCOMPARE(songLimitError(many), QStringLiteral("too many groups: 40,000; the limit is 500"));
        QVERIFY(songLimitError(repeated(501, 1)).contains("slides in \"Verse\""));
        QVERIFY(songLimitError(repeated(1, 1001)).contains("steps in arrangement"));
        Song longSlide = repeated(1, 1);
        longSlide.groups[0].slides[0] = QString(10'001, 'a');
        QVERIFY(songLimitError(longSlide).contains("characters"));
        Song arrangements = repeated(1, 1);
        arrangements.arrangements = QList<Arrangement>(101, arrangements.arrangements[0]);
        QVERIFY(songLimitError(arrangements).contains("arrangements"));
        Song total;
        for (int i = 0; i < 21; ++i)
            total.groups << repeated(500, 1).groups[0];
        QVERIFY(songLimitError(total).startsWith("too many slides:"));
    }

    // #95: the editor writes slide overrides now, so writes bound them. Reads drop what's over.
    void slideOverridesAreBoundedOnWrite()
    {
        Song song = repeated(3, 1);
        song.groups[0].fitSlideOverrides();
        const QJsonObject everything{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ffcc00"}, {"maxSize", 0.07}}}}}};
        song.groups[0].slideOverrides[0] = everything;
        QVERIFY(overrideLimitError(song).isEmpty());
        // Keys Visualis doesn't know still count.
        song.groups[0].slideOverrides[1] = {{"later", QString(songlimits::kSlideOverrideBytes, 'x')}};
        QVERIFY2(overrideLimitError(song).startsWith("a slide of \"Verse\" has too much style"), qPrintable(overrideLimitError(song)));
        QVERIFY(songLimitError(song).isEmpty());   // it still shows

        Song many = repeated(1, 1);
        many.groups.clear();
        const QJsonObject big{{"later", QString(songlimits::kSlideOverrideBytes / 2, 'x')}};
        for (int i = 0; i * songlimits::kSlideOverrideBytes / 2 <= songlimits::kOverrideBytes; ++i)
            many.groups << Group{QUuid::createUuid(), "Verse", "", {"x"}, {big}};
        QVERIFY2(overrideLimitError(many).startsWith("too much slide style"), qPrintable(overrideLimitError(many)));

        // What the library already holds can be written back, but not grown.
        StoredOverrides stored;
        stored.total = qint64(many.groups.size()) * QJsonDocument(big).toJson(QJsonDocument::Compact).size();
        stored.oversized.insert(QJsonDocument(song.groups[0].slideOverrides[1]).toJson(QJsonDocument::Compact));
        QVERIFY(overrideLimitError(many, stored).isEmpty());
        QVERIFY(overrideLimitError(song, stored).isEmpty());
        many.groups << Group{QUuid::createUuid(), "Verse", "", {"x"}, {big}};
        QVERIFY(!overrideLimitError(many, stored).isEmpty());
    }

    void oversizedOverridesAreDroppedWithAWarning()
    {
        Song song = repeated(2, 1);
        const QJsonObject gold{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ffcc00"}}}}}};
        song.groups[0].slideOverrides = {gold, {{"later", QString(songlimits::kSlideOverrideBytes, 'x')}}};
        const QStringList warnings = dropOversizedOverrides(song);
        QCOMPARE(warnings.size(), 1);
        QVERIFY2(warnings[0].contains("slide 2 of \"Verse\""), qPrintable(warnings[0]));
        QCOMPARE(song.groups[0].slideOverrides, (QList<QJsonObject>{gold, {}}));

        Song many = repeated(1, 1);
        many.groups.clear();
        const QJsonObject big{{"later", QString(songlimits::kSlideOverrideBytes / 2, 'x')}};
        for (int i = 0; i * songlimits::kSlideOverrideBytes / 2 <= songlimits::kOverrideBytes; ++i)
            many.groups << Group{QUuid::createUuid(), "Verse", "", {"x"}, {big}};
        QCOMPARE(dropOversizedOverrides(many).size(), 1);   // over the song's total: all of them, said once
        QVERIFY(overrideLimitError(many).isEmpty());
    }

    // The issue's case: 10,000 slides played 10,000 times would be 100 million slides.
    void aHugeExpansionIsRefusedNotAllocated()
    {
        QString error;
        QElapsedTimer t;
        t.start();
        QVERIFY(songSlides(repeated(10'000, 10'000), QUuid(), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY2(t.elapsed() < 1000, qPrintable(QString::number(t.elapsed())));

        // Within every structural limit, but 500 x 1,000 = 500,000 slides to play.
        error.clear();
        QVERIFY(songSlides(repeated(500, 1000), QUuid(), &error).isEmpty());
        QCOMPARE(error, QStringLiteral("too many slides to play: 500,000; the limit is 10,000"));

        error.clear();
        QCOMPARE(songSlides(repeated(10, 1000), QUuid(), &error).size(), 10'000);   // exactly at the limit
        QVERIFY(error.isEmpty());
    }

    void aHugeSequenceIsRefused()
    {
        QString error;
        QVERIFY(songSlides(repeated(500, 1), QStringList(1000, "Verse"), &error).isEmpty());
        QVERIFY(error.contains("slides to play"));
        error.clear();
        QVERIFY(songSlides(repeated(1, 1), QStringList(1001, "Verse"), &error).isEmpty());
        QVERIFY(error.contains("steps in the sequence"));
    }
};

QTEST_GUILESS_MAIN(SlideListTest)
#include "tst_slidelist.moc"
