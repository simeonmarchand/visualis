#include "live/liveitem.h"
#include "template/overrides.h"

#include <QJsonDocument>
#include <QtTest>

using namespace visualis;

namespace {

// JSON written with single quotes, so it reads in a C++ string.
QJsonObject parse(const char *json)
{
    return QJsonDocument::fromJson(QByteArray(json).replace('\'', '"')).object();
}

Song oneSlideSong(const QUuid &tmpl)
{
    Song s;
    s.id = QUuid::createUuid();
    s.title = "Song";
    s.groups = {Group{QUuid::createUuid(), "Verse 1", {}, {"one", "two"}}};
    s.defaultTemplate = tmpl;
    return s;
}

} // namespace

class OverridesTest : public QObject
{
    Q_OBJECT

private slots:
    void settingThenResettingGivesBackTheOriginal()
    {
        const QJsonObject original = parse("{'regions': {'main': {'color': '#ffcc00'}}}");
        QJsonObject o = original;
        QCOMPARE(overrides::setProperty(o, "main", "maxSize", 0.07), QString());
        QCOMPARE(overrides::setProperty(o, "main", "frame.y", 0.6), QString());
        QCOMPARE(o, parse("{'regions': {'main': {'color': '#ffcc00', 'maxSize': 0.07, 'frame': {'y': 0.6}}}}"));
        overrides::resetProperty(o, "main", "maxSize");
        overrides::resetProperty(o, "main", "frame.y");
        QCOMPARE(o, original);

        QJsonObject empty;
        QCOMPARE(overrides::setProperty(empty, "main", "weight", 700), QString());
        overrides::resetProperty(empty, "main", "weight");
        QCOMPARE(empty, QJsonObject());   // no empty "regions" left behind
    }

    void resettingANestedPropertyKeepsItsSiblingsAndPrunesWhenEmpty()
    {
        QJsonObject o = parse("{'regions': {'main': {'shadow': {'opacity': 0.5, 'color': '#102030'}}}}");
        overrides::resetProperty(o, "main", "shadow.opacity");
        QCOMPARE(o, parse("{'regions': {'main': {'shadow': {'color': '#102030'}}}}"));
        overrides::resetProperty(o, "main", "shadow.color");
        QCOMPARE(o, QJsonObject());
    }

    void anInvalidValueIsRefusedAndNothingChanges()
    {
        const QJsonObject original = parse("{'regions': {'main': {'color': '#ffcc00'}}}");
        QJsonObject o = original;
        QVERIFY(!overrides::setProperty(o, "main", "color", "yellow").isEmpty());
        QVERIFY(!overrides::setProperty(o, "main", "shadow.opacity", 2).isEmpty());
        QVERIFY(!overrides::setProperty(o, "main", "weight", 2.5).isEmpty());
        QVERIFY(!overrides::setProperty(o, "main", "fontSize", 12).isEmpty());       // not a property
        QVERIFY(!overrides::setProperty(o, "main", "shadow", QJsonObject{}).isEmpty());   // not a single property
        QVERIFY(!overrides::setProperty(o, "", "color", "#000000").isEmpty());       // no region
        QCOMPARE(o, original);
        const QString why = overrides::setProperty(o, "main", "maxSize", -1);
        QVERIFY2(why.contains("maxSize"), qPrintable(why));
    }

    void unknownKeysSurviveEveryEdit()
    {
        QJsonObject o = parse("{'later': 1, 'regions': {'main': {'glow': true, 'shadow': {'spread': 2}}, 'other': {'x': 1}}}");
        QCOMPARE(overrides::setProperty(o, "main", "shadow.opacity", 0.5), QString());
        overrides::resetProperty(o, "main", "shadow.opacity");
        overrides::setTemplate(o, QUuid::createUuid());
        overrides::setTemplate(o, QUuid());
        QCOMPARE(o, parse("{'later': 1, 'regions': {'main': {'glow': true, 'shadow': {'spread': 2}}, 'other': {'x': 1}}}"));
        overrides::setTransition(o, parse("{'kind': 'fade', 'ms': 400}"));
        overrides::resetAll(o);
        QCOMPARE(o, parse("{'later': 1}"));

        QJsonObject t = parse("{'transition': {'kind': 'cut', 'ms': 0, 'easing': 'outCubic'}}");
        QCOMPARE(overrides::setTransition(t, parse("{'kind': 'fade', 'ms': 500}")), QString());
        QCOMPARE(t, parse("{'transition': {'kind': 'fade', 'ms': 500, 'easing': 'outCubic'}}"));
    }

