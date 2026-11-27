#include "library/songeditor.h"
#include "song/songlimits.h"
#include "library/songlibrary.h"
#include "library/templatelibrary.h"
#include "library/library.h"
#include "live/livestate.h"
#include "song/slidelist.h"
#include "song/songtext.h"
#include "template/overrides.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include "otherprogram.h"
#include "testkeychain.h"

Q_IMPORT_QML_PLUGIN(Visualis_UiPlugin)

using namespace visualis;

namespace {

const char *kLyrics =
    "Intro\n\nVerse 1\nAmazing grace how sweet the sound\nThat saved a wretch like me\n\n"
    "Chorus\nMy chains are gone\nI've been set free\n\nVerse 1\n\nChorus\n";   // V1 C V1 C

QString idText(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }

QStringList strings(const QVariantList &list, const char *key)
{
    QStringList out;
    for (const QVariant &v : list)
        out << v.toMap().value(key).toString();
    return out;
}

} // namespace

class SongEditorTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { useTestKeychainEntry(); }

    void init()
    {
        QVERIFY(m_dir.isValid());
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_songs = m_engine->singletonInstance<SongLibrary *>("Visualis.Ui", "SongLibrary");
        m_live = m_engine->singletonInstance<LiveState *>("Visualis.Ui", "LiveState");
        m_templates = m_engine->singletonInstance<TemplateLibrary *>("Visualis.Ui", "TemplateLibrary");
        QVERIFY(m_songs->openLibrary(m_dir.filePath(QString::number(++m_n))));
        QVERIFY(m_songs->createFromPaste("Amazing Grace", kLyrics));
        QQmlComponent component(m_engine.get());
        component.setData("import Visualis.Ui\nSongEditor {}", {});
        m_editor.reset(qobject_cast<SongEditor *>(component.create()));
        QVERIFY2(m_editor, qPrintable(component.errorString()));
        m_editor->setSongId(m_songs->selectedId());
    }

    void cleanup()
    {
        m_editor.reset();
        m_engine.reset();
    }

    void loadsTheSong()
    {
        QVERIFY(m_editor->loaded());
        QCOMPARE(m_editor->title(), QStringLiteral("Amazing Grace"));
        QCOMPARE(strings(m_editor->groups(), "label"), (QStringList{"Intro", "Verse 1", "Chorus"}));
        QVERIFY(m_editor->lyrics().startsWith("[Intro]\n\n[Verse 1]\nAmazing grace"));
        QCOMPARE(strings(m_editor->sequence(), "label"), (QStringList{"Intro", "Verse 1", "Chorus", "Verse 1", "Chorus"}));
        QCOMPARE(m_editor->previewSlides().size(), 5);
    }

    void lyricEditsSaveAtOnceAndKeepTheArrangement()
    {
        QString text = m_editor->lyrics();
        text.replace("set free", "set free, my God my Savior");
        m_editor->applyLyrics(text);
        QVERIFY(m_editor->error().isEmpty());
        const Song saved = *m_songs->loadSong(QUuid::fromString(m_editor->songId()));
        QCOMPARE(saved.groups[2].slides[0], QStringLiteral("My chains are gone\nI've been set free, my God my Savior"));
        QCOMPARE(strings(m_editor->sequence(), "label"), (QStringList{"Intro", "Verse 1", "Chorus", "Verse 1", "Chorus"}));
        // The operator window's grid shows the edit too.
        QVERIFY(strings(m_songs->selectedSlides(), "text").contains("My chains are gone\nI've been set free, my God my Savior"));
    }

    void titleAndCreditsSave()
    {
        m_editor->setTitle("Amazing Grace (My Chains Are Gone)");
        m_editor->setCredits("4768151", "Chris Tomlin, Louie Giglio, John Newton", "Public Domain");
        QCOMPARE(strings(m_songs->songs(), "title"), QStringList{"Amazing Grace (My Chains Are Gone)"});
        const Song saved = *m_songs->loadSong(QUuid::fromString(m_editor->songId()));
        QCOMPARE(saved.credits.ccliNumber, QStringLiteral("4768151"));
        m_editor->setTitle("   ");   // a title can't be emptied
        QCOMPARE(m_editor->title(), QStringLiteral("Amazing Grace (My Chains Are Gone)"));
    }

    void renamingAGroupUpdatesTheTextAndKeepsItsPlaces()
    {
        QSignalSpy reset(m_editor.get(), &SongEditor::lyricsReset);
        const QString chorusId = strings(m_editor->groups(), "id")[2];
        m_editor->renameGroup(chorusId, "Refrain");
        QCOMPARE(reset.count(), 1);
        QVERIFY(m_editor->lyrics().contains("[Refrain]"));
        QCOMPARE(strings(m_editor->sequence(), "label"), (QStringList{"Intro", "Verse 1", "Refrain", "Verse 1", "Refrain"}));
        m_editor->setGroupColor(chorusId, "#123456");
        QCOMPARE(m_songs->loadSong(QUuid::fromString(m_editor->songId()))->group(QUuid::fromString(chorusId))->color,
                 QStringLiteral("#123456"));
    }

    // #99: the song's and a group's default backgrounds save at once; "" clears them.
    void backgroundsAreSetOnTheSongAndItsGroups()
    {
        const QString asset = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString chorusId = strings(m_editor->groups(), "id")[2];
        const QUuid songId = QUuid::fromString(m_editor->songId());
        m_editor->setBackgroundId(asset);
        m_editor->setGroupBackground(chorusId, asset);
        QCOMPARE(m_editor->backgroundId(), asset);
        QCOMPARE(strings(m_editor->groups(), "backgroundId")[2], asset);
        QCOMPARE(strings(m_editor->groups(), "backgroundId")[0], QString());
        QCOMPARE(m_songs->loadSong(songId)->defaultBackground, QUuid::fromString(asset));
        QCOMPARE(m_songs->loadSong(songId)->group(QUuid::fromString(chorusId))->defaultBackground, QUuid::fromString(asset));

        m_editor->setBackgroundId("");
        m_editor->setGroupBackground(chorusId, "");
        QVERIFY(m_songs->loadSong(songId)->defaultBackground.isNull());
        QVERIFY(m_songs->loadSong(songId)->group(QUuid::fromString(chorusId))->defaultBackground.isNull());
    }

    // PR #127 review: picking a background mid-service never waits on another program's lock.
    // The pick stays in the editor and saves when the library is free.
    void aBusyLibraryKeepsTheBackgroundsAndSavesThemLater()
    {
        const QString asset = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString chorusId = strings(m_editor->groups(), "id")[2];
        const QUuid songId = QUuid::fromString(m_editor->songId());
        {
            OtherProgram other(m_dir.filePath(QString::number(m_n)));
            QVERIFY(QSqlQuery(other.database()).exec("BEGIN IMMEDIATE"));
            QElapsedTimer t;
            t.start();
            m_editor->setBackgroundId(asset);
            m_editor->setGroupBackground(chorusId, asset);
            QVERIFY2(t.elapsed() < Library::kBusyTimeoutMs / 2, qPrintable(QString::number(t.elapsed())));
            QVERIFY2(m_editor->error().startsWith("Not saved yet"), qPrintable(m_editor->error()));
            QCOMPARE(m_editor->backgroundId(), asset);
            QCOMPARE(strings(m_editor->groups(), "backgroundId")[2], asset);
            QVERIFY(QSqlQuery(other.database()).exec("ROLLBACK"));
        }
        QTRY_COMPARE(m_songs->loadSong(songId)->defaultBackground, QUuid::fromString(asset));
        QCOMPARE(m_songs->loadSong(songId)->group(QUuid::fromString(chorusId))->defaultBackground, QUuid::fromString(asset));
        QVERIFY(m_editor->error().isEmpty());
    }

    // The rule's one exception: at quit, a save the busy library hasn't taken waits it out.
    void aBackgroundTheBusyLibraryHasntTakenSavesAtQuit()
    {
        const QString asset = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QUuid songId = QUuid::fromString(m_editor->songId());
        {
            OtherProgram other(m_dir.filePath(QString::number(m_n)));
            QVERIFY(QSqlQuery(other.database()).exec("BEGIN IMMEDIATE"));
            m_editor->setBackgroundId(asset);
            QVERIFY(QSqlQuery(other.database()).exec("ROLLBACK"));
        }
        m_editor.reset();   // before the retry runs
        QCOMPARE(m_songs->loadSong(songId)->defaultBackground, QUuid::fromString(asset));
    }

    void removingAGroupRemovesItsSteps()
    {
        m_editor->removeGroup(strings(m_editor->groups(), "id")[0]);
        QCOMPARE(strings(m_editor->sequence(), "label"), (QStringList{"Verse 1", "Chorus", "Verse 1", "Chorus"}));
        QVERIFY(!m_editor->lyrics().contains("[Intro]"));
    }

    // #83: the editor's save is refused past a limit, and the operator sees why.
    void aStepPastTheLimitIsRefusedWithTheReason()
    {
        Song song = *m_songs->loadSong(QUuid::fromString(m_editor->songId()));
        QVERIFY(!song.arrangements.isEmpty());
        song.arrangements[0].sequence = QList<QUuid>(songlimits::kSteps, song.groups[1].id);
        const QUuid arrangement = song.arrangements[0].id;
        const QString name = song.arrangements[0].name;
        QString error;
        QVERIFY2(m_songs->saveSong(song, &error), qPrintable(error));
        m_editor->setSongId(idText(song.id));
        m_editor->selectArrangement(idText(arrangement));
        QCOMPARE(m_editor->sequence().size(), songlimits::kSteps);

        m_editor->addStep(strings(m_editor->groups(), "id")[2]);
        QVERIFY2(m_editor->error().contains(QStringLiteral("too many steps in arrangement \"%1\": 1,001; the limit is 1,000")
                                                .arg(name)),
                 qPrintable(m_editor->error()));
        QCOMPARE(m_songs->loadSong(song.id)->arrangement(arrangement)->sequence.size(), songlimits::kSteps);
        m_editor->removeStep(songlimits::kSteps);   // back within the limit: saves again
        QVERIFY2(m_editor->error().isEmpty(), qPrintable(m_editor->error()));
    }

    void stepsMoveAddAndRemove()
    {
        m_editor->moveStep(4, -1);   // ... V1 C V1 C -> ... V1 C C V1
        QCOMPARE(strings(m_editor->sequence(), "label"), (QStringList{"Intro", "Verse 1", "Chorus", "Chorus", "Verse 1"}));
        m_editor->removeStep(0);
        m_editor->addStep(strings(m_editor->groups(), "id")[2]);   // just the chorus
        QCOMPARE(strings(m_editor->sequence(), "label"), (QStringList{"Verse 1", "Chorus", "Chorus", "Verse 1", "Chorus"}));
        m_editor->moveStep(0, -1);   // out of range: nothing
        m_editor->removeStep(99);
        QCOMPARE(m_editor->sequence().size(), 5);
        QCOMPARE(m_editor->previewSlides().size(), 5);
    }

    void arrangementsCanBeAddedRenamedMadeDefaultAndDeleted()
    {
        const QString original = m_editor->arrangementId();
        const QString added = m_editor->addArrangement("Short");
        QCOMPARE(m_editor->arrangementId(), added);
        while (!m_editor->sequence().isEmpty())
            m_editor->removeStep(0);
        m_editor->addStep(strings(m_editor->groups(), "id")[2]);
        m_editor->renameArrangement(added, "Chorus only");
        m_editor->makeDefaultArrangement(added);
        QCOMPARE(strings(m_editor->arrangements(), "name").last(), QStringLiteral("Chorus only"));
        // The grid and going live use the default arrangement.
        QCOMPARE(m_songs->selectedSlides().size(), 1);

        m_editor->removeArrangement(added);
        QCOMPARE(m_editor->arrangementId(), original);
        QCOMPARE(m_songs->selectedSlides().size(), 5);
    }

    void aSongWithoutAnArrangementGetsOneOnTheFirstStepChange()
    {
        QVERIFY(m_songs->createFromPaste("Simple", "Verse 1\nOne\n\nChorus\nTwo\n"));   // played in order: no arrangement
        m_editor->setSongId(m_songs->selectedId());
        QVERIFY(m_editor->arrangements().isEmpty());
        QCOMPARE(strings(m_editor->sequence(), "label"), (QStringList{"Verse 1", "Chorus"}));
        m_editor->addStep(strings(m_editor->groups(), "id")[1]);
        QCOMPARE(strings(m_editor->arrangements(), "name"), QStringList{"Default"});
        QCOMPARE(strings(m_editor->sequence(), "label"), (QStringList{"Verse 1", "Chorus", "Chorus"}));
    }

    // Decided for #44: an edit doesn't move what's on screen; the next go-live shows it.
    void editsDontChangeWhatsLive()
    {
        QVERIFY(m_songs->goLive(2));
        const QString shown = m_live->currentText();
        QString text = m_editor->lyrics();
        text.replace("My chains are gone", "My chains are GONE");
        m_editor->applyLyrics(text);
        QCOMPARE(m_live->currentText(), shown);
        QVERIFY(m_songs->goLive(2));
        QVERIFY(m_live->currentText().contains("GONE"));
    }

    void deletingASongRemovesItButNotWhatsLive()
    {
        QVERIFY(m_songs->goLive(1));
        QVERIFY(m_editor->deleteSong());
        QVERIFY(m_songs->songs().isEmpty());
        QVERIFY(!m_editor->loaded());
        QVERIFY(m_songs->selectedId().isEmpty());
        QVERIFY(m_live->hasSlide());
    }

    void aSongThatsGoneIsReported()
    {
        const QString id = m_editor->songId();
        QVERIFY(m_songs->removeSong(id));
        m_editor->setSongId(id);
        QVERIFY(!m_editor->loaded());
        QVERIFY(!m_editor->error().isEmpty());
    }

    // While the editor window is in front, operator keys don't fire: Esc in the editor
    // must never clear the screen.
    void operatorKeysAreOffWhileEditing()
    {
        m_engine->loadFromModule("Visualis.Ui", "Main");
        auto *main = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        QVERIFY(main);
        QVERIFY(m_songs->goLive(0));
        auto *editorWindow = main->findChild<QQuickWindow *>("songEditorWindow");
        QVERIFY(editorWindow);
        QMetaObject::invokeMethod(editorWindow, "edit", Q_ARG(QVariant, m_songs->selectedId()));
        QVERIFY(QTest::qWaitForWindowActive(editorWindow));
        QTest::keyClick(editorWindow, Qt::Key_Escape);
        QTest::keyClick(editorWindow, Qt::Key_Right);
        QVERIFY(!m_live->textCleared());
        QCOMPARE(m_live->slideIndex(), 0);

        if (const QString dir = qEnvironmentVariable("VISUALIS_SNAPSHOT_DIR"); !dir.isEmpty()) {
            QTest::qWait(300);
            editorWindow->grabWindow().save(dir + "/song-editor.png");
        }
        editorWindow->close();
        main->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(main));
        QTest::keyClick(main, Qt::Key_Right);
        QCOMPARE(m_live->slideIndex(), 1);
    }

    // #93 review: an open editor's preview follows a template saved or removed meanwhile.
    void thePreviewFollowsATemplateChange()
    {
        auto *templates = m_engine->singletonInstance<TemplateLibrary *>("Visualis.Ui", "TemplateLibrary");
        Template t = builtInLyricsTemplate();
        t.id = QUuid();
        t.name = "Ours";
        t.regions[0].style.fontFamily = "Avenir";
        QVERIFY(templates->save(t).ok);
        Song song = *m_songs->loadSong(QUuid::fromString(m_editor->songId()));
        song.defaultTemplate = t.id;
        QVERIFY(m_songs->saveSong(song, nullptr));
        m_editor->setSongId(m_editor->songId());
        auto previewFont = [this] {
            return m_editor->previewSlides().value(1).toMap()["content"].value<Content>().regions.value("main").style.fontFamily;
        };
        QCOMPARE(previewFont(), QStringLiteral("Avenir"));

        QSignalSpy changed(m_editor.get(), &SongEditor::changed);
        t.regions[0].style.fontFamily = "Georgia";
        QVERIFY(templates->save(t).ok);
        QVERIFY(changed.count() >= 1);
        QCOMPARE(previewFont(), QStringLiteral("Georgia"));
        QVERIFY(templates->remove(t.id).ok);
        QCOMPARE(previewFont(), builtInLyricsTemplate().regions[0].style.fontFamily);
        QVERIFY(changed.count() >= 2);
    }

    // #94: a template made in the template editor gets onto the screen through the song.
    void pickingATemplateSavesItAndThePreviewUsesIt()
    {
        auto *templates = m_engine->singletonInstance<TemplateLibrary *>("Visualis.Ui", "TemplateLibrary");
        QCOMPARE(m_editor->templateId(), idText(builtInLyricsTemplateId()));   // by default
        QCOMPARE(strings(templates->entries(), "name"), QStringList{"Lyrics"});
        Template t = builtInLyricsTemplate();
        t.id = QUuid();
        t.name = "Christmas";
        t.regions[0].style.color = "#ff0000";
        QVERIFY(templates->save(t).ok);
        QCOMPARE(strings(templates->entries(), "name"), (QStringList{"Lyrics", "Christmas"}));

        m_editor->setTemplateId(idText(t.id));
        QCOMPARE(m_editor->templateId(), idText(t.id));
        QCOMPARE(m_songs->loadSong(QUuid::fromString(m_editor->songId()))->defaultTemplate, t.id);
        QCOMPARE(m_editor->previewSlides().value(1).toMap()["content"].value<Content>().regions.value("main").style.color,
                 QStringLiteral("#ff0000"));
        QVERIFY(m_songs->goLive(1));
        QCOMPARE(m_live->content().regions.value("main").style.color, QStringLiteral("#ff0000"));

        m_editor->setTemplateId(idText(builtInLyricsTemplateId()));   // back to the built-in
        QVERIFY(m_songs->loadSong(QUuid::fromString(m_editor->songId()))->defaultTemplate.isNull());
        QCOMPARE(m_editor->templateId(), idText(builtInLyricsTemplateId()));
    }

    // #122 review (4): the picker keeps showing the song's template as the list changes around it.
    void thePickerFollowsTheListChanging()
    {
        auto *templates = m_engine->singletonInstance<TemplateLibrary *>("Visualis.Ui", "TemplateLibrary");
        auto add = [&](const char *name) {
            Template t = builtInLyricsTemplate();
            t.id = QUuid();
            t.name = name;
            return templates->save(t).ok ? t : Template{};
        };
        add("B");
        Template d = add("D");
        m_engine->loadFromModule("Visualis.Ui", "Main");
        auto *main = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        QVERIFY(main);
        auto *editorWindow = main->findChild<QQuickWindow *>("songEditorWindow");
        QVERIFY(editorWindow);
        QMetaObject::invokeMethod(editorWindow, "edit", Q_ARG(QVariant, m_songs->selectedId()));
        auto *picker = editorWindow->findChild<QQuickItem *>("songTemplate");
        QVERIFY(picker);
        auto *windowEditor = editorWindow->findChild<SongEditor *>();
        windowEditor->setTemplateId(idText(d.id));
        QTRY_COMPARE(picker->property("displayText").toString(), QStringLiteral("D"));

        add("C");   // lands between B and D
        QTRY_COMPARE(picker->property("displayText").toString(), QStringLiteral("D"));
        d.name = "A";   // moves to the top
        QVERIFY(templates->save(d).ok);
        QTRY_COMPARE(picker->property("displayText").toString(), QStringLiteral("A"));
        QTest::qWait(50);
        QCOMPARE(picker->property("displayText").toString(), QStringLiteral("A"));
        windowEditor->setTemplateId(idText(builtInLyricsTemplateId()));   // still follows the song
        QTRY_COMPARE(picker->property("displayText").toString(), QStringLiteral("Lyrics"));
    }

    // #95 acceptance: changing the template updates every slide except what a slide overrides.
    void aSlideStyleSticksThroughATemplateChange()
    {
        Template t = saveTemplate("Ours");
        m_editor->setTemplateId(idText(t.id));
        m_editor->selectSlide(slideKey(1), false);   // Verse 1's first slide, played twice
        QCOMPARE(m_editor->selectedKeys(), QStringList{slideKey(1)});
        m_editor->setSlideProperty("color", "#ffcc00");
        QVERIFY2(m_editor->warnings().isEmpty(), qPrintable(m_editor->warnings().join('\n')));
        QVERIFY(m_editor->flush());
        QCOMPARE(storedOverride(1), (QJsonObject{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ffcc00"}}}}}}));

        t.regions[0].style.color = "#00ff00";
        t.regions[0].style.fontFamily = "Georgia";
        QVERIFY(m_templates->save(t).ok);
        QCOMPARE(previewStyle(1).color, QStringLiteral("#ffcc00"));       // sticks
        QCOMPARE(previewStyle(1).fontFamily, QStringLiteral("Georgia"));  // follows
        QCOMPARE(previewStyle(3).color, QStringLiteral("#ffcc00"));       // the same slide, played again
        QCOMPARE(previewStyle(2).color, QStringLiteral("#00ff00"));
        QCOMPARE(previewSlide(1)["overrides"].toInt(), 1);
        QCOMPARE(previewSlide(2)["overrides"].toInt(), 0);
        // And on screen.
        QVERIFY(m_songs->goLive(1));
        QCOMPARE(m_live->content().regions.value("main").style.color, QStringLiteral("#ffcc00"));
        QCOMPARE(m_live->content().regions.value("main").style.fontFamily, QStringLiteral("Georgia"));
    }

    void resetBringsBackTheTemplatesValue()
    {
        m_editor->selectSlide(slideKey(2), false);
        m_editor->setSlideProperty("color", "#ffcc00");
        m_editor->setSlideProperty("shadow.opacity", 0.2);
        QCOMPARE(m_editor->slideStyle()["color"].toString(), QStringLiteral("#ffcc00"));
        QCOMPARE(m_editor->overriddenProperties(), (QStringList{"color", "shadow.opacity"}));
        m_editor->resetSlideProperty("color");
        QCOMPARE(previewStyle(2).color, builtInLyricsTemplate().regions[0].style.color);
        QCOMPARE(m_editor->overriddenProperties(), QStringList{"shadow.opacity"});
        m_editor->resetSlides();
        QVERIFY(m_editor->overriddenProperties().isEmpty());
        QVERIFY(m_editor->flush());
        QCOMPARE(storedOverride(2), QJsonObject());
    }

    // #96: a slide overrides its text transition; the rest follow the template's.
    void aSlideOverridesItsTransition()
    {
        Template t = saveTemplate("Ours");
        t.transitions.content = {TransitionKind::Fade, 400};
        QVERIFY(m_templates->save(t).ok);
        m_editor->setTemplateId(idText(t.id));
        m_editor->selectSlide(slideKey(2), false);
        QCOMPARE(m_editor->slideTransition(), (QVariantMap{{"kind", "fade"}, {"ms", 400}}));
        QVERIFY(!m_editor->slideTransitionOverridden());
        m_editor->setSlideTransition({{"kind", "cut"}});
        QVERIFY(m_editor->warnings().isEmpty());
        QCOMPARE(m_editor->slideTransition()["kind"].toString(), QStringLiteral("cut"));
        QVERIFY(m_editor->slideTransitionOverridden());
        QCOMPARE(previewSlide(2)["content"].value<Content>().transition.kind, TransitionKind::Cut);
        QCOMPARE(previewSlide(1)["content"].value<Content>().transition.kind, TransitionKind::Fade);
        QCOMPARE(previewSlide(2)["overrides"].toInt(), 1);
        QVERIFY(m_editor->flush());
        QCOMPARE(storedOverride(2), (QJsonObject{{"transition", QJsonObject{{"kind", "cut"}}}}));

        m_editor->selectSlide(slideKey(1), true);
        QVERIFY(m_editor->slideTransitionMixed());
        // A new length across a mixed selection changes only the length: each keeps its kind.
        m_editor->setSlideTransition({{"ms", 800}});
        QCOMPARE(previewSlide(2)["content"].value<Content>().transition, (Transition{TransitionKind::Cut, 800}));
        QCOMPARE(previewSlide(1)["content"].value<Content>().transition, (Transition{TransitionKind::Fade, 800}));
        m_editor->setSlideTransition({{"ms", 60'000}});   // too long: refused for both
        QVERIFY(!m_editor->warnings().isEmpty());
        QVERIFY(!m_editor->warnings().isEmpty());
        m_editor->resetSlideTransition();
        QVERIFY(!m_editor->slideTransitionOverridden());
        QVERIFY(m_editor->flush());
        QCOMPARE(storedOverride(2), QJsonObject());
        QCOMPARE(storedOverride(1), QJsonObject());
    }

    void severalSlidesAreStyledTogetherAndMixedValuesShow()
    {
        m_editor->selectSlide(slideKey(1), false);
        m_editor->setSlideProperty("color", "#ffcc00");
        m_editor->selectSlide(slideKey(2), true);
        QCOMPARE(m_editor->selectedKeys().size(), 2);
        QCOMPARE(m_editor->mixedProperties(), QStringList{"color"});
        QCOMPARE(m_editor->overriddenProperties(), QStringList{"color"});   // by any of them
        m_editor->setSlideProperty("weight", 800);
        QCOMPARE(previewStyle(1).weight, 800);
        QCOMPARE(previewStyle(2).weight, 800);
        QCOMPARE(previewStyle(1).color, QStringLiteral("#ffcc00"));
        m_editor->selectSlide(slideKey(1), true);   // toggled off again
        QCOMPARE(m_editor->selectedKeys(), QStringList{slideKey(2)});
        QVERIFY(m_editor->mixedProperties().isEmpty());
    }

    void anInvalidStyleIsRefusedWithTheReason()
    {
        m_editor->selectSlide(slideKey(1), false);
        m_editor->selectSlide(slideKey(2), true);
        m_editor->setSlideProperty("color", "gold");
        QVERIFY(!m_editor->warnings().isEmpty());
        // Valid alone, but not with the template: larger than the largest size.
        m_editor->setSlideProperty("minSize", 0.2);
        QVERIFY2(m_editor->warnings().join(' ').contains("minSize"), qPrintable(m_editor->warnings().join(' ')));
        QVERIFY(m_editor->overriddenProperties().isEmpty());
        QVERIFY(m_editor->flush());
        QCOMPARE(storedOverride(1), QJsonObject());
    }

    // Thermo review: a reset is checked like a set.
    void aResetThatBreaksARuleIsRefused()
    {
        m_editor->selectSlide(slideKey(1), false);
        m_editor->setSlideProperty("maxSize", 0.2);
        m_editor->setSlideProperty("minSize", 0.15);
        QCOMPARE(m_editor->overriddenProperties(), (QStringList{"maxSize", "minSize"}));
        m_editor->resetSlideProperty("maxSize");   // the template's largest is smaller
        QVERIFY(m_editor->warnings().join(' ').contains("minSize"));
        QCOMPARE(m_editor->overriddenProperties(), (QStringList{"maxSize", "minSize"}));
        m_editor->resetSlides();   // both together is fine
        QVERIFY(m_editor->overriddenProperties().isEmpty());
    }

    // Thermo review: a save that failed for good doesn't keep the editor on that song.
    void anotherSongOpensAfterAStyleSaveFails()
    {
        QVERIFY(m_songs->createFromPaste("Other", "Verse 1\nOne\n"));
        const QString other = m_songs->selectedId();
        m_editor->selectSlide(slideKey(1), false);
        m_editor->setSlideProperty("color", "#ffcc00");
        QVERIFY(m_songs->removeSong(m_editor->songId()));
        QVERIFY(!m_editor->flush());
        m_editor->setSongId(other);
        QCOMPARE(m_editor->songId(), other);
    }

    // Thermo review: the region edited is one the song's template has.
    void theRegionFollowsTheTemplate()
    {
        Template two = saveTemplate("Two regions");
        two.regions << TextRegion{"title", "Title", two.regions[0].style, {}};
        QVERIFY(m_templates->save(two).ok);
        m_editor->setTemplateId(idText(two.id));
        m_editor->selectRegion("title");
        QCOMPARE(m_editor->regionId(), QStringLiteral("title"));
        m_editor->setTemplateId(idText(builtInLyricsTemplateId()));
        QCOMPARE(m_editor->regionId(), QStringLiteral("main"));
    }

    // GUI-thread writes never wait on another program's lock: the edit stays and saves later.
    void aBusyLibraryKeepsTheStyleAndSavesItLater()
    {
        m_editor->selectSlide(slideKey(1), false);
        {
            OtherProgram other(m_dir.filePath(QString::number(m_n)));
            QVERIFY(QSqlQuery(other.database()).exec("BEGIN IMMEDIATE"));
            QElapsedTimer t;
            t.start();
            m_editor->setSlideProperty("color", "#ffcc00");
            QVERIFY(!m_editor->flush());
            QVERIFY(t.elapsed() < Library::kBusyTimeoutMs / 2);
            QVERIFY2(m_editor->error().startsWith("Not saved yet"), qPrintable(m_editor->error()));
            QCOMPARE(previewStyle(1).color, QStringLiteral("#ffcc00"));   // still shown
            QVERIFY(QSqlQuery(other.database()).exec("ROLLBACK"));
        }
        QTRY_COMPARE(storedColor(1), QStringLiteral("#ffcc00"));
        QVERIFY(m_editor->error().isEmpty());
    }

    // PR #123 review (2): a lyrics edit that didn't save isn't lost to a style save, and its
    // error stays until a save fixes it.
    void aFailedLyricsSaveKeepsTheEditAndItsError()
    {
        m_editor->selectSlide(slideKey(1), false);
        QSignalSpy reset(m_editor.get(), &SongEditor::lyricsReset);
        {
            OtherProgram other(m_dir.filePath(QString::number(m_n)));
            QVERIFY(QSqlQuery(other.database()).exec("BEGIN IMMEDIATE"));
            m_editor->setSlideProperty("color", "#ffcc00");
            QVERIFY(!m_editor->flush());   // busy: waits to retry
            QVERIFY(QSqlQuery(other.database()).exec("ROLLBACK"));
        }
        const QString tooLong = m_editor->lyrics().replace("My chains are gone", QString(songlimits::kCharactersPerSlide + 1, 'a'));
        m_editor->applyLyrics(tooLong);
        QVERIFY2(m_editor->error().contains("characters"), qPrintable(m_editor->error()));
        QTest::qWait(3 * SongEditor::kSaveDelayMs);   // the style retry would have run
        m_editor->setSlideProperty("weight", 800);    // and another style edit
        QTest::qWait(3 * SongEditor::kSaveDelayMs);
        QVERIFY2(m_editor->error().contains("characters"), qPrintable(m_editor->error()));
        QCOMPARE(reset.count(), 0);
        QCOMPARE(m_editor->lyrics(), lyricsText(applyLyricsText(*m_songs->loadSong(QUuid::fromString(m_editor->songId())), tooLong).song));
        QCOMPARE(m_editor->slideStyle()["color"].toString(), QStringLiteral("#ffcc00"));

        // A save that works takes everything, and the error goes.
        m_editor->applyLyrics(m_editor->lyrics().replace(QString(songlimits::kCharactersPerSlide + 1, 'a'), "My chains are GONE"));
        QVERIFY2(m_editor->error().isEmpty(), qPrintable(m_editor->error()));
        const Song saved = *m_songs->loadSong(QUuid::fromString(m_editor->songId()));
        QCOMPARE(saved.groups[2].slides[0].left(18), QStringLiteral("My chains are GONE"));
        QCOMPARE(storedColor(1), QStringLiteral("#ffcc00"));
    }

    // PR #123 review: a style the library can't show is pointed out, and kept.
    void anOversizedStoredStyleIsReportedAndKept()
    {
        const Song song = *m_songs->loadSong(QUuid::fromString(m_editor->songId()));
        QSqlQuery q(m_songs->library()->database());
        q.prepare("UPDATE song_group SET slide_overrides = ? WHERE id = ?");
        q.addBindValue(QStringLiteral("[{\"later\":\"%1\"}]").arg(QString(20 * 1024, 'x')));
        q.addBindValue(song.groups[1].id.toString(QUuid::WithoutBraces));
        QVERIFY(q.exec());
        m_editor->setSongId(m_editor->songId());
        QVERIFY2(m_editor->warnings().join(' ').contains("too much style"), qPrintable(m_editor->warnings().join(' ')));

        m_editor->selectSlide(slideKey(2), false);   // the chorus
        m_editor->setSlideProperty("color", "#ffcc00");
        QVERIFY(m_editor->flush());
        m_editor->setTitle("Renamed");
        QVERIFY(m_editor->error().isEmpty());
        QCOMPARE(m_songs->loadSong(song.id)->groups[1].slideOverride(0)["later"].toString().size(), 20 * 1024);
    }

    // PR #123 review: a failed save on one song doesn't stop another's style edits saving.
    void aFailedSaveOnOneSongDoesntBlockTheNext()
    {
        const QString tooLong = m_editor->lyrics().replace("My chains are gone", QString(songlimits::kCharactersPerSlide + 1, 'a'));
        m_editor->applyLyrics(tooLong);
        QVERIFY(!m_editor->error().isEmpty());
        QVERIFY(m_songs->createFromPaste("Other", "Verse 1\nOne\n"));
        m_editor->setSongId(m_songs->selectedId());
        QVERIFY(m_editor->error().isEmpty());
        m_editor->selectSlide(slideKey(0), false);
        m_editor->setSlideProperty("color", "#ffcc00");
        QVERIFY(m_editor->flush());
        QCOMPARE(storedColor(0), QStringLiteral("#ffcc00"));
    }

    void lyricEditsKeepSlideStyles()
    {
        m_editor->selectSlide(slideKey(1), false);
        m_editor->setSlideProperty("color", "#ffcc00");
        QString text = m_editor->lyrics();
        text.replace("Amazing grace how sweet", "Amazing grace, how sweet");
        m_editor->applyLyrics(text);
        QCOMPARE(m_editor->selectedKeys(), QStringList{slideKey(1)});
        QCOMPARE(storedColor(1), QStringLiteral("#ffcc00"));
        QVERIFY(m_songs->loadSong(QUuid::fromString(m_editor->songId()))->groups[1].slides[0].startsWith("Amazing grace, how"));
    }

    // The controls in the window: select a tile, see its marker and the highlighted field, reset it.
    void theWindowStylesASlide()
    {
        m_engine->loadFromModule("Visualis.Ui", "Main");
        auto *main = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        QVERIFY(main);
        auto *editorWindow = main->findChild<QQuickWindow *>("songEditorWindow");
        QVERIFY(editorWindow);
        QMetaObject::invokeMethod(editorWindow, "edit", Q_ARG(QVariant, m_songs->selectedId()));
        QVERIFY(QTest::qWaitForWindowExposed(editorWindow));
        auto *windowEditor = editorWindow->findChild<SongEditor *>();
        auto *controls = findItem(editorWindow->contentItem(), "slideStyle");
        QVERIFY(controls);
        QVERIFY(!controls->isVisible());   // nothing selected

        // Looked up each time: the grid makes new tiles whenever the slides change.
        auto item = [&](QQuickItem *in, const char *name) { return in ? findItem(in, name) : nullptr; };
        auto tile = [&] { return item(editorWindow->contentItem(), "previewTile1"); };
        auto centre = [](QQuickItem *i) { return i->mapToScene(QPointF(i->width() / 2, i->height() / 2)).toPoint(); };
        QTRY_VERIFY(tile());
        QTest::mouseClick(editorWindow, Qt::LeftButton, {}, centre(tile()));
        QCOMPARE(windowEditor->selectedKeys().size(), 1);
        QTRY_VERIFY(controls->isVisible());

        windowEditor->setSlideProperty("color", "#ffcc00");
        QTRY_VERIFY(item(tile(), "overrideMarker") && item(tile(), "overrideMarker")->isVisible());
        QTRY_VERIFY(item(controls, "reset_color") && item(controls, "reset_color")->isVisible());
        QTest::mouseClick(editorWindow, Qt::LeftButton, {}, centre(item(controls, "reset_color")));
        QTRY_VERIFY(windowEditor->overriddenProperties().isEmpty());
        QTRY_VERIFY(item(tile(), "overrideMarker") && !item(tile(), "overrideMarker")->isVisible());

        // The transition control edits the slide's override, and its reset puts it back.
        QQuickItem *transition = item(controls, "slideTransition");
        QVERIFY(transition);
        const QVariant fadeKind = QVariantMap{{"kind", "fade"}};
        QMetaObject::invokeMethod(transition, "edited", Q_ARG(QVariant, fadeKind));
        QCOMPARE(windowEditor->slideTransition()["kind"].toString(), QStringLiteral("fade"));
        QTRY_VERIFY(item(transition, "resetTransition") && item(transition, "resetTransition")->isVisible());
        QTest::mouseClick(editorWindow, Qt::LeftButton, {}, centre(item(transition, "resetTransition")));
        QTRY_VERIFY(!windowEditor->slideTransitionOverridden());

        if (const QString dir = qEnvironmentVariable("VISUALIS_SNAPSHOT_DIR"); !dir.isEmpty()) {
            windowEditor->setSlideProperty("weight", 800);
            QTest::qWait(300);
            editorWindow->grabWindow().save(dir + "/song-editor-slide-style.png");
        }
    }

private:
    // In the visual tree: a view's delegates have no QObject parent to find them by.
    static QQuickItem *findItem(QQuickItem *item, const QString &name)
    {
        if (item->objectName() == name)
            return item;
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *found = findItem(child, name))
                return found;
        }
        return nullptr;
    }

    Template saveTemplate(const char *name)
    {
        Template t = builtInLyricsTemplate();
        t.id = QUuid();
        t.name = name;
        return m_templates->save(t).ok ? t : Template{};
    }
    QVariantMap previewSlide(int i) const { return m_editor->previewSlides().value(i).toMap(); }
    QString slideKey(int i) const { return previewSlide(i)["key"].toString(); }
    RegionStyle previewStyle(int i) const { return previewSlide(i)["content"].value<Content>().regions.value("main").style; }
    // The stored override of preview slide `i`.
    QJsonObject storedOverride(int i) const
    {
        const SongSlide slide = songSlides(*m_songs->loadSong(QUuid::fromString(m_editor->songId())),
                                           QUuid::fromString(m_editor->arrangementId()))[i];
        return m_songs->loadSong(QUuid::fromString(m_editor->songId()))->group(slide.groupId)->slideOverride(slide.indexInGroup);
    }

    QString storedColor(int i) const
    {
        return overrides::propertyValue(storedOverride(i).value("regions").toObject().value("main").toObject(), "color").toString();
    }

    QTemporaryDir m_dir;
    int m_n = 0;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<SongEditor> m_editor;
    SongLibrary *m_songs = nullptr;
    LiveState *m_live = nullptr;
    TemplateLibrary *m_templates = nullptr;
};

QTEST_MAIN(SongEditorTest)
#include "tst_songeditor.moc"
