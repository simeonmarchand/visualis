#include "library/songeditor.h"
#include "library/songlibrary.h"
#include "live/livestate.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Visualis_UiPlugin)

using namespace visualis;

namespace {

const char *kLyrics =
    "Intro\n\nVerse 1\nAmazing grace how sweet the sound\nThat saved a wretch like me\n\n"
    "Chorus\nMy chains are gone\nI've been set free\n\nVerse 1\n\nChorus\n";   // V1 C V1 C

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
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_songs = m_engine->singletonInstance<SongLibrary *>("Visualis.Ui", "SongLibrary");
        m_live = m_engine->singletonInstance<LiveState *>("Visualis.Ui", "LiveState");
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

    void removingAGroupRemovesItsSteps()
    {
        m_editor->removeGroup(strings(m_editor->groups(), "id")[0]);
        QCOMPARE(strings(m_editor->sequence(), "label"), (QStringList{"Verse 1", "Chorus", "Verse 1", "Chorus"}));
        QVERIFY(!m_editor->lyrics().contains("[Intro]"));
    }

    void stepsMoveAddAndRemove()
    {
        m_editor->moveStep(4, -1);   // ... V1 C V1 C -> ... V1 C C V1
        QCOMPARE(strings(m_editor->sequence(), "label"), (QStringList{"Intro", "Verse 1", "Chorus", "Chorus", "Verse 1"}));
        m_editor->removeStep(0);
        m_editor->addStep(strings(m_editor->groups(), "id")[2]);
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
        m_editor->addStep(strings(m_editor->groups(), "id")[2]);   // just the chorus
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

private:
    QTemporaryDir m_dir;
    int m_n = 0;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<SongEditor> m_editor;
    SongLibrary *m_songs = nullptr;
    LiveState *m_live = nullptr;
};

QTEST_MAIN(SongEditorTest)
#include "tst_songeditor.moc"
