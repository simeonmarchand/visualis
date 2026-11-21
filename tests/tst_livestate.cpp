#include "live/commands.h"
#include "live/livestate.h"

#include <QSignalSpy>
#include <QtTest>

using namespace visualis;

namespace {

// Intro (blank), a two-slide verse, and a chorus the arrangement repeats:
// slides are "", V1a, V1b, C, C.
Song sample()
{
    Song s;
    s.title = "Joy to the World";   // public domain
    s.groups = {
        Group{QUuid::createUuid(), "Intro", "#b3a624", {""}},
        Group{QUuid::createUuid(), "Verse 1", "#0078cc", {"Joy to the world", "Let earth receive her King"}},
        Group{QUuid::createUuid(), "Chorus", "#cc004f", {"And heaven and nature sing"}},
    };
    const auto &g = s.groups;
    s.arrangements = {Arrangement{QUuid::createUuid(), "Default", {g[0].id, g[1].id, g[2].id, g[2].id}, {}}};
    return s;
}

// Verse 1 (2 slides), Chorus, Verse 2, Bridge, and a Tag the arrangement doesn't use,
// arranged V1 C V2 C B C. Slides: 0 V1a, 1 V1b, 2 C, 3 V2, 4 C, 5 B, 6 C.
Song arranged()
{
    Song s;
    s.title = "Arranged";
    s.groups = {
        Group{QUuid::createUuid(), "Verse 1", "", {"v1a", "v1b"}},
        Group{QUuid::createUuid(), "Chorus", "", {"c"}},
        Group{QUuid::createUuid(), "Verse 2", "", {"v2"}},
        Group{QUuid::createUuid(), "Bridge", "", {"b"}},
        Group{QUuid::createUuid(), "Tag", "", {"t"}},
    };
    const auto &g = s.groups;
    s.arrangements = {Arrangement{QUuid::createUuid(), "Default",
                                  {g[0].id, g[1].id, g[2].id, g[1].id, g[3].id, g[1].id}, {}}};
    return s;
}

} // namespace

class LiveStateTest : public QObject
{
    Q_OBJECT

private slots:
    void startsWithNothingLive()
    {
        LiveState live;
        QVERIFY(!live.hasSlide());
        QCOMPARE(live.slideIndex(), -1);
        QVERIFY(live.currentText().isEmpty());
        QVERIFY(!live.next());
        QVERIFY(!live.previous());
        QVERIFY(!live.goToSlide(0));
    }

    void showTakesASongLive()
    {
        LiveState live;
        QSignalSpy spy(&live, &LiveState::changed);
        QVERIFY(live.show(sample(), {}, 1));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(live.songTitle(), QStringLiteral("Joy to the World"));
        QCOMPARE(live.slideCount(), 5);
        QCOMPARE(live.slideIndex(), 1);
        QCOMPARE(live.currentText(), QStringLiteral("Joy to the world"));
        QCOMPARE(live.nextText(), QStringLiteral("Let earth receive her King"));
        QCOMPARE(live.groupLabel(), QStringLiteral("Verse 1"));
        QCOMPARE(live.groupColor(), QStringLiteral("#0078cc"));
    }

    void goToGroupJumpsToTheNextOneAhead()
    {
        LiveState live;
        live.show(arranged(), {}, 3);   // Verse 2
        QVERIFY(live.goToGroupStartingWith("c"));
        QCOMPARE(live.slideIndex(), 4);   // the Chorus after Verse 2, not the first one
        QVERIFY(live.goToGroupStartingWith("C"));
        QCOMPARE(live.slideIndex(), 6);
        QVERIFY(live.goToGroupStartingWith("c"));   // none ahead: wraps to the first
        QCOMPARE(live.slideIndex(), 2);
    }

    void goToGroupStepsThroughGroupsWithTheSameLetter()
    {
        LiveState live;
        live.show(arranged(), {}, 0);
        QVERIFY(live.goToGroupStartingWith("v"));
        QCOMPARE(live.groupLabel(), QStringLiteral("Verse 2"));
        QVERIFY(live.goToGroupStartingWith("v"));
        QCOMPARE(live.groupLabel(), QStringLiteral("Verse 1"));
        QCOMPARE(live.slideIndex(), 0);   // its first slide
    }

    void goToGroupFromTheMiddleOfAGroupGoesToTheNextStart()
    {
        LiveState live;
        live.show(arranged(), {}, 1);   // second slide of Verse 1
        QVERIFY(live.goToGroupStartingWith("b"));
        QCOMPARE(live.slideIndex(), 5);
    }

