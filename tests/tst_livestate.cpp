#include "live/commands.h"
#include "live/liveitem.h"
#include "live/livestate.h"

#include <QSet>
#include <QSignalSpy>
#include <QtTest>

using namespace visualis;

namespace {

// A song taken live from the library, through the built-in template.
LiveItem libraryItem(const Song &song, const QUuid &arrangement = {})
{
    return liveSong(song, arrangement, templateSet({}));
}

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
    return {liveItem(header("Worship"), std::nullopt, templateSet({})), liveItem(songItem(a), a, templateSet({})), liveItem(missing, std::nullopt, templateSet({})),
            liveItem(songItem(b), b, templateSet({})), liveItem(textItem("Welcome", "Welcome!"), std::nullopt, templateSet({}))};
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

// A media check that knows the given assets, the rest missing, and counts its calls.
struct FakeMedia {
    QSet<QUuid> present;
    std::shared_ptr<int> calls = std::make_shared<int>(0);
    MediaCheckFn fn() const
    {
        return [present = present, calls = calls](const QUuid &id) {
            ++*calls;
            MediaCheck c;
            c.status = present.contains(id) ? MediaStatus::Ok : MediaStatus::Missing;
            c.path = present.contains(id) ? QStringLiteral("/media/still.jpg") : QString();
            c.message = present.contains(id) ? QString() : QStringLiteral("\"Sunrise.jpg\" is missing from the media folder.");
            return c;
        };
    }
};

const Transition kFade{TransitionKind::Fade, 800};

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

    // #93: the outputs bind to the live slide's content layer.
    void contentFollowsTheLiveSlide()
    {
        LiveState live;
        QCOMPARE(live.content(), Content());
        const LiveItem item = libraryItem(sample());
        QVERIFY(live.show(item, 1));
        QCOMPARE(live.content(), item.slides[1].content);
        QCOMPARE(live.content().regions["main"].text, QStringLiteral("Joy to the world"));
        live.next();
        QCOMPARE(live.content(), item.slides[2].content);
        live.clearText();   // each output decides what clearing hides, as with currentText
        QCOMPARE(live.content(), item.slides[2].content);
    }

    // #93: after the live song or its template changes, what's live is the new rendering, at
    // the same slide.
    void updateSwapsInANewRenderingAtTheSameSlide()
    {
        LiveState live;
        Song song = sample();
        song.id = QUuid::createUuid();
        live.show(libraryItem(song), 3);
        Template t = builtInLyricsTemplate();
        t.id = QUuid::createUuid();
        t.regions[0].style.fontFamily = "Georgia";
        song.defaultTemplate = t.id;
        QSignalSpy spy(&live, &LiveState::changed);
        QVERIFY(live.update(liveSong(song, {}, templateSet({t}))));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(live.slideIndex(), 3);
        QCOMPARE(live.content().regions["main"].style.fontFamily, QStringLiteral("Georgia"));

        song.groups.removeLast();   // the chorus: 5 slides become 3
        QVERIFY(live.update(libraryItem(song)));
        QCOMPARE(live.slideIndex(), 2);   // clamped to the last one it has now
        QCOMPARE(live.slideCount(), 3);

        Song other = sample();
        other.id = QUuid::createUuid();
        QVERIFY(!live.update(libraryItem(other)));   // not what's live: nothing changes
        QCOMPARE(live.songId(), song.id.toString(QUuid::WithoutBraces));
    }

    void showTakesASongLive()
    {
        LiveState live;
        QSignalSpy spy(&live, &LiveState::changed);
        QVERIFY(live.show(libraryItem(sample()), 1));
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
        live.show(libraryItem(arranged()), 3);   // Verse 2
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
        live.show(libraryItem(arranged()), 0);
        QVERIFY(live.goToGroupStartingWith("v"));
        QCOMPARE(live.groupLabel(), QStringLiteral("Verse 2"));
        QVERIFY(live.goToGroupStartingWith("v"));
        QCOMPARE(live.groupLabel(), QStringLiteral("Verse 1"));
        QCOMPARE(live.slideIndex(), 0);   // its first slide
    }

