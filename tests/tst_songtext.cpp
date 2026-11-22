#include "song/lyricspaste.h"
#include "song/songtext.h"

#include <QtTest>

using namespace visualis;

namespace {

// Amazing Grace (public domain): Intro (blank), Verse 1 (2 slides), Chorus; arranged
// Intro V1 C V1 C.
Song sample()
{
    Song s;
    s.title = "Amazing Grace";
    s.groups = {
        Group{QUuid::createUuid(), "Intro", "#b3a624", {""}},
        Group{QUuid::createUuid(), "Verse 1", "#0078cc",
              {"Amazing grace how sweet the sound\nThat saved a wretch like me", "I once was lost but now am found\nWas blind but now I see"}},
        Group{QUuid::createUuid(), "Chorus", "#cc004f", {"My chains are gone\nI've been set free"}},
    };
    const auto &g = s.groups;
    s.arrangements = {Arrangement{QUuid::createUuid(), "Default", {g[0].id, g[1].id, g[2].id, g[1].id, g[2].id}, {}}};
    return s;
}

// Each group once in a Default arrangement, plus `repeats` of the first.
Song withGroups(QList<Group> groups)
{
    Song s;
    s.title = "Round trip";
    s.groups = groups;
    QList<QUuid> sequence;
    for (const Group &g : groups)
        sequence << g.id;
    sequence << groups.first().id;
    s.arrangements = {Arrangement{QUuid::createUuid(), "Default", sequence, {}}};
    return s;
}

void verifyLossless(const Song &s)
{
    const LyricsEdit e = applyLyricsText(s, lyricsText(s));
    QCOMPARE(e.song.groups, s.groups);
    QCOMPARE(e.song.arrangements, s.arrangements);
    QVERIFY2(e.warnings.isEmpty(), qPrintable(e.warnings.join('\n')));
}

QStringList labels(const Song &s)
{
    QStringList out;
    for (const Group &g : s.groups)
        out << g.label;
    return out;
}

} // namespace

class SongTextTest : public QObject
{
    Q_OBJECT

private slots:
    void writesHeadersAndBlankLineSlideBreaks()
    {
        QCOMPARE(lyricsText(sample()),
                 QStringLiteral("[Intro]\n\n"
                                "[Verse 1]\nAmazing grace how sweet the sound\nThat saved a wretch like me\n\n"
                                "I once was lost but now am found\nWas blind but now I see\n\n"
                                "[Chorus]\nMy chains are gone\nI've been set free"));
    }

    void unchangedTextChangesNothing()
    {
        const Song s = sample();
        const LyricsEdit e = applyLyricsText(s, lyricsText(s));
        QCOMPARE(e.song.groups, s.groups);
        QCOMPARE(e.song.arrangements, s.arrangements);
        QVERIFY2(e.warnings.isEmpty(), qPrintable(e.warnings.join('\n')));
    }

    void editingALineKeepsTheGroupAndArrangement()
    {
        const Song s = sample();
        QString text = lyricsText(s);
        text.replace("set free", "set free!");
        const LyricsEdit e = applyLyricsText(s, text);
        QCOMPARE(e.song.groups[2].id, s.groups[2].id);
        QCOMPARE(e.song.groups[2].color, s.groups[2].color);
        QCOMPARE(e.song.groups[2].slides, QStringList{"My chains are gone\nI've been set free!"});
        QCOMPARE(e.song.arrangements, s.arrangements);
    }

    void slidesAreTakenAsWrittenNotResplit()
    {
        const Song s = sample();
        QString text = lyricsText(s);
        // Join the verse's two slides into one four-line slide.
        text.replace("like me\n\nI once", "like me\nI once");
        const LyricsEdit e = applyLyricsText(s, text);
        QCOMPARE(e.song.groups[1].slides.size(), 1);
        QCOMPARE(e.song.groups[1].slides[0].count('\n'), 3);
    }

    void aHeaderRenamedInPlaceKeepsItsGroup()
    {
        const Song s = sample();
        QString text = lyricsText(s);
        text.replace("[Chorus]", "[Refrain]");
        const LyricsEdit e = applyLyricsText(s, text);
        QCOMPARE(labels(e.song), (QStringList{"Intro", "Verse 1", "Refrain"}));
        QCOMPARE(e.song.groups[2].id, s.groups[2].id);   // so the arrangement still plays it
        QCOMPARE(e.song.arrangements, s.arrangements);
    }

