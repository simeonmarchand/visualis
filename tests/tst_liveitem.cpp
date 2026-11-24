#include "live/liveitem.h"
#include "template/template.h"

#include <QtTest>

using namespace visualis;

namespace {

Song sample()
{
    Song s;
    s.id = QUuid::createUuid();
    s.title = "Joy";
    s.groups = {Group{QUuid::createUuid(), "Verse 1", "#0078cc", {"Joy to the world", "Let earth receive"}},
                Group{QUuid::createUuid(), "Chorus", "#cc004f", {"And heaven and nature sing"}}};
    return s;
}

// A stored template whose main region is set apart by its font.
Template named(const QString &font)
{
    Template t = builtInLyricsTemplate();
    t.id = QUuid::createUuid();
    t.name = font;
    t.regions[0].style.fontFamily = font;
    return t;
}

ServiceItem songItem(const Song &song, const QJsonObject &overrides = {})
{
    ServiceItem item;
    item.id = QUuid::createUuid();
    item.kind = ServiceItem::Kind::Song;
    item.ref = song.id;
    item.title = song.title;
    item.overrides = overrides;
    return item;
}

QJsonObject pick(const Template &t) { return {{"template", t.id.toString(QUuid::WithoutBraces)}}; }

QJsonObject mainStyle(const QJsonObject &style) { return {{"regions", QJsonObject{{"main", style}}}}; }

// A service where every song uses `t`: item 0's second slide overrides the font, item 1
// overrides it for the whole item, item 2 doesn't.
QList<LiveItem> service(const Template &t, const QList<Song> &songs, const QList<ServiceItem> &items)
{
    QList<LiveItem> out;
    for (int i = 0; i < items.size(); ++i)
        out << liveItem(items[i], songs[i], templateSet({t}));
    return out;
}

QString fontOf(const LiveItem &live) { return live.slides.value(0).content.regions.value("main").style.fontFamily; }

} // namespace

class LiveItemTest : public QObject
{
    Q_OBJECT

private slots:
    void aLibrarySongRendersThroughTheBuiltInTemplate()
    {
        const LiveItem live = liveSong(sample(), {}, templateSet({}));
        QCOMPARE(live.slides.size(), 3);
        QCOMPARE(live.templateId(), builtInLyricsTemplateId());
        QVERIFY(!live.templateFellBack);
        const Template builtIn = builtInLyricsTemplate();
        for (const LiveSlide &s : live.slides) {
            QCOMPARE(s.content.regions.keys(), QList<QString>{"main"});
            QCOMPARE(s.content.regions["main"].text, s.slide.text);
            QCOMPARE(s.content.regions["main"].style, builtIn.regions[0].style);
            QCOMPARE(s.content.transition, builtIn.transitions.content);
        }
        QCOMPARE(live.slides[2].slide.text, QStringLiteral("And heaven and nature sing"));
    }

    void theTemplateIsTheItemsElseTheSongsElseTheBuiltIn()
    {
        const Template avenir = named("Avenir"), georgia = named("Georgia");
        const TemplateSet set = templateSet({avenir, georgia});
        Song song = sample();
        QCOMPARE(fontOf(liveItem(songItem(song), song, set)), builtInLyricsTemplate().regions[0].style.fontFamily);

        song.defaultTemplate = avenir.id;
        LiveItem live = liveItem(songItem(song), song, set);
        QCOMPARE(live.templateId(), avenir.id);
        QCOMPARE(fontOf(live), QStringLiteral("Avenir"));
        QCOMPARE(fontOf(liveSong(song, {}, set)), QStringLiteral("Avenir"));   // from the library too

        live = liveItem(songItem(song, pick(georgia)), song, set);
        QCOMPARE(live.templateId(), georgia.id);
        QCOMPARE(fontOf(live), QStringLiteral("Georgia"));
        QVERIFY(!live.templateFellBack);
    }

    void aTextItemUsesItsOverrideElseTheBuiltIn()
    {
        const Template georgia = named("Georgia");
        const TemplateSet set = templateSet({georgia});
        ServiceItem text;
        text.id = QUuid::createUuid();
        text.kind = ServiceItem::Kind::Text;
        text.title = "Welcome";
        text.body = "Welcome!";
        LiveItem live = liveItem(text, std::nullopt, set);
        QCOMPARE(live.templateId(), builtInLyricsTemplateId());
        QCOMPARE(live.slides[0].content.regions["main"].text, QStringLiteral("Welcome!"));
        text.overrides = pick(georgia);
        live = liveItem(text, std::nullopt, set);
        QCOMPARE(live.templateId(), georgia.id);
        QCOMPARE(fontOf(live), QStringLiteral("Georgia"));
    }

