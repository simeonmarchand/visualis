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

ServiceItem songItem(const Song &song)
{
    return ServiceItem{QUuid::createUuid(), ServiceItem::Kind::Song, song.title, song.id, {}, {}, {}, {}, {}, {}, {}};
}

ServiceItem header(const QString &title)
{
    return ServiceItem{QUuid::createUuid(), ServiceItem::Kind::Header, title, {}, {}, {}, {}, {}, {}, {}, {}};
}

ServiceItem textItem(const QString &title, const QString &body)
{
    return ServiceItem{QUuid::createUuid(), ServiceItem::Kind::Text, title, {}, {}, {}, body, {}, {}, {}, {}};
}

// A header, sample() (5 slides), a placeholder, arranged() (7 slides), a text slide:
// service items 0..4, with slides only at 1, 3, and 4.
QList<LiveItem> service()
{
    Song a = sample();
    a.id = QUuid::createUuid();
    Song b = arranged();
    b.id = QUuid::createUuid();
    ServiceItem missing = songItem(b);
    missing.kind = ServiceItem::Kind::Placeholder;
    return {liveItem(header("Worship"), std::nullopt), liveItem(songItem(a), a), liveItem(missing, std::nullopt),
            liveItem(songItem(b), b), liveItem(textItem("Welcome", "Welcome!"), std::nullopt)};
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
        QCOMPARE(commandNames().size(), 9);
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

    // #51: a service, run from Next and Previous.
    void nextStartsTheServiceAndCrossesItemsSkippingThoseWithoutSlides()
    {
        LiveState live;
        const QUuid id = QUuid::createUuid();
        live.setService(id, service());
        QVERIFY(!live.hasSlide());
        QVERIFY(live.next());   // nothing live: the first item with slides (past the header)
        QCOMPARE(live.itemIndex(), 1);
        QCOMPARE(live.slideIndex(), 0);
        QCOMPARE(live.serviceIdString(), id.toString(QUuid::WithoutBraces));
        QVERIFY(live.goToSlide(4));
        QVERIFY(live.next());   // past the placeholder
        QCOMPARE(live.itemIndex(), 3);
        QCOMPARE(live.slideIndex(), 0);
        QCOMPARE(live.songTitle(), QStringLiteral("Arranged"));
        QVERIFY(live.goToSlide(6));
        QVERIFY(live.next());
        QCOMPARE(live.itemIndex(), 4);
        QCOMPARE(live.currentText(), QStringLiteral("Welcome!"));
        QVERIFY(!live.next());   // the end of the service
        QCOMPARE(live.itemIndex(), 4);
    }

    void previousGoesToThePreviousItemsLastSlide()
    {
        LiveState live;
        live.setService(QUuid::createUuid(), service());
        QVERIFY(live.goToItem(3));
        live.clearText();
        QVERIFY(live.previous());
        QCOMPARE(live.itemIndex(), 1);
        QCOMPARE(live.slideIndex(), 4);
        QVERIFY(!live.textCleared());
        QVERIFY(live.goToSlide(0));
        QVERIFY(!live.previous());   // only a header before it
        QCOMPARE(live.itemIndex(), 1);
    }

    void goToItemTakesAnItemLiveAtASlide()
    {
        LiveState live;
        live.setService(QUuid::createUuid(), service());
        QVERIFY(live.goToItem(3, 2));
        QCOMPARE(live.slideIndex(), 2);
        QVERIFY(live.goToItem(1, -1));
        QCOMPARE(live.slideIndex(), 4);
        QVERIFY(!live.goToItem(0));      // a header
        QVERIFY(!live.goToItem(2));      // a placeholder
        QVERIFY(!live.goToItem(3, 7));
        QVERIFY(!live.goToItem(5));
        QCOMPARE(live.itemIndex(), 1);
        QCOMPARE(live.slideIndex(), 4);
    }

    void gotoItemCommand()
    {
        LiveState live;
        live.setService(QUuid::createUuid(), service());
        QVERIFY(runCommand(live, "goto_item", 3));
        QCOMPARE(live.itemIndex(), 3);
        QVERIFY(runCommand(live, "goto_item", QVariantList{1, 2}));
        QCOMPARE(live.itemIndex(), 1);
        QCOMPARE(live.slideIndex(), 2);
        QVERIFY(runCommand(live, "goto_item", "4"));   // as --command goto_item=4 passes it
        QCOMPARE(live.itemIndex(), 4);
        QVERIFY(!runCommand(live, "goto_item"));
        QVERIFY(!runCommand(live, "goto_item", "x"));
        QVERIFY(!runCommand(live, "goto_item", QVariantList{1, "x"}));
        QVERIFY(!runCommand(live, "goto_item", 0));
        QCOMPARE(live.itemIndex(), 4);
    }

    void aSongFromTheLibraryIsNotAServiceItem()
    {
        LiveState live;
        live.setService(QUuid::createUuid(), service());
        QVERIFY(live.goToItem(1));
        live.show(arranged(), {}, 6);
        QCOMPARE(live.itemIndex(), -1);
        QVERIFY(live.itemIdString().isEmpty());
        QVERIFY(!live.next());   // stops at the song's end
        QVERIFY(live.goToSlide(0));
        QVERIFY(!live.previous());
    }

    void aChangedServiceCarriesOnFromTheLiveItem()
    {
        LiveState live;
        const QUuid id = QUuid::createUuid();
        QList<LiveItem> items = service();
        live.setService(id, items);
        QVERIFY(live.goToItem(1, 4));
        items.move(1, 4);   // the live song moved to the end
        QSignalSpy spy(&live, &LiveState::changed);
        live.setService(id, items);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(live.itemIndex(), 4);
        QCOMPARE(live.slideIndex(), 4);   // what's showing doesn't move
        QVERIFY(!live.next());
        QVERIFY(live.goToSlide(0));
        QVERIFY(live.previous());
        QCOMPARE(live.itemIndex(), 3);   // Welcome, now before it

        items.removeAt(3);   // the live item taken out: it keeps showing, and Next stays in it
        live.setService(id, items);
        QCOMPARE(live.itemIndex(), -1);
        QCOMPARE(live.currentText(), QStringLiteral("Welcome!"));
        QVERIFY(!live.next());
        QVERIFY(!live.previous());
    }

    void restoreThenSetServicePicksTheServiceBackUp()
    {
        const QList<LiveItem> items = service();
        const QUuid id = QUuid::createUuid();
        LiveState live;
        live.restore(items[3].song, items[3].arrangement, 6, true, false, id, items[3].id);
        QCOMPARE(live.itemIndex(), -1);   // the service isn't loaded yet
        QCOMPARE(live.itemIdString(), items[3].id.toString(QUuid::WithoutBraces));
        QVERIFY(!live.next());
        live.setService(id, items);
        QCOMPARE(live.itemIndex(), 3);
        QVERIFY(live.textCleared());
        QVERIFY(live.next());
        QCOMPARE(live.itemIndex(), 4);
    }

    void liveItemsForEachKind()
    {
        Song song = arranged();
        song.id = QUuid::createUuid();
        ServiceItem item = songItem(song);
        QCOMPARE(liveItem(item, song).slides.size(), 7);
        QVERIFY(liveItem(item, std::nullopt).slides.isEmpty());   // the song couldn't load

        song.arrangements << Arrangement{QUuid::createUuid(), "Short", {song.groups[3].id}, {}};
        item.arrangement = song.arrangements[1].id;
        QCOMPARE(liveItem(item, song).slides.size(), 1);

        // Planning Center's order for the day, by label; a label naming no group is left out.
        item.customSequence = {"chorus", "Verse 2", "Outro", "Chorus"};
        const LiveItem custom = liveItem(item, song);
        QStringList labels;
        for (const SongSlide &s : custom.slides)
            labels << s.groupLabel;
        QCOMPARE(labels, (QStringList{"Chorus", "Verse 2", "Chorus"}));
        QCOMPARE(custom.arrangement, item.id);
        QCOMPARE(songSlides(custom.song, custom.arrangement), custom.slides);   // the journal can rebuild it

        item.customSequence = {"Outro"};   // nothing matches: the arrangement stands
        QCOMPARE(liveItem(item, song).slides.size(), 1);

        const LiveItem text = liveItem(textItem("Welcome", "Welcome!\nSit anywhere"), std::nullopt);
        QCOMPARE(text.slides.size(), 1);
        QCOMPARE(text.slides[0].text, QStringLiteral("Welcome!\nSit anywhere"));
        QCOMPARE(text.song.title, QStringLiteral("Welcome"));
        const LiveItem blank = liveItem(textItem("Blank", ""), std::nullopt);
        QCOMPARE(blank.slides.size(), 1);
        QVERIFY(blank.slides[0].isBlank());
        QVERIFY(liveItem(header("Worship"), std::nullopt).slides.isEmpty());
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