    void goToGroupNotInTheArrangementDoesNothing()
    {
        LiveState live;
        live.show(arranged(), {}, 2);
        QSignalSpy spy(&live, &LiveState::changed);
        QVERIFY(!live.goToGroupStartingWith("t"));   // Tag isn't arranged
        QVERIFY(!live.goToGroupStartingWith("x"));
        QVERIFY(!live.goToGroupStartingWith(""));
        QCOMPARE(live.slideIndex(), 2);
        QCOMPARE(spy.count(), 0);
    }

    void goToGroupBringsBackClearedText()
    {
        LiveState live;
        live.show(arranged(), {}, 0);
        live.clearText();
        QVERIFY(live.goToGroupStartingWith("c"));
        QVERIFY(!live.textCleared());
    }

    void showBackgroundUndoesClearBackground()
    {
        LiveState live;
        live.clearAll();
        live.showBackground();
        QVERIFY(!live.backgroundCleared());
        QVERIFY(live.textCleared());   // only the background layer
    }

    void commandsRunByName()
    {
        LiveState live;
        live.show(arranged(), {}, 0);
        QVERIFY(runCommand(live, "next_slide"));
        QCOMPARE(live.slideIndex(), 1);
        QVERIFY(runCommand(live, "prev_slide"));
        QVERIFY(runCommand(live, "goto_slide", 5));
        QCOMPARE(live.slideIndex(), 5);
        QVERIFY(runCommand(live, "goto_group", "Chorus"));   // only the first letter counts
        QCOMPARE(live.slideIndex(), 6);
        QVERIFY(runCommand(live, "clear_text"));
        QVERIFY(live.textCleared());
        QVERIFY(runCommand(live, "clear_background"));
        QVERIFY(live.backgroundCleared());
        QVERIFY(runCommand(live, "show_background"));
        QVERIFY(!live.backgroundCleared());
        QVERIFY(runCommand(live, "clear_all"));
        QVERIFY(live.textCleared() && live.backgroundCleared());
    }

    void badCommandsAreRefused()
    {
        LiveState live;
        live.show(arranged(), {}, 6);
        QVERIFY(!runCommand(live, "next_slide"));       // already at the end
        QVERIFY(!runCommand(live, "goto_slide"));       // no index
        QVERIFY(!runCommand(live, "goto_slide", "x"));
        QVERIFY(!runCommand(live, "launch_rockets"));
        QCOMPARE(live.slideIndex(), 6);
        QCOMPARE(commandNames().size(), 8);
    }

    void songIdNamesTheLiveSong()
    {
        LiveState live;
        QVERIFY(live.songId().isEmpty());
        Song s = sample();
        s.id = QUuid::createUuid();
        live.show(s);
        QCOMPARE(live.songId(), s.id.toString(QUuid::WithoutBraces));
    }

    void nextCrossesGroups()
    {
        LiveState live;
        live.show(sample(), {}, 2);   // last verse slide
        QVERIFY(live.next());
        QCOMPARE(live.slideIndex(), 3);
        QCOMPARE(live.groupLabel(), QStringLiteral("Chorus"));
        QCOMPARE(live.groupColor(), QStringLiteral("#cc004f"));
        QVERIFY(live.previous());
        QCOMPARE(live.groupLabel(), QStringLiteral("Verse 1"));
        QCOMPARE(live.currentSlide()->indexInGroup, 1);
    }

    void nextIntoARepeatOfTheSameGroup()
    {
        LiveState live;
        live.show(sample(), {}, 3);
        QVERIFY(live.next());
        QCOMPARE(live.slideIndex(), 4);
        QCOMPARE(live.currentSlide()->step, 3);   // the second chorus, not the first
    }

    void stopsAtTheEnds()
    {
        LiveState live;
        live.show(sample(), {}, 4);
        QSignalSpy spy(&live, &LiveState::changed);
        QVERIFY(!live.next());
        QCOMPARE(live.slideIndex(), 4);
        QVERIFY(live.nextText().isEmpty());

        live.goToSlide(0);
        spy.clear();
        QVERIFY(!live.previous());
        QCOMPARE(live.slideIndex(), 0);
        QCOMPARE(spy.count(), 0);   // outputs don't redraw for a no-op
    }

    void goToSlideRejectsOutOfRange()
    {
        LiveState live;
        live.show(sample(), {}, 2);
        QVERIFY(!live.goToSlide(5));
        QVERIFY(!live.goToSlide(-1));
        QCOMPARE(live.slideIndex(), 2);
        QVERIFY(live.goToSlide(4));
        QCOMPARE(live.slideIndex(), 4);
    }