    // Phase 3 acceptance (#93): a template change shows on every slide that doesn't override it.
    void changingTheTemplatesFontChangesEverySlideButOverriddenOnes()
    {
        Template t = named("Avenir");
        QList<Song> songs{sample(), sample(), sample()};
        for (Song &s : songs)
            s.defaultTemplate = t.id;
        songs[0].groups[0].slideOverrides = {{}, mainStyle({{"fontFamily", "Courier"}})};
        const QList<ServiceItem> items{songItem(songs[0]), songItem(songs[1], mainStyle({{"fontFamily", "Menlo"}})),
                                       songItem(songs[2])};
        auto fonts = [](const QList<LiveItem> &service) {
            QStringList out;
            for (const LiveItem &item : service)
                for (const LiveSlide &s : item.slides)
                    out << s.content.regions["main"].style.fontFamily;
            return out;
        };
        QCOMPARE(fonts(service(t, songs, items)),
                 (QStringList{"Avenir", "Courier", "Avenir", "Menlo", "Menlo", "Menlo", "Avenir", "Avenir", "Avenir"}));

        t.regions[0].style.fontFamily = "Georgia";
        t.regions[0].style.color = "#ffcc00";
        const QList<LiveItem> after = service(t, songs, items);
        QCOMPARE(fonts(after),
                 (QStringList{"Georgia", "Courier", "Georgia", "Menlo", "Menlo", "Menlo", "Georgia", "Georgia", "Georgia"}));
        for (const LiveItem &item : after)   // what nothing overrides follows the template everywhere
            for (const LiveSlide &s : item.slides)
                QCOMPARE(s.content.regions["main"].style.color, QStringLiteral("#ffcc00"));
    }

    // Phase 3 acceptance (#93): moving the lyrics region moves it on every slide.
    void movingTheLyricsRegionMovesItOnEverySlide()
    {
        Template t = named("Avenir");
        Song song = sample();
        song.defaultTemplate = t.id;
        song.groups[1].slideOverrides = {mainStyle({{"color", "#ff0000"}})};   // overrides something else
        const ServiceItem item = songItem(song, mainStyle({{"weight", 800}}));
        const QRectF lowerThird(0.05, 0.7, 0.9, 0.25);
        t.regions[0].style.frame = lowerThird;
        const LiveItem live = liveItem(item, song, templateSet({t}));
        QCOMPARE(live.slides.size(), 3);
        for (const LiveSlide &s : live.slides)
            QCOMPARE(s.content.regions["main"].style.frame, lowerThird);
        QCOMPARE(liveSong(song, {}, templateSet({t})).slides[1].content.regions["main"].style.frame, lowerThird);
    }

    void aMissingTemplateFallsBackToTheBuiltInWithOneWarning()
    {
        Song song = sample();
        song.defaultTemplate = QUuid::createUuid();   // deleted, or never loaded
        const LiveItem live = liveItem(songItem(song), song, templateSet({}));
        QCOMPARE(live.slides.size(), 3);   // never a blank output
        QCOMPARE(live.templateId(), builtInLyricsTemplateId());
        QVERIFY(live.templateFellBack);
        QCOMPARE(fontOf(live), builtInLyricsTemplate().regions[0].style.fontFamily);
        QCOMPARE(live.warnings.size(), 1);   // one for the item, not one per slide
        QVERIFY(live.warnings[0].contains("built-in"));

        const LiveItem fine = liveItem(songItem(sample()), sample(), templateSet({}));
        QVERIFY(!fine.templateFellBack);
        QVERIFY(fine.warnings.isEmpty());
    }

    void badOverrideValuesAreReportedOncePerItem()
    {
        Song song = sample();
        song.groups[0].slideOverrides = {mainStyle({{"weight", 2.5}}), mainStyle({{"weight", 2.5}})};
        const LiveItem live = liveItem(songItem(song, mainStyle({{"color", "gold"}})), song, templateSet({}));
        QCOMPARE(live.warnings.size(), 2);   // the item's bad color on all 3 slides, the slides' weight on 2
        QCOMPARE(live.slides[0].content.regions["main"].style.color, builtInLyricsTemplate().regions[0].style.color);
    }

    // Recovery re-renders from the journal, which keeps the live item's own song.
    void anItemKeepsItsSourceAndRendersTheSameFromItsOwnSong()
    {
        const Song song = sample();
        ServiceItem item = songItem(song, mainStyle({{"weight", 800}}));
        item.customSequence = {"Intro", "Verse 1", "Chorus", "Ending"};   // adds blank groups and an arrangement
        const LiveItem live = liveItem(item, song, templateSet({}));
        QCOMPARE(live.source, std::optional(item));
        QCOMPARE(live.slides.size(), 5);
        QCOMPARE(liveItem(item, live.song, templateSet({})), live);
    }
};

QTEST_GUILESS_MAIN(LiveItemTest)
#include "tst_liveitem.moc"