    void goToGroupFromTheMiddleOfAGroupGoesToTheNextStart()
    {
        LiveState live;
        live.show(libraryItem(arranged()), 1);   // second slide of Verse 1
        QVERIFY(live.goToGroupStartingWith("b"));
        QCOMPARE(live.slideIndex(), 5);
    }

    void goToGroupNotInTheArrangementDoesNothing()
    {
        LiveState live;
        live.show(libraryItem(arranged()), 2);
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
        live.show(libraryItem(arranged()), 0);
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
        live.show(libraryItem(arranged()), 0);
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
        live.show(libraryItem(arranged()), 6);
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
        live.show(libraryItem(s));
        QCOMPARE(live.songId(), s.id.toString(QUuid::WithoutBraces));
    }

    void nextCrossesGroups()
    {
        LiveState live;
        live.show(libraryItem(sample()), 2);   // last verse slide
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
        live.show(libraryItem(sample()), 3);
        QVERIFY(live.next());
        QCOMPARE(live.slideIndex(), 4);
        QCOMPARE(live.currentSlide()->step, 3);   // the second chorus, not the first
    }

    void stopsAtTheEnds()
    {
        LiveState live;
        live.show(libraryItem(sample()), 4);
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
        live.show(libraryItem(sample()), 2);
        QVERIFY(!live.goToSlide(5));
        QVERIFY(!live.goToSlide(-1));
        QCOMPARE(live.slideIndex(), 2);
        QVERIFY(live.goToSlide(4));
        QCOMPARE(live.slideIndex(), 4);
    }

    void clearTextKeepsThePosition()
    {
        LiveState live;
        live.show(libraryItem(sample()), 1);
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
        live.show(libraryItem(sample()), 1);
        live.clearText();
        QVERIFY(live.next());
        QCOMPARE(live.slideIndex(), 2);
        QVERIFY(!live.textCleared());
    }

    void goToTheCurrentSlideBringsTextBack()
    {
        LiveState live;
        live.show(libraryItem(sample()), 1);
        live.clearText();
        QVERIFY(live.goToSlide(1));
        QVERIFY(!live.textCleared());
    }

    void clearTextAtTheEndStaysCleared()
    {
        LiveState live;
        live.show(libraryItem(sample()), 4);
        live.clearText();
        QVERIFY(!live.next());
        QVERIFY(live.textCleared());
    }

    void clearBackgroundLeavesTheText()
    {
        LiveState live;
        live.show(libraryItem(sample()), 1);
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
        live.show(libraryItem(sample()), 2);
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
        live.show(libraryItem(sample()), 1);
        live.clearText();
        live.show(libraryItem(sample()));
        QVERIFY(!live.textCleared());
        QCOMPARE(live.slideIndex(), 0);
    }