    void overriddenPropertiesListsExactlyWhatWasSet()
    {
        QJsonObject o;
        QVERIFY(overrides::overriddenProperties(o, "main").isEmpty());
        overrides::setProperty(o, "main", "color", "#ff0000");
        overrides::setProperty(o, "main", "shadow.opacity", 0.2);
        overrides::setProperty(o, "main", "frame.x", 0.1);
        overrides::setProperty(o, "other", "weight", 300);
        QCOMPARE(overrides::overriddenProperties(o, "main"), (QStringList{"color", "frame.x", "shadow.opacity"}));
        QCOMPARE(overrides::overriddenProperties(o, "other"), QStringList{"weight"});
        QCOMPARE(overrides::propertyCount(o), 4);   // in every region
        overrides::setTransition(o, parse("{'kind': 'cut'}"));
        QCOMPARE(overrides::propertyCount(o), 5);   // and the transition
        // Unknown keys stored alongside aren't properties.
        o = parse("{'regions': {'main': {'glow': true, 'shadow': {'spread': 2, 'blur': 0.1}}}}");
        QCOMPARE(overrides::overriddenProperties(o, "main"), QStringList{"shadow.blur"});
    }

    void templateIsSetAndResetOnItsOwn()
    {
        QJsonObject o = parse("{'regions': {'main': {'color': '#ffcc00'}}}");
        const QUuid id = QUuid::createUuid();
        overrides::setTemplate(o, id);
        QCOMPARE(overrideTemplateId(o), id);
        overrides::setTemplate(o, QUuid());   // none: back to the default
        QCOMPARE(o, parse("{'regions': {'main': {'color': '#ffcc00'}}}"));
    }

    // #99: an item's background, by media asset id. Not style, so resetting the style keeps it.
    void aBackgroundIsSetAndResetOnItsOwn()
    {
        const QJsonObject original = parse("{'regions': {'main': {'color': '#ffcc00'}}, 'glow': true}");
        QJsonObject o = original;
        QVERIFY(overrideBackgroundId(o).isNull());
        const QUuid asset = QUuid::createUuid();
        overrides::setBackground(o, asset);
        QCOMPARE(overrideBackgroundId(o), asset);
        overrides::resetAll(o);
        QCOMPARE(overrideBackgroundId(o), asset);
        QCOMPARE(o, (QJsonObject{{"glow", true}, {"background", asset.toString(QUuid::WithoutBraces)}}));
        overrides::setBackground(o, QUuid());
        QCOMPARE(o, parse("{'glow': true}"));
    }

    void aTransitionIsSetAndResetOnItsOwn()
    {
        const QJsonObject original = parse("{'regions': {'main': {'color': '#ffcc00'}}}");
        QJsonObject o = original;
        QVERIFY(!overrides::overridesTransition(o));
        QCOMPARE(overrides::setTransition(o, parse("{'kind': 'fade', 'ms': 450}")), QString());
        QVERIFY(overrides::overridesTransition(o));
        QCOMPARE(o, parse("{'regions': {'main': {'color': '#ffcc00'}}, 'transition': {'kind': 'fade', 'ms': 450}}"));
        overrides::resetTransition(o);
        QCOMPARE(o, original);
    }

    void anInvalidTransitionIsRefused()
    {
        QJsonObject o;
        QVERIFY(!overrides::setTransition(o, parse("{'kind': 'fade', 'ms': -1}")).isEmpty());
        QVERIFY(!overrides::setTransition(o, parse("{'kind': 'wipe'}")).isEmpty());   // not a kind this version has
        const QString why = overrides::setTransition(o, parse("{'ms': 60000}"));
        QVERIFY2(why.contains("ms"), qPrintable(why));
        QCOMPARE(o, QJsonObject());
    }

