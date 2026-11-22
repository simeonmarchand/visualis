#include "song/lyricspaste.h"
#include "song/slidelist.h"

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
        for (const char *label : {"Intro", "Turnaround", "Interlude", "Ending", "Instrumental", "Outro", "Interlude 2"})
            QVERIFY2(isLyricless(label), label);
        for (const char *label : {"Verse 1", "Chorus", "Tag", "Pre Chorus 1", "Bridge", ""})
            QVERIFY2(!isLyricless(label), label);
    }
};

QTEST_GUILESS_MAIN(SlideListTest)
#include "tst_slidelist.moc"