    void clearTextKeepsThePosition()
    {
        LiveState live;
        live.show(sample(), {}, 1);
        QSignalSpy spy(&live, &LiveState::changed);
        live.clearText();
        QVERIFY(live.textCleared());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(live.slideIndex(), 1);
        // The text stays readable; each output decides what clearing hides.
        QCOMPARE(live.currentText(), QStringLiteral("Joy to the world"));
        live.clearText();
        QCOMPARE(spy.count(), 1);   // already cleared
    }

    void stepAfterClearTextShowsTheNextSlide()
    {
        LiveState live;
        live.show(sample(), {}, 1);
        live.clearText();
        QVERIFY(live.next());
        QCOMPARE(live.slideIndex(), 2);
        QVERIFY(!live.textCleared());
    }

    void goToTheCurrentSlideBringsTextBack()
    {
        LiveState live;
        live.show(sample(), {}, 1);
        live.clearText();
        QVERIFY(live.goToSlide(1));
        QVERIFY(!live.textCleared());
    }

    void clearTextAtTheEndStaysCleared()
    {
        LiveState live;
        live.show(sample(), {}, 4);
        live.clearText();
        QVERIFY(!live.next());
        QVERIFY(live.textCleared());
    }

    void clearBackgroundLeavesTheText()
    {
        LiveState live;
        live.show(sample(), {}, 1);
        live.clearBackground();
        QVERIFY(live.backgroundCleared());
        QVERIFY(!live.textCleared());
        // Moving between slides doesn't bring the background back.
        live.next();
        QVERIFY(live.backgroundCleared());
    }

    void clearAllClearsBothAndKeepsThePosition()
    {
        LiveState live;
        live.show(sample(), {}, 2);
        QSignalSpy spy(&live, &LiveState::changed);
        live.clearAll();
        QVERIFY(live.textCleared());
        QVERIFY(live.backgroundCleared());
        QCOMPARE(spy.count(), 1);
        live.clearAll();
        QCOMPARE(spy.count(), 1);

        QVERIFY(live.next());   // carries on from where it was
        QCOMPARE(live.slideIndex(), 3);
        QVERIFY(!live.textCleared());
    }

    void showingANewSongBringsTextBack()
    {
        LiveState live;
        live.show(sample(), {}, 1);
        live.clearText();
        live.show(sample());
        QVERIFY(!live.textCleared());
        QCOMPARE(live.slideIndex(), 0);
    }

    void showWithABadIndexKeepsTheSongLiveWithNoSlide()
    {
        LiveState live;
        QVERIFY(!live.show(sample(), {}, 9));
        QCOMPARE(live.songTitle(), QStringLiteral("Joy to the World"));
        QVERIFY(!live.hasSlide());
        QVERIFY(live.nextText().isEmpty());
        QVERIFY(live.next());   // Next lands on the first slide
        QCOMPARE(live.slideIndex(), 0);
    }

    void emptyArrangementHasNoSlides()
    {
        Song s = sample();
        s.arrangements = {Arrangement{QUuid::createUuid(), "Empty", {}, {}}};
        LiveState live;
        QVERIFY(!live.show(s));
        QCOMPARE(live.slideCount(), 0);
        QVERIFY(!live.hasSlide());
        QVERIFY(!live.next());
        QVERIFY(!live.previous());
    }

    void showsTheRequestedArrangement()
    {
        Song s = sample();
        s.arrangements << Arrangement{QUuid::createUuid(), "Chorus only", {s.groups[2].id}, {}};
        LiveState live;
        QVERIFY(live.show(s, s.arrangements[1].id));
        QCOMPARE(live.slideCount(), 1);
        QCOMPARE(live.groupLabel(), QStringLiteral("Chorus"));
    }

    // Outputs bind to properties by name (QML); check they're readable through the meta-object.
    void propertiesAreReadableByName()
    {
        LiveState live;
        live.show(sample(), {}, 3);
        live.clearBackground();
        QCOMPARE(live.property("currentText").toString(), QStringLiteral("And heaven and nature sing"));
        QCOMPARE(live.property("groupLabel").toString(), QStringLiteral("Chorus"));
        QCOMPARE(live.property("slideIndex").toInt(), 3);
        QCOMPARE(live.property("backgroundCleared").toBool(), true);
    }
};

QTEST_GUILESS_MAIN(LiveStateTest)
#include "tst_livestate.moc"
