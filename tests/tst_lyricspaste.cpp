#include "library/library.h"
#include "song/lyricspaste.h"
#include "song/songstore.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace visualis;

// Sample lyrics are public-domain hymns; the repo holds no copyrighted lyrics.
namespace {

QStringList labels(const PastedLyrics &p)
{
    QStringList out;
    for (const Group &g : p.groups)
        out << g.label;
    return out;
}

// Labels in pasted order, with repeats.
QStringList order(const PastedLyrics &p)
{
    QStringList out;
    for (const QUuid &id : p.sequence) {
        for (const Group &g : p.groups) {
            if (g.id == id)
                out << g.label;
        }
    }
    return out;
}

const char *amazingGrace = R"(Verse 1
Amazing grace how sweet the sound
That saved a wretch like me

I once was lost but now am found
Was blind but now I see

Verse 2
'Twas grace that taught my heart to fear
And grace my fears relieved
)";

} // namespace

class LyricsPasteTest : public QObject
{
    Q_OBJECT

private slots:
    void headersAndBlankLinesMakeGroupsAndSlides()
    {
        const PastedLyrics p = parsePastedLyrics(amazingGrace);
        QCOMPARE(labels(p), (QStringList{"Verse 1", "Verse 2"}));
        QCOMPARE(p.groups[0].slides, (QStringList{"Amazing grace how sweet the sound\nThat saved a wretch like me",
                                                  "I once was lost but now am found\nWas blind but now I see"}));
        QCOMPARE(p.groups[1].slides.size(), 1);
        QVERIFY(p.warnings.isEmpty());
    }

