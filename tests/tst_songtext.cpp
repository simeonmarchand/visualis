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

    void aGroupWithoutLyricsStaysABlankSlide()
    {
        const Song s = sample();
        const LyricsEdit e = applyLyricsText(s, lyricsText(s));
        QCOMPARE(e.song.groups[0].slides, QStringList{""});
    }
};

QTEST_GUILESS_MAIN(SongTextTest)
#include "tst_songtext.moc"