    void showWithABadIndexKeepsTheSongLiveWithNoSlide()
    {
        LiveState live;
        QVERIFY(!live.show(libraryItem(sample()), 9));
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
        QVERIFY(!live.show(libraryItem(s)));
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
        QVERIFY(live.show(libraryItem(s, s.arrangements[1].id)));
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
        live.show(libraryItem(arranged()), 6);
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

    // #93: setService() after a song edit refreshes the live item too (it used to keep
    // showing the old copy until the operator moved to another item).
    void setServiceRefreshesTheLiveItem()
    {
        QList<LiveItem> items = service();
        const QUuid id = QUuid::createUuid();
        LiveState live;
        live.setService(id, items);
        QVERIFY(live.goToItem(3, 1));
        const QString before = live.currentText();

        items[3].slides[1].slide.text = "edited";
        items[3].slides[1].content.regions["main"].text = "edited";
        live.setService(id, items);
        QCOMPARE(live.itemIndex(), 3);
        QCOMPARE(live.slideIndex(), 1);
        QVERIFY(live.currentText() != before);
        QCOMPARE(live.currentText(), QStringLiteral("edited"));
        QCOMPARE(live.content().regions["main"].text, QStringLiteral("edited"));
    }

    // #93 review: never a blank output. A live song deleted (its item now has no slides) or
    // grown past the limits keeps showing as it was, flagged, until the operator moves on.
    void aLiveItemThatCantShowAnyMoreStaysUpFlagged()
    {
        QList<LiveItem> items = service();
        const QUuid id = QUuid::createUuid();
        LiveState live;
        live.setService(id, items);
        QVERIFY(live.goToItem(3, 1));
        const QString shown = live.currentText();
        const Content content = live.content();
        QVERIFY(live.heldBack().isEmpty());

        LiveItem gone = liveItem(*items[3].source, std::nullopt, templateSet({}));   // song deleted
        items[3] = gone;
        live.setService(id, items);
        QCOMPARE(live.currentText(), shown);
        QCOMPARE(live.content(), content);
        QCOMPARE(live.itemIndex(), 3);
        QCOMPARE(live.slideIndex(), 1);
        QVERIFY(!live.heldBack().isEmpty());

        LiveItem tooBig = items[3];
        tooBig.error = "too many slides";
        QVERIFY(live.update(tooBig));   // the live one, but it can't show
        QCOMPARE(live.currentText(), shown);
        QVERIFY(!live.heldBack().isEmpty());

        QVERIFY(live.next());   // still the copy that was on screen
        QVERIFY(!live.heldBack().isEmpty());
        QVERIFY(live.goToItem(1, 0));   // another item: the flag goes
        QVERIFY(live.heldBack().isEmpty());
    }

    void restoreThenSetServicePicksTheServiceBackUp()
    {
        const QList<LiveItem> items = service();
        const QUuid id = QUuid::createUuid();
        LiveState live;
        live.restore(items[3], 6, true, {}, id);
        QCOMPARE(live.itemIndex(), -1);   // the service isn't loaded yet
        QCOMPARE(live.itemIdString(), items[3].id().toString(QUuid::WithoutBraces));
        QVERIFY(!live.next());
        live.setService(id, items);
        QCOMPARE(live.itemIndex(), 3);
        QVERIFY(live.textCleared());
        QVERIFY(live.next());
        QCOMPARE(live.itemIndex(), 4);
    }

    // #55: an imported sequence plays with forgiving labels and blank lyric-less steps, and
    // the journal (which keeps the live song) rebuilds exactly the same slides.
    void anImportedSequencePlaysForgivingly()
    {
        Song song;
        song.id = QUuid::createUuid();
        song.title = "Example";
        song.groups = {Group{QUuid::createUuid(), "Verse 1", "#0078cc", {"v1"}},
                       Group{QUuid::createUuid(), "PreChorus", "", {"pc"}},
                       Group{QUuid::createUuid(), "Chorus 1", "", {"c1", "c2"}}};
        ServiceItem item = songItem(song);
        item.customSequence = {"Intro", "Verse 1", "Pre Chorus 1", "Chorus", "Turnaround", "Bridge 2", "Chorus", "Ending"};
        const LiveItem live = liveItem(item, song, templateSet({}));
        QStringList texts;
        QStringList labels;
        QList<SongSlide> liveSlides;
        for (const LiveSlide &l : live.slides) {
            const SongSlide &s = l.slide;
            liveSlides << s;
            texts << s.text;
            labels << s.groupLabel;
        }
        QCOMPARE(texts, (QStringList{"", "v1", "pc", "c1", "c2", "", "c1", "c2", ""}));
        QCOMPARE(labels, (QStringList{"Intro", "Verse 1", "PreChorus", "Chorus 1", "Chorus 1", "Turnaround", "Chorus 1",
                                      "Chorus 1", "Ending"}));
        QCOMPARE(songSlides(live.song, live.arrangement), liveSlides);    // what recovery does
        QCOMPARE(liveItem(item, song, templateSet({})), live);                              // same ids every load
        QCOMPARE(live.song.groups.size(), 6);                              // three blanks added to the live copy
        QCOMPARE(song.groups.size(), 3);                                   // never to the library's song

        // A repeated lyric-less label reuses its one blank group.
        item.customSequence = {"Intro", "Verse 1", "Intro"};
        QCOMPARE(liveItem(item, song, templateSet({})).song.groups.size(), 4);

        // Only lyric-less labels: nothing names the song's own groups, so its arrangement stands.
        item.customSequence = {"Intro", "Ending"};
        QCOMPARE(liveItem(item, song, templateSet({})).song.groups.size(), 3);
    }

    void liveItemsForEachKind()
    {
        Song song = arranged();
        song.id = QUuid::createUuid();
        ServiceItem item = songItem(song);
        QCOMPARE(liveItem(item, song, templateSet({})).slides.size(), 7);
        QVERIFY(liveItem(item, std::nullopt, templateSet({})).slides.isEmpty());   // the song couldn't load

        song.arrangements << Arrangement{QUuid::createUuid(), "Short", {song.groups[3].id}, {}};
        item.arrangement = song.arrangements[1].id;
        QCOMPARE(liveItem(item, song, templateSet({})).slides.size(), 1);

        // Planning Center's order for the day, by label. A lyric-less label with no group
        // ("Outro") is a blank slide (#55); another label naming no group is left out.
        item.customSequence = {"chorus", "Verse 2", "Outro", "Vamp", "Chorus"};
        const LiveItem custom = liveItem(item, song, templateSet({}));
        QStringList labels;
        QList<SongSlide> customSlides;
        for (const LiveSlide &l : custom.slides) {
            labels << l.slide.groupLabel;
            customSlides << l.slide;
        }
        QCOMPARE(labels, (QStringList{"Chorus", "Verse 2", "Outro", "Chorus"}));
        QVERIFY(custom.slides[2].slide.isBlank());
        QCOMPARE(custom.arrangement, item.id);
        QCOMPARE(songSlides(custom.song, custom.arrangement), customSlides);   // the journal can rebuild it

        item.customSequence = {"Outro"};   // nothing matches: the arrangement stands
        QCOMPARE(liveItem(item, song, templateSet({})).slides.size(), 1);

        const LiveItem text = liveItem(textItem("Welcome", "Welcome!\nSit anywhere"), std::nullopt, templateSet({}));
        QCOMPARE(text.slides.size(), 1);
        QCOMPARE(text.slides[0].slide.text, QStringLiteral("Welcome!\nSit anywhere"));
        QCOMPARE(text.song.title, QStringLiteral("Welcome"));
        const LiveItem blank = liveItem(textItem("Blank", ""), std::nullopt, templateSet({}));
        QCOMPARE(blank.slides.size(), 1);
        QVERIFY(blank.slides[0].slide.isBlank());
        QVERIFY(liveItem(header("Worship"), std::nullopt, templateSet({})).slides.isEmpty());
    }

    // Outputs bind to properties by name (QML); check they're readable through the meta-object.
    void propertiesAreReadableByName()
    {
        LiveState live;
        live.show(libraryItem(sample()), 3);
        live.clearBackground();
        QCOMPARE(live.property("currentText").toString(), QStringLiteral("And heaven and nature sing"));
        QCOMPARE(live.property("groupLabel").toString(), QStringLiteral("Chorus"));
        QCOMPARE(live.property("slideIndex").toInt(), 3);
        QCOMPARE(live.property("backgroundCleared").toBool(), true);
    }

    // #98: the background layer. A slide changes only the layers it carries (plan 5.2).
    void theSameBackgroundAgainChangesNothing()
    {
        const QUuid still = QUuid::createUuid();
        LiveState live;
        live.setMediaCheck(FakeMedia{{still}}.fn());
        QVERIFY(live.setBackground(still, kFade));
        QSignalSpy changed(&live, &LiveState::changed);
        QVERIFY(live.setBackground(still, Transition{}));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(live.background().transition, kFade);   // still the one it arrived with
    }

    void aCarriedBackgroundBringsAClearedOneBack()
    {
        const QUuid a = QUuid::createUuid(), b = QUuid::createUuid();
        LiveState live;
        live.setMediaCheck(FakeMedia{{a, b}}.fn());
        live.setBackground(a, kFade);
        live.clearBackground();
        QVERIFY(live.setBackground(a, Transition{}));
        QVERIFY(!live.backgroundCleared());
        QCOMPARE(live.background().transition, kFade);

        live.clearBackground();
        QVERIFY(live.setBackground(b, Transition{}));
        QCOMPARE(live.background(), (BackgroundLayer{b, false, Transition{}}));
    }

    void aMissingBackgroundKeepsTheOldOneAndSaysSo()
    {
        const QUuid shown = QUuid::createUuid(), gone = QUuid::createUuid();
        LiveState live;
        live.setMediaCheck(FakeMedia{{shown}}.fn());
        live.show(libraryItem(sample()), 1);
        live.setBackground(shown, kFade);
        live.clearBackground();

        QVERIFY(!live.setBackground(gone, Transition{}));
        QCOMPARE(live.background(), (BackgroundLayer{shown, true, kFade}));
        QVERIFY2(live.backgroundAlert().contains("Sunrise.jpg"), qPrintable(live.backgroundAlert()));
        QVERIFY(live.next());   // the text still changes
        QCOMPARE(live.currentText(), QStringLiteral("Let earth receive her King"));

        live.showBackground();
        QVERIFY(live.setBackground(shown, Transition{}));
        QVERIFY(live.backgroundAlert().isEmpty());   // a background that shows ends the alert
    }

    void clearingAndShowingKeepTheTransition()
    {
        const QUuid still = QUuid::createUuid();
        LiveState live;
        live.setMediaCheck(FakeMedia{{still}}.fn());
        live.setBackground(still, kFade);
        live.clearBackground();
        QCOMPARE(live.background(), (BackgroundLayer{still, true, kFade}));
        live.showBackground();
        QCOMPARE(live.background(), (BackgroundLayer{still, false, kFade}));
        live.clearAll();
        QCOMPARE(live.background().transition, kFade);
    }

    void aStillSurvivesEverySlideChange()
    {
        const QUuid still = QUuid::createUuid();
        const QList<LiveItem> items = service();
        LiveState live;
        live.setMediaCheck(FakeMedia{{still}}.fn());
        live.setBackground(still, kFade);
        const BackgroundLayer layer = live.background();
        live.setService(QUuid::createUuid(), items);
        QVERIFY(live.next());
        while (live.next()) {   // through every slide and item
            QCOMPARE(live.background(), layer);
        }
        QVERIFY(live.previous());
        QVERIFY(live.goToItem(1, 2));
        QVERIFY(live.goToSlide(0));
        QVERIFY(live.goToGroupStartingWith("c"));
        live.clearText();
        Song song = arranged();
        song.id = QUuid::createUuid();
        QVERIFY(live.show(libraryItem(song), 3));
        QVERIFY(live.update(libraryItem(song)));
        live.setService(QUuid::createUuid(), items);
        QCOMPARE(live.background(), layer);
    }

    void noBackgroundIsAnEmptyLayer()
    {
        const QUuid still = QUuid::createUuid();
        FakeMedia media{{still}};
        LiveState live;
        live.setMediaCheck(media.fn());
        live.setBackground(still, kFade);
        const int checks = *media.calls;
        QVERIFY(live.setBackground({}, Transition{}));
        QCOMPARE(live.background().asset, QUuid());
        QCOMPARE(live.property("backgroundId").toString(), QString());
        QCOMPARE(*media.calls, checks);   // nothing to check
    }

    // The media library's picker: the live item's template says how the background changes.
    void pickingABackgroundUsesTheLiveTemplatesTransition()
    {
        const QUuid still = QUuid::createUuid();
        Template t = builtInLyricsTemplate();
        t.id = QUuid::createUuid();
        t.transitions.background = kFade;
        Song song = sample();
        song.defaultTemplate = t.id;
        LiveState live;
        live.setMediaCheck(FakeMedia{{still}}.fn());
        QVERIFY(live.pickBackground(still.toString(QUuid::WithoutBraces)));
        QCOMPARE(live.background().transition, Transitions{}.background);   // nothing live: the default
        live.show(liveSong(song, {}, templateSet({t})), 0);
        QVERIFY(live.pickBackground(""));
        QVERIFY(live.pickBackground(still.toString(QUuid::WithoutBraces)));
        QCOMPARE(live.background(), (BackgroundLayer{still, false, kFade}));
        QCOMPARE(live.property("backgroundId").toString(), still.toString(QUuid::WithoutBraces));
    }

    void restoringAMissingBackgroundLeavesNoneAndSaysSo()
    {
        const QUuid gone = QUuid::createUuid();
        LiveState live;
        live.setMediaCheck(FakeMedia{}.fn());
        QSignalSpy changed(&live, &LiveState::changed);
        QVERIFY(live.restore(libraryItem(sample()), 1, false, BackgroundLayer{gone, true, kFade}));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(live.slideIndex(), 1);   // the text comes back regardless
        QCOMPARE(live.background().asset, QUuid());
        QVERIFY(!live.backgroundAlert().isEmpty());
    }

    void aBackgroundThePlayerCantShowGoesBackToWhatItShows()
    {
        const QUuid a = QUuid::createUuid(), b = QUuid::createUuid(), c = QUuid::createUuid();
        LiveState live;
        live.setMediaCheck(FakeMedia{{a, b, c}}.fn());
        live.setBackground(a, kFade);
        live.setBackground(b, kFade);
        live.backgroundFailed(b, a, kFade, "it won't decode");
        QCOMPARE(live.background().asset, a);
        QCOMPARE(live.backgroundAlert(), QStringLiteral("it won't decode"));
        QVERIFY(live.setBackground(b, kFade));   // asked for again: tried again
        QCOMPARE(live.background().asset, b);

        live.setBackground(c, kFade);
        QSignalSpy changed(&live, &LiveState::changed);
        live.backgroundFailed(b, a, kFade, "too late");   // the layer has moved on
        QCOMPARE(changed.count(), 0);
        QCOMPARE(live.background().asset, c);
    }

    // Without a library there's nothing to check against: every asset is taken as given.
    void withoutAMediaCheckEveryAssetIsAccepted()
    {
        LiveState live;
        const QUuid id = QUuid::createUuid();
        QVERIFY(live.setBackground(id, kFade));
        QCOMPARE(live.background().asset, id);
        QVERIFY(live.checkMedia(id).ok());
    }

    // #83: a song too big to expand leaves what's live alone and says why.
    void anOversizedSongIsRefusedAndLiveStaysPut()
    {
        LiveState live;
        QVERIFY(live.show(libraryItem(sample()), 1));
        QSignalSpy changed(&live, &LiveState::changed);
        QSignalSpy refused(&live, &LiveState::refused);

        QVERIFY(!live.show(libraryItem(repeated(500, 1000))));
        QCOMPARE(refused.count(), 1);
        QVERIFY(refused.at(0).at(0).toString().contains("too many slides to play"));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(live.songTitle(), sample().title);
        QCOMPARE(live.slideIndex(), 1);
        QCOMPARE(live.slideCount(), 5);
    }

    void anOversizedRestoreIsRefusedAndLiveStaysPut()
    {
        LiveState live;
        QVERIFY(live.show(libraryItem(sample()), 2));
        QSignalSpy changed(&live, &LiveState::changed);
        QSignalSpy refused(&live, &LiveState::refused);
        QVERIFY(!live.restore(libraryItem(repeated(10'000, 10'000)), 0, true, BackgroundLayer{.cleared = true}, QUuid::createUuid()));
        QCOMPARE(refused.count(), 1);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(live.songTitle(), sample().title);
        QCOMPARE(live.slideIndex(), 2);
        QVERIFY(!live.textCleared() && !live.backgroundCleared());
        QVERIFY(live.serviceId().isNull());
    }

    void anOversizedServiceItemHasNoSlidesAndSaysWhy()
    {
        ServiceItem item;
        item.id = QUuid::createUuid();
        item.kind = ServiceItem::Kind::Song;
        const LiveItem l = liveItem(item, repeated(500, 1000), templateSet({}));
        QVERIFY(l.slides.isEmpty());
        QVERIFY(l.error.contains("too many slides to play"));
    }

    void anOversizedCustomSequenceIsRefusedBeforeMatching()
    {
        ServiceItem item;
        item.id = QUuid::createUuid();
        item.kind = ServiceItem::Kind::Song;
        item.customSequence = QStringList(100'000, "Verse");
        const LiveItem l = liveItem(item, repeated(1, 1), templateSet({}));
        QVERIFY(l.slides.isEmpty());
        QCOMPARE(l.error, QStringLiteral("too many steps in the sequence: 100,000; the limit is 1,000"));
    }
};

QTEST_GUILESS_MAIN(LiveStateTest)
#include "tst_livestate.moc"