    void labelsMatchIgnoringCaseAndOrder()
    {
        const Song s = sample();
        const LyricsEdit e = applyLyricsText(s, "[chorus]\nMy chains are gone\n\n[verse 1]\nAmazing grace\n\n[Intro]");
        QCOMPARE(e.song.groups[0].id, s.groups[2].id);
        QCOMPARE(e.song.groups[1].id, s.groups[1].id);
        QCOMPARE(e.song.groups[2].id, s.groups[0].id);
        QCOMPARE(e.song.arrangements, s.arrangements);   // group order isn't play order
    }

    void aNewGroupIsAddedWithItsDefaultColorAndAWarning()
    {
        const Song s = sample();
        const LyricsEdit e = applyLyricsText(s, lyricsText(s) + "\n\n[Bridge]\nThe Lord has promised good to me");
        QCOMPARE(labels(e.song), (QStringList{"Intro", "Verse 1", "Chorus", "Bridge"}));
        QVERIFY(!e.song.groups[3].id.isNull());
        QVERIFY(!e.song.groups[3].color.isEmpty());
        QVERIFY(e.warnings.join(' ').contains("Bridge"));
    }

    void aRemovedGroupLeavesTheArrangementsWithAWarning()
    {
        const Song s = sample();
        QString text = lyricsText(s);
        text = text.left(text.indexOf("\n\n[Chorus]"));   // the chorus is gone
        const LyricsEdit e = applyLyricsText(s, text);
        QCOMPARE(labels(e.song), (QStringList{"Intro", "Verse 1"}));
        const QList<QUuid> expected{s.groups[0].id, s.groups[1].id, s.groups[1].id};
        QCOMPARE(e.song.arrangements[0].sequence, expected);
        QVERIFY(e.warnings.join(' ').contains("Chorus"));
    }

    // #66: labels parseLabel() doesn't know used to read back as lyric lines.
    void customLabelsSurviveTheRoundTrip()
    {
        const Song s = withGroups({
            Group{QUuid::createUuid(), "Verse 1", "#0078cc", {"Amazing grace how sweet the sound"}},
            Group{QUuid::createUuid(), "Spoken Word", "", {"Read Psalm 23 here"}},
            Group{QUuid::createUuid(), "Tag A", "#cc7a00", {"Was blind but now I see"}},
            Group{QUuid::createUuid(), "Chorus x2", "#cc004f", {"My chains are gone"}},
        });
        verifyLossless(s);
        // Still the same groups after an edit elsewhere.
        QString text = lyricsText(s);
        text.replace("the sound", "the sound!");
        const LyricsEdit e = applyLyricsText(s, text);
        QCOMPARE(labels(e.song), (QStringList{"Verse 1", "Spoken Word", "Tag A", "Chorus x2"}));
        QCOMPARE(e.song.arrangements, s.arrangements);
    }

    void unlabelledGroupsSurviveTheRoundTrip()
    {
        const Song s = withGroups({
            Group{QUuid::createUuid(), "", "", {"Before any label"}},
            Group{QUuid::createUuid(), "Verse 1", "#0078cc", {"Amazing grace"}},
            Group{QUuid::createUuid(), "", "", {"Between labels"}},
        });
        QVERIFY(lyricsText(s).startsWith("[]\n"));
        verifyLossless(s);
    }

    void duplicateGroupsAreNotMerged()
    {
        const Song s = withGroups({
            Group{QUuid::createUuid(), "Chorus", "#cc004f", {"My chains are gone"}},
            Group{QUuid::createUuid(), "Verse 1", "#0078cc", {"Amazing grace"}},
            Group{QUuid::createUuid(), "Chorus", "#cc004f", {"My chains are gone"}},
            Group{QUuid::createUuid(), "Intro", "#b3a624", {""}},
            Group{QUuid::createUuid(), "Intro", "#b3a624", {""}},
        });
        verifyLossless(s);
    }