    void headerSpellings_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("label");
        QTest::addColumn<int>("repeat");
        QTest::newRow("plain") << "Chorus" << "Chorus" << 1;
        QTest::newRow("colon") << "Chorus:" << "Chorus" << 1;
        QTest::newRow("brackets") << "[Verse 2]" << "Verse 2" << 1;
        QTest::newRow("parentheses") << "(Bridge)" << "Bridge" << 1;
        QTest::newRow("upper case") << "PRE-CHORUS 1" << "PRE-CHORUS 1" << 1;
        QTest::newRow("x2 in parentheses") << "Chorus (x2)" << "Chorus" << 2;
        QTest::newRow("x3 in brackets") << "[Chorus x3]" << "Chorus" << 3;
        QTest::newRow("2x after colon") << "Chorus: 2x" << "Chorus" << 2;
        QTest::newRow("multiplication sign") << "Tag ×4" << "Tag" << 4;
        QTest::newRow("repeat") << "Chorus (Repeat)" << "Chorus" << 1;
        QTest::newRow("language") << "Spanish - Chorus 1" << "Spanish - Chorus 1" << 1;
    }
    void headerSpellings()
    {
        QFETCH(QString, line);
        QFETCH(QString, label);
        QFETCH(int, repeat);
        const PastedLyrics p = parsePastedLyrics(line + "\nHoly, holy, holy\nLord God Almighty\n");
        QCOMPARE(labels(p), QStringList{label});
        QCOMPARE(p.sequence.size(), repeat);
    }

    void lyricLinesThatMentionSectionsStayLyrics()
    {
        const PastedLyrics p = parsePastedLyrics(
            "Verse 1\nBridge over the river wide\nThe chorus of the angels sings\nTag along with me\n", {4, {}});
        QCOMPARE(labels(p), QStringList{"Verse 1"});
        QCOMPARE(p.groups[0].slides[0].count('\n'), 2);   // all three lines kept as lyrics
    }

    void repeatedHeadersRepeatTheGroup()
    {
        const PastedLyrics p = parsePastedLyrics(
            "Verse 1\nHoly, holy, holy\n\nChorus\nLord God Almighty\n\nVerse 2\nEarly in the morning\n\n"
            "Chorus\n\n"                                   // repeat, no text
            "Chorus\nLord God Almighty\n");                // repeat, same text
        QCOMPARE(labels(p), (QStringList{"Verse 1", "Chorus", "Verse 2"}));
        QCOMPARE(order(p), (QStringList{"Verse 1", "Chorus", "Verse 2", "Chorus", "Chorus"}));
        QVERIFY(p.warnings.isEmpty());
    }

    void sameLabelWithDifferentTextIsKeptAndWarned()
    {
        const PastedLyrics p = parsePastedLyrics("Chorus\nLord God Almighty\n\nChorus\nGod in three persons\n");
        QCOMPARE(labels(p), (QStringList{"Chorus", "Chorus"}));
        QCOMPARE(p.warnings.size(), 1);
        QVERIFY2(p.warnings[0].contains("Chorus"), qPrintable(p.warnings[0]));
    }

    void textBeforeTheFirstHeaderIsUnlabelled()
    {
        const PastedLyrics p = parsePastedLyrics("Holy, holy, holy\n\nChorus\nLord God Almighty\n");
        QCOMPARE(labels(p), (QStringList{"", "Chorus"}));
        QVERIFY(p.warnings.isEmpty());
    }

    void noHeadersAtAllIsOneGroupWithAWarning()
    {
        const PastedLyrics p = parsePastedLyrics("Holy, holy, holy\nLord God Almighty\n\nEarly in the morning\n");
        QCOMPARE(labels(p), QStringList{""});
        QCOMPARE(p.groups[0].slides.size(), 2);
        QCOMPARE(p.warnings.size(), 1);
    }

    void instrumentalHeadersGetABlankSlide()
    {
        const PastedLyrics p = parsePastedLyrics("Intro\n\nVerse 1\nHoly, holy, holy\n\nInterlude\n");
        QCOMPARE(labels(p), (QStringList{"Intro", "Verse 1", "Interlude"}));
        QCOMPARE(p.groups[0].slides, QStringList{QString()});
        QCOMPARE(p.groups[2].slides, QStringList{QString()});
    }

    void longStanzasSplitEvenly()
    {
        auto lines = [](int n) {
            QStringList l;
            for (int i = 1; i <= n; ++i)
                l << QStringLiteral("line %1").arg(i);
            return "Verse 1\n" + l.join('\n');
        };
        auto sizes = [](const PastedLyrics &p) {
            QList<int> out;
            for (const QString &s : p.groups[0].slides)
                out << s.count('\n') + 1;
            return out;
        };
        const PasteOptions four{4, {}};
        QCOMPARE(sizes(parsePastedLyrics(lines(4), four)), QList<int>{4});
        QCOMPARE(sizes(parsePastedLyrics(lines(6), four)), (QList<int>{3, 3}));      // not 4 + 2
        QCOMPARE(sizes(parsePastedLyrics(lines(7), four)), (QList<int>{4, 3}));
        QCOMPARE(sizes(parsePastedLyrics(lines(9), four)), (QList<int>{3, 3, 3}));
        QCOMPARE(sizes(parsePastedLyrics(lines(5), {2, {}})), (QList<int>{2, 2, 1}));
    }

    void defaultIsTwoLinesPerSlide()
    {
        const PastedLyrics p = parsePastedLyrics("Verse 1\nline 1\nline 2\nline 3\nline 4\n");
        QCOMPARE(p.groups[0].slides.size(), 2);
        QCOMPARE(p.groups[0].slides[0], QStringLiteral("line 1\nline 2"));
    }

    void songSelectPaste()
    {
        // The shape SongSelect's "copy lyrics" gives: title, sections, then credit lines.
        const QString text =
            "Holy Holy Holy\r\n\r\nVerse 1\r\nHoly, holy, holy\r\nLord God Almighty\r\n\r\n"
            "Verse 2\r\nHoly, holy, holy\r\nAll the saints adore Thee\r\n\r\n"
            "CCLI Song # 1234567\r\nReginald Heber | John Bacchus Dykes\r\n"
            "© Words: Public Domain\r\n"
            "For use solely with the SongSelect® Terms of Use. All rights reserved. www.ccli.com\r\n"
            "CCLI License # 7654321\r\n";
        QStringList warnings;
        const Song s = songFromPastedLyrics("Holy Holy Holy", text, {}, &warnings);
        QCOMPARE(s.groups.size(), 2);
        QCOMPARE(s.groups[0].label, QStringLiteral("Verse 1"));   // the title line was dropped
        QCOMPARE(s.credits.ccliNumber, QStringLiteral("1234567"));   // the song's number, not the license
        QCOMPARE(s.credits.authors, QStringLiteral("Reginald Heber, John Bacchus Dykes"));
        QCOMPARE(s.credits.copyright, QStringLiteral("Words: Public Domain"));
        for (const Group &g : s.groups) {
            for (const QString &slide : g.slides)
                QVERIFY2(!slide.contains("CCLI") && !slide.contains("©"), qPrintable(slide));
        }
        QVERIFY(warnings.isEmpty());
    }

    void multiTracksPaste()
    {
        // The shape copied from MultiTracks: plain headers each followed by a blank line, every
        // section one block of short phrase lines, and repeated sections written out in full.
        const QString text =
            "Verse 1\n\nJoy to the world\nThe Lord is come\nLet earth\nReceive her King\n"
            "Let every heart\nPrepare Him room\n\n"
            "Chorus 1\n\nAnd heaven and nature sing\nAnd heaven and nature sing\n\n"
            "Verse 2\n\nJoy to the earth\nThe Savior reigns\nLet men their songs employ\n\n"
            "Chorus 1\n\nAnd heaven and nature sing\nAnd heaven and nature sing\n\n"
            "Chorus 2\n\nAnd heaven and nature sing\n\n"
            "Outro\n\nJoy to the world\nJoy to the world\n";
        const PastedLyrics p = parsePastedLyrics(text, {2, {}});
        QCOMPARE(labels(p), (QStringList{"Verse 1", "Chorus 1", "Verse 2", "Chorus 2", "Outro"}));
        QCOMPARE(order(p), (QStringList{"Verse 1", "Chorus 1", "Verse 2", "Chorus 1", "Chorus 2", "Outro"}));
        QCOMPARE(p.groups[0].slides, (QStringList{"Joy to the world\nThe Lord is come", "Let earth\nReceive her King",
                                                  "Let every heart\nPrepare Him room"}));
        QVERIFY(p.warnings.isEmpty());
    }

    void messyWhitespace()
    {
        const PastedLyrics p = parsePastedLyrics(
            "\n\n  Verse 1:  \n\tHoly, holy, holy   \nLord God Almighty\n\n\n\n\nEarly in the morning\n\n");
        QCOMPARE(p.groups.size(), 1);
        QCOMPARE(p.groups[0].slides, (QStringList{"Holy, holy, holy\nLord God Almighty", "Early in the morning"}));
    }

    void emptyInput()
    {
        const PastedLyrics p = parsePastedLyrics("   \n\n");
        QVERIFY(p.groups.isEmpty());
        QVERIFY(p.sequence.isEmpty());
        QVERIFY(p.warnings.isEmpty());
    }

    void defaultColorsByKind()
    {
        QCOMPARE(defaultGroupColor("Verse 3"), QStringLiteral("#0078cc"));
        QCOMPARE(defaultGroupColor("Chorus"), QStringLiteral("#cc004f"));
        QCOMPARE(defaultGroupColor("Spanish - Bridge 1"), QStringLiteral("#7500cc"));
        QCOMPARE(defaultGroupColor("Spoken Word"), QString());
        QCOMPARE(parsePastedLyrics("Chorus\nLord God Almighty").groups[0].color, QStringLiteral("#cc004f"));
    }

    void arrangementOnlyWhenTheOrderNeedsOne()
    {
        const Song plain = songFromPastedLyrics("Amazing Grace", amazingGrace);
        QVERIFY(plain.arrangements.isEmpty());   // groups in order already say it all

        const Song repeated = songFromPastedLyrics("Holy", "Verse 1\nHoly\n\nChorus (x2)\nAlmighty\n");
        QCOMPARE(repeated.arrangements.size(), 1);
        QCOMPARE(repeated.defaultArrangement, repeated.arrangements[0].id);
        QCOMPARE(repeated.playOrder().size(), 3);
    }

    // Only the editor's own text takes any "[...]" line as a header (#66); a paste from
    // outside keeps bracketed lyric lines that aren't section labels.
    void pastedBracketedLinesAreLyricsUnlessTheyAreLabels()
    {
        const PastedLyrics p = parsePastedLyrics("[Verse 1]\nAmazing grace\n[Spoken Word]\nHow sweet the sound");
        QCOMPARE(labels(p), QStringList{"Verse 1"});
        QCOMPARE(p.groups[0].slides.join('\n'), QStringLiteral("Amazing grace\n[Spoken Word]\nHow sweet the sound"));
    }

    void pastedSongsSave()
    {
        QTemporaryDir tmp;
        auto lib = Library::open(tmp.path()).library;
        QVERIFY(lib);
        Song s = songFromPastedLyrics("Holy", "Intro\n\nVerse 1\nHoly\n\nChorus (x2)\nAlmighty\n\nInterlude\n");
        const SongStore::Result r = SongStore(*lib).save(s);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(*SongStore(*lib).load(s.id), s);
    }
};

QTEST_GUILESS_MAIN(LyricsPasteTest)
#include "tst_lyricspaste.moc"