    // #96: a slide's text transition is the template's unless the slide overrides it.
    void theSlidesTransitionOverridesTheTemplates()
    {
        Template t = builtInLyricsTemplate();
        t.id = QUuid::createUuid();
        t.name = "Ours";
        t.transitions.content = {TransitionKind::Fade, 400};
        Song song = oneSlideSong(t.id);
        song.groups[0].fitSlideOverrides();
        QCOMPARE(overrides::setTransition(song.groups[0].slideOverrides[1], parse("{'kind': 'cut', 'ms': 300}")), QString());

        auto transition = [&](int slide) { return liveSong(song, {}, templateSet({t})).slides[slide].content->transition; };
        QCOMPARE(transition(0), (Transition{TransitionKind::Fade, 400}));
        QCOMPARE(transition(1), (Transition{TransitionKind::Cut, 300}));

        t.transitions.content = {TransitionKind::Fade, 800};
        QCOMPARE(transition(0), (Transition{TransitionKind::Fade, 800}));   // follows
        QCOMPARE(transition(1), (Transition{TransitionKind::Cut, 300}));    // sticks

        overrides::resetTransition(song.groups[0].slideOverrides[1]);
        QCOMPARE(transition(1), (Transition{TransitionKind::Fade, 800}));
    }

    void aBadTransitionOverrideIsReportedAndTheRestApplies()
    {
        const Transition fade{TransitionKind::Fade, 400};
        QStringList warnings;
        // A length alone keeps the kind; a bad length keeps the one below it.
        QCOMPARE(effectiveTransition(fade, {parse("{'transition': {'ms': 250}}")}, &warnings), (Transition{TransitionKind::Fade, 250}));
        QVERIFY(warnings.isEmpty());
        QCOMPARE(effectiveTransition(fade, {parse("{'transition': {'kind': 'cut', 'ms': -5}}")}, &warnings),
                 (Transition{TransitionKind::Cut, 400}));
        QCOMPARE(warnings.size(), 1);
        // A kind this version doesn't know cuts, as in a template.
        QCOMPARE(effectiveTransition(fade, {parse("{'transition': {'kind': 'dissolve'}}")}, &warnings).kind, TransitionKind::Cut);
        QCOMPARE(warnings.size(), 2);
        QCOMPARE(effectiveTransition(fade, {parse("{'transition': 3}")}, &warnings), fade);
        QCOMPARE(warnings.size(), 3);
    }

    // The renderer reads what the module writes; everything not overridden follows the template.
    void anOverrideShowsAndTheRestFollowsTheTemplate()
    {
        Template t = builtInLyricsTemplate();
        t.id = QUuid::createUuid();
        t.name = "Ours";
        Song song = oneSlideSong(t.id);
        song.groups[0].fitSlideOverrides();
        QCOMPARE(overrides::setProperty(song.groups[0].slideOverrides[1], "main", "color", "#ffcc00"), QString());

        auto style = [&](int slide) {
            return liveSong(song, {}, templateSet({t})).slides[slide].content->regions["main"].style;
        };
        QCOMPARE(style(1).color, QStringLiteral("#ffcc00"));
        QCOMPARE(style(0).color, QStringLiteral("#ffffff"));

        t.regions[0].style.color = "#00ff00";
        t.regions[0].style.fontFamily = "Georgia";
        QCOMPARE(style(1).color, QStringLiteral("#ffcc00"));       // sticks
        QCOMPARE(style(1).fontFamily, QStringLiteral("Georgia"));  // follows
        QCOMPARE(style(0).color, QStringLiteral("#00ff00"));

        overrides::resetProperty(song.groups[0].slideOverrides[1], "main", "color");
        QCOMPARE(style(1).color, QStringLiteral("#00ff00"));       // reset: the template's again
    }
};

QTEST_MAIN(OverridesTest)
#include "tst_overrides.moc"