    // #67: blank slides (from ProPresenter, or lyric-less sequence entries) used to be
    // dropped from the text, so any edit deleted them.
    void blankSlidesSurviveAnEditElsewhere()
    {
        const Song s = withGroups({
            Group{QUuid::createUuid(), "Verse 1", "#0078cc", {"", "First", "", "Last", ""}},
            Group{QUuid::createUuid(), "Chorus", "#cc004f", {"My chains are gone"}},
            Group{QUuid::createUuid(), "Blank", "", {"", ""}},
        });
        QVERIFY(lyricsText(s).startsWith("[Verse 1]\n(blank)\n\nFirst\n\n(blank)\n\nLast\n\n(blank)\n\n[Chorus]"));
        verifyLossless(s);

        QString text = lyricsText(s);
        text.replace("are gone", "are gone!");
        const LyricsEdit e = applyLyricsText(s, text);
        QCOMPARE(e.song.groups[0].slides, (QStringList{"", "First", "", "Last", ""}));
        QCOMPARE(e.song.groups[2].slides, (QStringList{"", ""}));
        QCOMPARE(e.song.groups[1].slides, QStringList{"My chains are gone!"});
    }

    // PR #74 review: with two Choruses, renaming the first must keep each group's id, or
    // the arrangement plays the wrong lyrics.
    void renamingOneOfTwoDuplicatesKeepsEachGroup()
    {
        const Song s = withGroups({
            Group{QUuid::createUuid(), "Chorus", "#cc004f", {"First chorus"}},
            Group{QUuid::createUuid(), "Verse 1", "#0078cc", {"Amazing grace"}},
            Group{QUuid::createUuid(), "Chorus", "#cc004f", {"Second chorus"}},
        });
        QString text = lyricsText(s);
        text.replace(text.indexOf("[Chorus]"), 8, "[Refrain]");
        const LyricsEdit e = applyLyricsText(s, text);
        QCOMPARE(labels(e.song), (QStringList{"Refrain", "Verse 1", "Chorus"}));
        for (int i = 0; i < 3; ++i)
            QCOMPARE(e.song.groups[i].id, s.groups[i].id);
        QCOMPARE(e.song.arrangements, s.arrangements);
    }

    void removingTheFirstOfTwoDuplicatesKeepsTheSecond()
    {
        const Song s = withGroups({
            Group{QUuid::createUuid(), "Chorus", "#cc004f", {"First chorus"}},
            Group{QUuid::createUuid(), "Verse 1", "#0078cc", {"Amazing grace"}},
            Group{QUuid::createUuid(), "Chorus", "#cc004f", {"Second chorus"}},
        });
        QString text = lyricsText(s);
        text.remove(0, text.indexOf("[Verse 1]"));
        const LyricsEdit e = applyLyricsText(s, text);
        QCOMPARE(e.song.groups.size(), 2);
        QCOMPARE(e.song.groups[1].id, s.groups[2].id);
        QCOMPARE(e.song.groups[1].slides, QStringList{"Second chorus"});
    }

    // PR #74 review: lyrics that look like the editor's own syntax stay lyrics.
    void lyricsThatLookLikeEditorSyntaxSurvive()
    {
        const Song s = withGroups({
            Group{QUuid::createUuid(), "Verse 1", "#0078cc",
                  {"(blank)", "(BLANK)", "Amazing grace\n[Spoken Word]\nhow sweet", "[Tag]", "Chorus",
                   "\\back\\slash", "Verse 2:\nstill a lyric"}},
        });
        verifyLossless(s);
    }

    void aBracketedLyricKeptByAPasteStaysALyric()
    {
        Song s = songFromPastedLyrics("Psalm", "[Verse 1]\nThe Lord is my shepherd\n[Spoken Word]");
        QCOMPARE(s.groups.size(), 1);
        const LyricsEdit e = applyLyricsText(s, lyricsText(s));
        QCOMPARE(e.song.groups, s.groups);
    }

    void aGroupWithoutLyricsStaysABlankSlide()
    {
        const Song s = sample();
        const LyricsEdit e = applyLyricsText(s, lyricsText(s));
        QCOMPARE(e.song.groups[0].slides, QStringList{""});
    }
};

QTEST_GUILESS_MAIN(SongTextTest)
#include "tst_songtext.moc"
