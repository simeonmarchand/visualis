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
        QCOMPARE(overrides::setTemplate(o, QUuid::createUuid()), QString());
        overrides::resetTemplate(o);
        QCOMPARE(o, parse("{'later': 1, 'regions': {'main': {'glow': true, 'shadow': {'spread': 2}}, 'other': {'x': 1}}}"));
        overrides::resetAll(o);
        QCOMPARE(o, parse("{'later': 1}"));
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
        // Unknown keys stored alongside aren't properties.
        o = parse("{'regions': {'main': {'glow': true, 'shadow': {'spread': 2, 'blur': 0.1}}}}");
        QCOMPARE(overrides::overriddenProperties(o, "main"), QStringList{"shadow.blur"});
    }

    void templateIsSetAndResetOnItsOwn()
    {
        QJsonObject o = parse("{'regions': {'main': {'color': '#ffcc00'}}}");
        const QUuid id = QUuid::createUuid();
        QCOMPARE(overrides::setTemplate(o, id), QString());
        QCOMPARE(overrideTemplateId(o), id);
        QVERIFY(!overrides::setTemplate(o, QUuid()).isEmpty());   // reset instead
        overrides::resetTemplate(o);
        QCOMPARE(o, parse("{'regions': {'main': {'color': '#ffcc00'}}}"));
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
            return liveSong(song, {}, templateSet({t})).slides[slide].content.regions["main"].style;
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
