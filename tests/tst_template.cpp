#include "library/library.h"
#include "song/songjson.h"
#include "song/songstore.h"
#include "song/songtext.h"
#include "template/template.h"
#include "template/templatejson.h"
#include "template/templatestore.h"

#include <QJsonArray>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace visualis;

namespace {

QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

QJsonObject object(const QByteArray &bytes) { return QJsonDocument::fromJson(bytes).object(); }

// A template with every property away from its default, so a round trip shows each one.
Template sample()
{
    Template t;
    t.name = QStringLiteral("Lower third");
    t.kind = TemplateKind::LowerThird;
    t.safeAreaX = 0.05;
    t.safeAreaY = 0.1;
    t.transitions.content = {TransitionKind::Fade, 450};
    t.transitions.background = {TransitionKind::Cut, 0};
    RegionStyle a;
    a.frame = QRectF(0.1, 0.7, 0.8, 0.2);
    a.fontFamily = QStringLiteral("Avenir Next");
    a.maxSize = 0.06;
    a.minSize = 0.02;
    a.weight = 700;
    a.color = QStringLiteral("#80ffcc00");
    a.hAlign = HAlign::Left;
    a.vAlign = VAlign::Bottom;
    a.lineHeight = 1.25;
    a.shadow = {true, QStringLiteral("#102030"), 0.4, 0.2, -0.01};
    a.outline = {QStringLiteral("#ffffff"), 0.002};
    RegionStyle b;
    b.frame = QRectF(0.1, 0.9, 0.5, 0.05);
    b.hAlign = HAlign::Right;
    b.vAlign = VAlign::Top;
    t.regions << TextRegion{"title", "Title", a, {}} << TextRegion{"subtitle", "Subtitle", b, {}};
    return t;
}

// Everything but what's kept for unknown keys.
void compareTemplates(Template a, Template b)
{
    a.extra = b.extra = {};
    for (TextRegion &r : a.regions) r.extra = {};
    for (TextRegion &r : b.regions) r.extra = {};
    QCOMPARE(a, b);
}

QByteArray minimal(const QJsonObject &more = {})
{
    QJsonObject o{{"format", "visualis-template"}, {"version", 1}, {"name", "T"}};
    for (auto it = more.begin(); it != more.end(); ++it)
        o[it.key()] = *it;
    return json(o);
}

} // namespace

class TemplateTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dir.emplace();
        auto r = Library::open(m_dir->path());
        QVERIFY2(r.library, qPrintable(r.message));
        m_library = std::move(r.library);
    }
    void cleanup()
    {
        m_library.reset();
        m_dir.reset();
    }

    void jsonRoundTrips()
    {
        Template t = sample();
        t.id = QUuid::createUuid();
        t.revision = 7;
        t.created = QDateTime(QDate(2026, 11, 1), QTime(9, 30), QTimeZone::UTC);
        t.updated = QDateTime(QDate(2026, 11, 2), QTime(10, 0), QTimeZone::UTC);
        const TemplateJsonResult r = templateFromJson(templateToJson(t));
        QVERIFY2(r.tmpl, qPrintable(r.error));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join('\n')));
        compareTemplates(*r.tmpl, t);
        QCOMPARE(templateToJson(*r.tmpl), templateToJson(t));

        const TemplateJsonResult builtIn = templateFromJson(templateToJson(builtInLyricsTemplate()));
        QVERIFY(builtIn.tmpl && builtIn.warnings.isEmpty());
        compareTemplates(*builtIn.tmpl, builtInLyricsTemplate());
    }

    void unknownKeysAreKept()
    {
        QJsonObject o = object(templateToJson(sample()));
        o["futureKey"] = "kept";
        QJsonObject transitions = o["transitions"].toObject();
        QJsonObject content = transitions["content"].toObject();
        content["easing"] = "outCubic";
        transitions["content"] = content;
        o["transitions"] = transitions;
        QJsonArray regions = o["regions"].toArray();
        QJsonObject region = regions[0].toObject();
        region["glow"] = QJsonObject{{"radius", 3}};
        QJsonObject shadow = region["shadow"].toObject();
        shadow["spread"] = 2;
        region["shadow"] = shadow;
        regions[0] = region;
        o["regions"] = regions;

        TemplateJsonResult r = templateFromJson(json(o));
        QVERIFY2(r.tmpl && r.warnings.isEmpty(), qPrintable(r.error + r.warnings.join('\n')));
        r.tmpl->regions[0].style.color = "#000000";   // an edit, then a save
        const QJsonObject out = object(templateToJson(*r.tmpl));
        QCOMPARE(out["futureKey"].toString(), QStringLiteral("kept"));
        QCOMPARE(out["transitions"]["content"]["easing"].toString(), QStringLiteral("outCubic"));
        const QJsonObject outRegion = out["regions"][0].toObject();
        QCOMPARE(outRegion["glow"]["radius"].toInt(), 3);
        QCOMPARE(outRegion["shadow"]["spread"].toInt(), 2);
        QCOMPARE(outRegion["shadow"]["opacity"].toDouble(), 0.4);
        QCOMPARE(outRegion["color"].toString(), QStringLiteral("#000000"));   // the edit wins
    }

    void badValuesFallBackAndAreReported()
    {
        const QJsonObject bad{
            {"id", "main"},
            {"color", "red"},                                     // not hex
            {"maxSize", -1},
            {"weight", 1.5},
            {"align", "justify"},
            {"lineHeight", "tall"},
            {"frame", QJsonObject{{"x", 0.5}, {"w", 0.9}}},       // off the output
            {"shadow", "yes"},
            {"outline", QJsonObject{{"width", 4}}},
        };
        const TemplateJsonResult r = templateFromJson(minimal({
            {"kind", "banner"},
            {"safeArea", QJsonObject{{"x", 2}}},
            {"transitions", QJsonObject{{"content", QJsonObject{{"kind", "dissolve"}, {"ms", 500}}},
                                        {"background", QJsonObject{{"ms", -3}}}}},
            {"regions", QJsonArray{bad, QJsonObject{{"name", "no id"}}, QJsonObject{{"id", "main"}}, 7}},
        }));
        QVERIFY2(r.tmpl, qPrintable(r.error));
        const Template &t = *r.tmpl;
        const RegionStyle defaults;
        QCOMPARE(t.kind, TemplateKind::Lyrics);
        QCOMPARE(t.safeAreaX, Template().safeAreaX);
        QCOMPARE(t.transitions.content.kind, TransitionKind::Cut);   // unknown kinds cut
        QCOMPARE(t.transitions.content.ms, 500);
        QCOMPARE(t.transitions.background.ms, Transitions().background.ms);
        QCOMPARE(t.regions.size(), 1);   // the one without an id, the duplicate, and the 7 are skipped
        QCOMPARE(t.regions[0].style, defaults);
        // One warning each: 8 region values (the frame counts once), kind, safe area, two transitions, 3 skipped.
        QCOMPARE(r.warnings.size(), 8 + 1 + 1 + 2 + 3);
        QVERIFY(r.warnings.join('\n').contains("region \"main\".color is \"red\""));
        QVERIFY(r.warnings.join('\n').contains("doesn't know; using a cut"));
    }

    void minSizeNeverExceedsMaxSize()
    {
        const TemplateJsonResult r = templateFromJson(minimal({
            {"regions", QJsonArray{QJsonObject{{"id", "main"}, {"maxSize", 0.05}, {"minSize", 0.08}}}}}));
        QVERIFY(r.tmpl);
        QCOMPARE(r.tmpl->regions[0].style.maxSize, 0.05);
        QCOMPARE(r.tmpl->regions[0].style.minSize, 0.05);
        QCOMPARE(r.warnings.size(), 1);
    }

    void malformedInputIsRefused_data()
    {
        QTest::addColumn<QByteArray>("input");
        QTest::newRow("not JSON") << QByteArray("{\"format\": ");
        QTest::newRow("a list") << QByteArray("[]");
        QTest::newRow("a song") << json({{"format", "visualis-song"}, {"version", 1}});
        QTest::newRow("no version") << json({{"format", "visualis-template"}});
        QTest::newRow("newer") << json({{"format", "visualis-template"}, {"version", 99}});
        QByteArray huge = minimal();
        huge.insert(1, QByteArray(templatelimits::kBytes, ' '));   // valid JSON, but too large
        QTest::newRow("oversized") << huge;
    }
    void malformedInputIsRefused()
    {
        QFETCH(QByteArray, input);
        const TemplateJsonResult r = templateFromJson(input);
        QVERIFY(!r.tmpl);
        QVERIFY(!r.error.isEmpty());
    }

    void tooManyRegionsKeepsTheFirst()
    {
        QJsonArray regions;
        for (int i = 0; i < templatelimits::kRegions + 5; ++i)
            regions.append(QJsonObject{{"id", QStringLiteral("r%1").arg(i)}});
        const TemplateJsonResult r = templateFromJson(minimal({{"regions", regions}}));
        QVERIFY(r.tmpl);
        QCOMPARE(r.tmpl->regions.size(), templatelimits::kRegions);
        QCOMPARE(r.tmpl->regions.last().id, QStringLiteral("r%1").arg(templatelimits::kRegions - 1));
        QCOMPARE(r.warnings.size(), 1);
    }

    void storeSavesLoadsListsAndRemoves()
    {
        TemplateStore store(*m_library);
        Template t = sample();
        QVERIFY(store.save(t).ok);
        QVERIFY(!t.id.isNull());
        QCOMPARE(t.revision, 1);
        QVERIFY(t.created.isValid());
        QStringList warnings;
        const std::optional<Template> loaded = store.load(t.id, &warnings);
        QVERIFY(loaded);
        QVERIFY(warnings.isEmpty());
        compareTemplates(*loaded, t);

        Template again = *loaded;
        again.name = "Another";
        QVERIFY(store.save(again).ok);
        QCOMPARE(again.revision, 2);
        QCOMPARE(again.created, t.created);

        Template b = sample();
        b.name = "aardvark";
        QVERIFY(store.save(b).ok);
        const QList<TemplateSummary> list = store.list();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list[0].id, builtInLyricsTemplateId());   // built-ins first
        QVERIFY(list[0].builtIn);
        QCOMPARE(list[1].name, QStringLiteral("aardvark"));
        QCOMPARE(list[2].name, QStringLiteral("Another"));
        QCOMPARE(list[2].kind, TemplateKind::LowerThird);

        QVERIFY(store.remove(b.id).ok);
        QVERIFY(!store.remove(b.id).ok);
        QVERIFY(!store.load(b.id));
    }

    void builtInsCanOnlyBeRead()
    {
        TemplateStore store(*m_library);
        compareTemplates(*store.load(builtInLyricsTemplateId()), builtInLyricsTemplate());
        Template edited = builtInLyricsTemplate();
        edited.name = "Mine";
        QVERIFY(!store.save(edited).ok);
        QCOMPARE(edited.revision, builtInLyricsTemplate().revision);   // untouched
        QVERIFY(!store.remove(builtInLyricsTemplateId()).ok);
        QCOMPARE(store.load(builtInLyricsTemplateId())->name, QStringLiteral("Lyrics"));

        Template unnamed;
        QVERIFY(!store.save(unnamed).ok);
        QVERIFY(unnamed.id.isNull());   // a failed save leaves it as it was
    }

    // The outputs' fallback (#93) must work even if the template table can't be read.
    void builtInsSurviveAnUnreadableTable()
    {
        QSqlQuery q(m_library->database());
        QVERIFY(q.exec("DROP TABLE template"));
        TemplateStore store(*m_library);
        QCOMPARE(store.list().size(), 1);
        QVERIFY(store.load(builtInLyricsTemplateId()));
    }

    void aStoredTemplateWithBadValuesStillLoads()
    {
        const QUuid id = QUuid::createUuid();
        QSqlQuery q(m_library->database());
        q.prepare("INSERT INTO template (id, name, kind, body, created_at, updated_at) VALUES (?, 'T', 'lyrics', ?, '', '')");
        q.addBindValue(id.toString(QUuid::WithoutBraces));
        q.addBindValue(QString::fromUtf8(minimal({{"regions", QJsonArray{QJsonObject{{"id", "main"}, {"color", 5}}}}})));
        QVERIFY(q.exec());
        QStringList warnings;
        const std::optional<Template> t = TemplateStore(*m_library).load(id, &warnings);
        QVERIFY(t);
        QCOMPARE(t->id, id);
        QCOMPARE(warnings.size(), 1);
    }

    // A library from before templates gains them, and its songs keep everything.
    void anExistingLibraryMigrates()
    {
        QTemporaryDir dir;
        const auto &all = Library::migrations();
        const std::vector<Migration> v4(all.begin(), all.begin() + 4);   // before templates
        QCOMPARE(v4.back().version, 4);
        {
            auto old = Library::open(dir.path(), v4);
            QVERIFY2(old.library, qPrintable(old.message));
            QSqlQuery q(old.library->database());
            QVERIFY(q.exec("INSERT INTO song (id, title, created_at, updated_at) VALUES "
                           "('2d6c8a0e-0000-4000-8000-000000000001', 'Holy, Holy, Holy', "
                           "'2026-01-01T00:00:00.000Z', '2026-01-01T00:00:00.000Z')"));
            QVERIFY(q.exec("INSERT INTO song_group (id, song_id, position, label, color, slides) VALUES "
                           "('2d6c8a0e-0000-4000-8000-000000000002', '2d6c8a0e-0000-4000-8000-000000000001', "
                           "0, 'Verse 1', '', '[\"one\",\"two\"]')"));
        }
        auto now = Library::open(dir.path());
        QVERIFY2(now.library, qPrintable(now.message));
        QCOMPARE(now.library->schemaVersion(), Library::latestVersion());
        const std::optional<Song> song =
            SongStore(*now.library).load(QUuid::fromString(QStringLiteral("2d6c8a0e-0000-4000-8000-000000000001")));
        QVERIFY(song);
        QCOMPARE(song->title, QStringLiteral("Holy, Holy, Holy"));
        QVERIFY(song->defaultTemplate.isNull());
        QCOMPARE(song->groups.size(), 1);
        QCOMPARE(song->groups[0].slides, QStringList({"one", "two"}));
        QCOMPARE(song->groups[0].slideOverrides, QList<QJsonObject>({{}, {}}));

        TemplateStore templates(*now.library);
        QCOMPARE(templates.list().size(), 1);   // only the built-in
        Template t = sample();
        QVERIFY(templates.save(t).ok);
    }

    void slideOverridesAlwaysMatchTheSlides()
    {
        SongStore store(*m_library);
        const QJsonObject gold{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ffcc00"}}}}}};
        Song s;
        s.title = "Overrides";
        s.defaultTemplate = QUuid::createUuid();
        s.groups = {Group{{}, "Short", {}, {"a", "b", "c"}}, Group{{}, "Long", {}, {"d"}}, Group{{}, "None", {}, {"e", "f"}}};
        s.groups[0].slideOverrides = {gold};                       // fewer than the slides: padded
        s.groups[1].slideOverrides = {gold, gold, gold};           // more: trimmed
        QVERIFY(store.save(s).ok);
        const std::optional<Song> loaded = store.load(s.id);
        QVERIFY(loaded);
        for (const Group &g : loaded->groups)
            QCOMPARE(g.slideOverrides.size(), g.slides.size());
        QCOMPARE(loaded->groups[0].slideOverrides, QList<QJsonObject>({gold, {}, {}}));
        QCOMPARE(loaded->groups[1].slideOverrides, QList<QJsonObject>({gold}));
        QCOMPARE(loaded->groups[2].slideOverrides, QList<QJsonObject>({{}, {}}));
        QCOMPARE(loaded->defaultTemplate, s.defaultTemplate);
        QCOMPARE(*loaded, s);   // the caller's song got the same fitting

        // Removing a slide and saving again keeps them in step.
        Song edited = *loaded;
        edited.groups[0].slides.removeLast();
        QVERIFY(store.save(edited).ok);
        QCOMPARE(store.load(s.id)->groups[0].slideOverrides.size(), 2);
    }

    void songJsonCarriesOverridesOnlyWhenThereAreSome()
    {
        Song s;
        s.title = "Plain";
        s.groups = {Group{QUuid::createUuid(), "Verse", {}, {"a", "b"}}};
        const QJsonObject plain = object(songToJson(s));
        QVERIFY(!plain.contains("defaultTemplate"));
        QVERIFY(!plain["groups"][0].toObject().contains("slideOverrides"));   // files look as they did

        const QJsonObject gold{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ffcc00"}}}}}};
        s.defaultTemplate = QUuid::createUuid();
        s.groups[0].slideOverrides = {{}, gold};
        const SongJsonResult r = songFromJson(songToJson(s));
        QVERIFY2(r.song, qPrintable(r.error));
        QCOMPARE(r.song->defaultTemplate, s.defaultTemplate);
        QCOMPARE(r.song->groups[0].slideOverrides, s.groups[0].slideOverrides);
    }

    void anOverrideWinsAndTheRestFollowsTheTemplate()
    {
        Template t = builtInLyricsTemplate();
        const QJsonObject item{{"regions", QJsonObject{{"main", QJsonObject{
            {"color", "#ffcc00"}, {"shadow", QJsonObject{{"opacity", 0.3}}}}}}}};
        RegionStyle s = *effectiveStyle(t, "main", item);
        QCOMPARE(s.color, QStringLiteral("#ffcc00"));
        QCOMPARE(s.shadow.opacity, 0.3);
        QCOMPARE(s.shadow.color, t.regions[0].style.shadow.color);
        QCOMPARE(s.fontFamily, t.regions[0].style.fontFamily);

        // Change the template: every property that isn't overridden follows it.
        RegionStyle &base = t.regions[0].style;
        base.color = "#00ff00";
        base.fontFamily = "Avenir Next";
        base.maxSize = 0.07;
        base.shadow.opacity = 0.9;
        base.shadow.color = "#202020";
        base.frame = QRectF(0.1, 0.5, 0.8, 0.4);
        s = *effectiveStyle(t, "main", item);
        RegionStyle expected = base;
        expected.color = "#ffcc00";
        expected.shadow.opacity = 0.3;
        QCOMPARE(s, expected);

        // No overrides is the template itself.
        QCOMPARE(*effectiveStyle(t, "main", QJsonObject()), base);
    }

    void theSlideBeatsTheItem()
    {
        const Template t = builtInLyricsTemplate();
        const QJsonObject item{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ffcc00"}, {"weight", 800}}}}}};
        const QJsonObject slide{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ff0000"}}}}}};
        const RegionStyle s = *effectiveStyle(t, "main", QList<QJsonObject>{item, slide});
        QCOMPARE(s.color, QStringLiteral("#ff0000"));
        QCOMPARE(s.weight, 800);
    }

    void aBadOverrideShowsTheTemplate()
    {
        const Template t = builtInLyricsTemplate();
        QStringList warnings;
        const QJsonObject o{{"regions", QJsonObject{{"main", QJsonObject{{"color", "gold"}, {"frame", QJsonObject{{"y", 0.6}}}}}}}};
        const RegionStyle s = *effectiveStyle(t, "main", o, &warnings);
        QCOMPARE(s, t.regions[0].style);   // y 0.6 with h 0.84 runs off the output
        QCOMPARE(warnings.size(), 2);

        warnings.clear();
        QCOMPARE(*effectiveStyle(t, "main", QJsonObject{{"regions", 3}}, &warnings), t.regions[0].style);
        QCOMPARE(warnings.size(), 1);
        QVERIFY(!effectiveStyle(t, "nope", QJsonObject()));
    }

    void anOverrideCanPickTheTemplate()
    {
        const QUuid id = QUuid::createUuid();
        QCOMPARE(overrideTemplateId({{"template", id.toString(QUuid::WithoutBraces)}}), id);
        QVERIFY(overrideTemplateId({}).isNull());
        QVERIFY(overrideTemplateId({{"template", 4}}).isNull());
    }

    // Codex on #102 (1): what's saved must load back with the same regions.
    void saveRefusesWhatTheReaderWouldChange_data()
    {
        QTest::addColumn<QStringList>("ids");
        QTest::addColumn<QString>("color");
        QTest::newRow("spaces around an id") << QStringList{" main "} << "#ffffff";
        QTest::newRow("the same id once trimmed") << QStringList{"main", "main\t"} << "#ffffff";
        QTest::newRow("an id over the limit") << QStringList{QString(templatelimits::kTextLength + 1, 'x')} << "#ffffff";
        QTest::newRow("a value the reader replaces") << QStringList{"main"} << "white";
    }
    void saveRefusesWhatTheReaderWouldChange()
    {
        QFETCH(QStringList, ids);
        QFETCH(QString, color);
        Template t;
        t.name = "Odd";
        for (const QString &id : ids) {
            RegionStyle style;
            style.color = color;
            t.regions << TextRegion{id, {}, style, {}};
        }
        const Template before = t;
        TemplateStore store(*m_library);
        const TemplateStore::Result r = store.save(t);
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QCOMPARE(t, before);
        QCOMPARE(store.list().size(), 1);   // only the built-in
    }

    // Codex on #102 (4).
    void aRevisionAtTheLimitIsRefusedNotOverflowed()
    {
        Template t = sample();
        t.revision = std::numeric_limits<int>::max();
        QVERIFY(!TemplateStore(*m_library).save(t).ok);
        QCOMPARE(t.revision, std::numeric_limits<int>::max());
        t.revision = -1;
        QVERIFY(!TemplateStore(*m_library).save(t).ok);
    }

    // Codex on #102 (2): rules between properties hold for the final style, not each layer.
    void layersAreCheckedTogether()
    {
        const Template t = builtInLyricsTemplate();
        const RegionStyle &base = t.regions[0].style;
        auto layer = [](const QJsonObject &main) { return QJsonObject{{"regions", QJsonObject{{"main", main}}}}; };
        QStringList warnings;

        // Alone, the item's maxSize is below the template's minSize; the slide's isn't.
        RegionStyle s = *effectiveStyle(t, "main", {layer({{"maxSize", 0.02}}), layer({{"maxSize", 0.08}})}, &warnings);
        QCOMPARE(s.maxSize, 0.08);
        QCOMPARE(s.minSize, base.minSize);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));

        // A frame moved down by the item and shortened by the slide fits once both apply.
        s = *effectiveStyle(t, "main", {layer({{"frame", QJsonObject{{"y", 0.6}}}}), layer({{"frame", QJsonObject{{"h", 0.3}}}})}, &warnings);
        QCOMPARE(s.frame, QRectF(base.frame.x(), 0.6, base.frame.width(), 0.3));
        QVERIFY(warnings.isEmpty());

        // The final style still obeys them.
        s = *effectiveStyle(t, "main", layer({{"maxSize", 0.02}}), &warnings);
        QCOMPARE(s.minSize, 0.02);
        QCOMPARE(warnings.size(), 1);
    }

    // Codex (3) and claude-3 (b) on #102: one form of "no overrides", at every boundary.
    void slideOverridesFitAtTheJsonBoundaries()
    {
        const QJsonObject gold{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ffcc00"}}}}}};
        Song s;
        s.title = "Fitting";
        s.groups = {Group{QUuid::createUuid(), "Short", {}, {"a", "b", "c"}}, Group{QUuid::createUuid(), "Long", {}, {"d"}},
                    Group{QUuid::createUuid(), "Empty", {}, {"e"}}};
        s.groups[0].slideOverrides = {gold};
        s.groups[1].slideOverrides = {gold, gold, gold};
        s.groups[2].slideOverrides = {{}, {}};   // all empty: written as none

        const QJsonObject written = object(songToJson(s));
        QCOMPARE(written["groups"][0]["slideOverrides"].toArray().size(), 3);
        QCOMPARE(written["groups"][1]["slideOverrides"].toArray().size(), 1);
        QVERIFY(!written["groups"][2].toObject().contains("slideOverrides"));

        const SongJsonResult r = songFromJson(songToJson(s));
        QVERIFY2(r.song, qPrintable(r.error));
        for (const Group &g : r.song->groups)
            QCOMPARE(g.slideOverrides.size(), g.slides.size());
        QCOMPARE(r.song->groups[0].slideOverrides, QList<QJsonObject>({gold, {}, {}}));
        QCOMPARE(r.song->groups[1].slideOverrides, QList<QJsonObject>({gold}));

        // A song from the library equals the same song from its export, and from before saving.
        SongStore store(*m_library);
        Song plain = s;
        for (Group &g : plain.groups)
            g.slideOverrides.clear();
        Song saved = plain;
        QVERIFY(store.save(saved).ok);
        const Song loaded = *store.load(saved.id);
        QCOMPARE(*songFromJson(songToJson(loaded)).song, loaded);
        plain.id = saved.id;
        plain.created = saved.created;
        plain.updated = saved.updated;
        QCOMPARE(loaded, plain);
    }

    // PR #123 review: an oversized override already in a library is kept as stored, and
    // never lost to a write of something else. Nothing writes a new one. A file's is dropped
    // on import, with a warning.
    void anOversizedStoredOverrideSurvivesOtherWrites()
    {
        SongStore store(*m_library);
        const QJsonObject gold{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ffcc00"}}}}}};
        const QJsonObject big{{"later", QString(20 * 1024, 'x')}};
        Song s;
        s.title = "Big";
        s.groups = {Group{{}, "Verse", {}, {"a", "b", "c"}}};
        QVERIFY(store.save(s).ok);
        storeOverridesRaw(s.groups[0].id, QJsonArray{gold, big, QJsonObject{}});
        const QString stored = storedOverridesRaw(s.groups[0].id);

        Song loaded = *store.load(s.id);
        QCOMPARE(loaded.groups[0].slideOverrides[1], big);   // as stored; rendering leaves it out
        loaded.title = "Big, renamed";
        QVERIFY2(store.save(loaded).ok, "a title save");
        QCOMPARE(storedOverridesRaw(s.groups[0].id), stored);
        loaded.groups[0].slideOverrides[2] = gold;   // another slide's style
        const SongStore::Result r = store.saveSlideOverrides(loaded);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(store.load(s.id)->groups[0].slideOverrides, (QList<QJsonObject>{gold, big, gold}));

        Song bigger = loaded;   // a new oversized one is refused
        bigger.groups[0].slideOverrides[0] = QJsonObject{{"later", QString(20 * 1024, 'y')}};
        QVERIFY(!store.save(bigger).ok);
        QVERIFY(!store.saveSlideOverrides(bigger).ok);

        Song imported = s;
        imported.groups[0].slideOverrides = {gold, big, {}};
        const SongJsonResult r2 = songFromJson(songToJson(imported));
        QVERIFY2(r2.song, qPrintable(r2.error));
        QCOMPARE(r2.song->groups[0].slideOverrides, (QList<QJsonObject>{gold, {}, {}}));
        QCOMPARE(r2.warnings.size(), 1);
    }

    // Codex on #102 (5): unusable overrides are reported, at import too.
    void malformedSlideOverridesAreReported()
    {
        QJsonObject song{{"format", "visualis-song"}, {"version", 1}, {"title", "Malformed"},
                         {"groups", QJsonArray{
                             QJsonObject{{"label", "Verse"}, {"slides", QJsonArray{"a"}}, {"slideOverrides", 42}},
                             QJsonObject{{"label", "Chorus"}, {"slides", QJsonArray{"b", "c"}}, {"slideOverrides", QJsonArray{42, QJsonObject{}}}},
                             QJsonObject{{"label", "Tag"}, {"slides", QJsonArray{"d"}}, {"slideOverrides", QJsonArray{QJsonObject{}, QJsonObject{}}}},
                         }}};
        const SongJsonResult r = songFromJson(json(song));
        QVERIFY2(r.song, qPrintable(r.error));
        QCOMPARE(r.warnings.size(), 3);
        QVERIFY(r.warnings[0].contains("Verse"));
        QVERIFY(r.warnings[1].contains("slide 1"));
        QVERIFY(r.warnings[2].contains("2 slide overrides for 1 slides"));
        for (const Group &g : r.song->groups)
            QCOMPARE(g.slideOverrides.size(), g.slides.size());

        QTemporaryDir folder;
        QFile f(folder.filePath("Malformed.visualis-song.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(json(song));
        f.close();
        SongStore store(*m_library);
        const SongImportReport report = importSongs(folder.path(), store);
        QCOMPARE(report.imported, QStringList{"Malformed"});
        QCOMPARE(report.warnings.size(), 3);
        QVERIFY(report.warnings[0].startsWith("Malformed.visualis-song.json: "));
    }

    // claude-3 on #102 (a): an editor save keeps the overrides of slides it didn't change.
    void editingLyricsKeepsSlideOverrides()
    {
        const QJsonObject gold{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ffcc00"}}}}}};
        Song s;
        s.title = "Edited";
        s.groups = {Group{QUuid::createUuid(), "Verse 1", {}, {"one", "two"}}, Group{QUuid::createUuid(), "Chorus", {}, {"sing"}}};
        s.groups[0].slideOverrides = {{}, gold};
        s.groups[1].slideOverrides = {gold};

        // Unchanged text keeps everything.
        LyricsEdit same = applyLyricsText(s, lyricsText(s));
        QCOMPARE(same.song.groups[0].slideOverrides, s.groups[0].slideOverrides);
        QCOMPARE(same.song.groups[1].slideOverrides, s.groups[1].slideOverrides);
        QVERIFY(same.warnings.isEmpty());

        // Editing the chorus's one slide keeps both groups' (#95: it follows by position).
        const LyricsEdit edited = applyLyricsText(s, "[Verse 1]\none\n\ntwo\n\n[Chorus]\nsing it");
        QCOMPARE(edited.song.groups[0].slideOverrides, s.groups[0].slideOverrides);
        QCOMPARE(edited.song.groups[1].slideOverrides, s.groups[1].slideOverrides);
        QVERIFY(edited.warnings.isEmpty());
    }

    // Codex's second pass on #102 (2): the caller gets the template as it loads back.
    void saveReturnsTheTemplateAsItLoads()
    {
        TemplateStore store(*m_library);
        Template t = sample();
        t.name = "  Spaced name  ";
        t.regions[0].name = " Title ";
        t.regions[0].style.fontFamily = " Avenir Next\t";
        t.regions[0].style.color = "#80FFCC00";
        t.regions[0].style.shadow.color = "#ABCDEF";
        QVERIFY(store.save(t).ok);
        QCOMPARE(t.name, QStringLiteral("Spaced name"));
        QCOMPARE(t.regions[0].name, QStringLiteral("Title"));
        QCOMPARE(t.regions[0].style.fontFamily, QStringLiteral("Avenir Next"));
        QCOMPARE(t.regions[0].style.color, QStringLiteral("#80ffcc00"));
        QCOMPARE(t.regions[0].style.shadow.color, QStringLiteral("#abcdef"));
        compareTemplates(*store.load(t.id), t);
        QCOMPARE(store.list().last().name, QStringLiteral("Spaced name"));
    }

    // Codex's second pass on #102 (1): the parser tidies slides it reads, so a group the
    // operator didn't touch must still count as unchanged and keep its slides as they were.
    void untouchedSlidesKeepTheirTextAndOverrides()
    {
        const QJsonObject gold{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ffcc00"}}}}}};
        Song s;
        s.title = "Untidy";
        s.groups = {Group{QUuid::createUuid(), "Verse 1", {}, {" one ", "two\t\nthree", QStringLiteral("four ")}},
                    Group{QUuid::createUuid(), "Chorus", {}, {"sing"}}};
        s.groups[0].slideOverrides = {gold, {}, gold};

        const LyricsEdit same = applyLyricsText(s, lyricsText(s));
        QCOMPARE(same.song.groups[0].slides, s.groups[0].slides);
        QCOMPARE(same.song.groups[0].slideOverrides, s.groups[0].slideOverrides);
        QVERIFY2(same.warnings.isEmpty(), qPrintable(same.warnings.join('\n')));

        // Editing another group leaves this one alone too.
        QString text = lyricsText(s);
        text.replace("[Chorus]\nsing", "[Chorus]\nsing it");
        const LyricsEdit other = applyLyricsText(s, text);
        QCOMPARE(other.song.groups[0].slides, s.groups[0].slides);
        QCOMPARE(other.song.groups[0].slideOverrides, s.groups[0].slideOverrides);
        QVERIFY(other.warnings.isEmpty());
    }

    // #122 review: the template editor saves from the GUI thread while the service runs, so
    // another program holding the write lock makes a save fail at once with `busy` instead of
    // freezing the operator window and the outputs for the busy timeout.
    void aSaveWhileAnotherProgramWritesIsBusyAtOnce()
    {
        TemplateStore store(*m_library);
        Template kept = sample();
        QVERIFY(store.save(kept).ok);
        {
            QSqlDatabase other = QSqlDatabase::addDatabase("QSQLITE", "other-program");
            other.setDatabaseName(m_library->databasePath());
            QVERIFY(other.open());
            QVERIFY(QSqlQuery(other).exec("BEGIN IMMEDIATE"));

            QElapsedTimer t;
            t.start();
            Template edited = kept;
            edited.name = "Edited";
            const TemplateStore::Result saved = store.save(edited);
            const TemplateStore::Result removed = store.remove(kept.id);
            QVERIFY(!saved.ok && saved.busy);
            QVERIFY(!removed.ok && removed.busy);
            QCOMPARE(edited.name, QStringLiteral("Edited"));   // left as it was
            QCOMPARE(edited.revision, kept.revision);
            QVERIFY2(t.elapsed() < Library::kBusyTimeoutMs / 2, qPrintable(QString::number(t.elapsed())));

            QVERIFY(QSqlQuery(other).exec("ROLLBACK"));
            other.close();
        }
        QSqlDatabase::removeDatabase("other-program");
        Template edited = kept;
        QVERIFY(store.save(edited).ok);
        QVERIFY(store.remove(kept.id).ok);
    }

    // #94: dragging a frame in the editor keeps it on the output and no smaller than the minimum.
    void draggingAFrameStaysOnTheOutput_data()
    {
        QTest::addColumn<int>("edges");
        QTest::addColumn<QPointF>("delta");
        QTest::addColumn<QRectF>("expected");
        const int all = Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge;
        // From (0.1, 0.2, 0.5, 0.4): right edge 0.6, bottom 0.6.
        QTest::newRow("move") << all << QPointF(0.1, -0.1) << QRectF(0.2, 0.1, 0.5, 0.4);
        QTest::newRow("move past the right and bottom stops at the edge") << all << QPointF(0.9, 0.9) << QRectF(0.5, 0.6, 0.5, 0.4);
        QTest::newRow("move past the left and top stops at the edge") << all << QPointF(-1, -1) << QRectF(0, 0, 0.5, 0.4);
        QTest::newRow("right edge grows") << int(Qt::RightEdge) << QPointF(0.2, 0.5) << QRectF(0.1, 0.2, 0.7, 0.4);
        QTest::newRow("right edge stops at the output") << int(Qt::RightEdge) << QPointF(2, 0) << QRectF(0.1, 0.2, 0.9, 0.4);
        QTest::newRow("left edge past 0 shrinks nothing on the right") << int(Qt::LeftEdge) << QPointF(-0.5, 0) << QRectF(0, 0.2, 0.6, 0.4);
        QTest::newRow("left edge stops at the minimum width") << int(Qt::LeftEdge) << QPointF(0.9, 0) << QRectF(0.6 - kMinFrameSize, 0.2, kMinFrameSize, 0.4);
        QTest::newRow("top-left corner") << int(Qt::LeftEdge | Qt::TopEdge) << QPointF(-0.05, -0.1) << QRectF(0.05, 0.1, 0.55, 0.5);
        QTest::newRow("bottom edge stops at the minimum height") << int(Qt::BottomEdge) << QPointF(0, -1) << QRectF(0.1, 0.2, 0.5, kMinFrameSize);
    }

    void draggingAFrameStaysOnTheOutput()
    {
        QFETCH(int, edges);
        QFETCH(QPointF, delta);
        QFETCH(QRectF, expected);
        const QRectF r = draggedFrame(QRectF(0.1, 0.2, 0.5, 0.4), Qt::Edges(edges), delta);
        QVERIFY2(qAbs(r.x() - expected.x()) < 1e-9 && qAbs(r.y() - expected.y()) < 1e-9
                     && qAbs(r.width() - expected.width()) < 1e-9 && qAbs(r.height() - expected.height()) < 1e-9,
                 qPrintable(QStringLiteral("got %1 %2 %3 %4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height())));
    }

private:
    void storeOverridesRaw(const QUuid &group, const QJsonArray &overrides)
    {
        QSqlQuery q(m_library->database());
        q.prepare("UPDATE song_group SET slide_overrides = ? WHERE id = ?");
        q.addBindValue(QString::fromUtf8(QJsonDocument(overrides).toJson(QJsonDocument::Compact)));
        q.addBindValue(group.toString(QUuid::WithoutBraces));
        QVERIFY(q.exec());
    }
    QString storedOverridesRaw(const QUuid &group) const
    {
        QSqlQuery q(m_library->database());
        q.prepare("SELECT slide_overrides FROM song_group WHERE id = ?");
        q.addBindValue(group.toString(QUuid::WithoutBraces));
        return q.exec() && q.next() ? q.value(0).toString() : QString();
    }

    std::optional<QTemporaryDir> m_dir;
    std::unique_ptr<Library> m_library;
};

QTEST_MAIN(TemplateTest)
#include "tst_template.moc"
