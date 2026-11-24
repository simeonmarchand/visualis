#include "library/songlibrary.h"
#include "library/templateeditor.h"
#include "library/templatelibrary.h"
#include "live/livestate.h"
#include "template/templatejson.h"

#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include "testkeychain.h"

Q_IMPORT_QML_PLUGIN(Visualis_UiPlugin)

using namespace visualis;

namespace {

QString idText(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }

QStringList names(const QVariantList &templates)
{
    QStringList out;
    for (const QVariant &v : templates)
        out << v.toMap().value("name").toString();
    return out;
}

// JSON written with single quotes, so it reads in a C++ string.
QJsonObject json(const char *text) { return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object(); }
QVariantMap map(const char *text) { return json(text).toVariantMap(); }

constexpr int kAllEdges = Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge;

} // namespace

class TemplateEditorTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { useTestKeychainEntry(); }

    void init()
    {
        QVERIFY(m_dir.isValid());
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_songs = m_engine->singletonInstance<SongLibrary *>("Visualis.Ui", "SongLibrary");
        m_templates = m_engine->singletonInstance<TemplateLibrary *>("Visualis.Ui", "TemplateLibrary");
        m_live = m_engine->singletonInstance<LiveState *>("Visualis.Ui", "LiveState");
        QVERIFY(m_songs->openLibrary(m_dir.filePath(QString::number(++m_n))));
        QQmlComponent component(m_engine.get());
        component.setData("import Visualis.Ui\nTemplateEditor {}", {});
        m_editor.reset(qobject_cast<TemplateEditor *>(component.create()));
        QVERIFY2(m_editor, qPrintable(component.errorString()));
    }

    void cleanup()
    {
        m_editor.reset();
        m_engine.reset();
    }

    void startsOnTheBuiltInWhichCanOnlyBeDuplicated()
    {
        QCOMPARE(names(m_editor->templates()), QStringList{"Lyrics"});
        QCOMPARE(m_editor->templateId(), idText(builtInLyricsTemplateId()));
        QVERIFY(m_editor->builtIn());
        QCOMPARE(m_editor->regionId(), QStringLiteral("main"));

        m_editor->setStyle(map("{'color': '#ff0000'}"));
        QVERIFY(!m_editor->error().isEmpty());
        QCOMPARE(m_editor->style().value("color").toString(), QStringLiteral("#ffffff"));
        m_editor->rename("Mine");
        QVERIFY(!m_editor->remove());
        m_editor->flush();
        QCOMPARE(m_templates->templates().size(), 1);
        QCOMPARE(stored(), builtInLyricsTemplate());
    }

    void newTemplatesStartFromTheBuiltInAndAreListedByName()
    {
        const QString zion = m_editor->create("Zion");
        const QString advent = m_editor->create("advent");
        QCOMPARE(m_editor->templateId(), advent);
        QVERIFY(!m_editor->builtIn());
        QCOMPARE(names(m_editor->templates()), (QStringList{"Lyrics", "advent", "Zion"}));
        Template t = m_templates->templates().value(QUuid::fromString(zion));
        QCOMPARE(t.name, QStringLiteral("Zion"));
        QCOMPARE(t.regions.size(), 1);
        QCOMPARE(t.regions[0].style, builtInLyricsTemplate().regions[0].style);
        QVERIFY(m_editor->create("   ").isEmpty() == false);   // an empty name gets one
        QVERIFY(names(m_editor->templates()).contains("New template"));
    }

    void duplicateRenameAndDelete()
    {
        const QString copy = m_editor->duplicate();   // of the built-in
        QCOMPARE(m_editor->templateId(), copy);
        QCOMPARE(m_editor->name(), QStringLiteral("Lyrics copy"));
        m_editor->setStyle(map("{'fontFamily': 'Georgia'}"));
        const QString second = m_editor->duplicate();
        QVERIFY(second != copy);
        QCOMPARE(storedStyle().fontFamily, QStringLiteral("Georgia"));   // the pending edit came along

        m_editor->rename("  Christmas  ");
        QCOMPARE(stored().name, QStringLiteral("Christmas"));
        m_editor->rename("");   // refused
        QCOMPARE(stored().name, QStringLiteral("Christmas"));

        QVERIFY(m_editor->remove());
        QVERIFY(!m_templates->templates().contains(QUuid::fromString(second)));
        QCOMPARE(m_editor->templateId(), idText(builtInLyricsTemplateId()));
        QCOMPARE(names(m_editor->templates()), (QStringList{"Lyrics", "Lyrics copy"}));
    }

    void eachPropertyAppliesAndPersists_data()
    {
        QTest::addColumn<QByteArray>("change");
        QTest::newRow("fontFamily") << QByteArray("{'fontFamily': 'Georgia'}");
        QTest::newRow("weight") << QByteArray("{'weight': 800}");
        QTest::newRow("maxSize") << QByteArray("{'maxSize': 0.1}");
        QTest::newRow("minSize") << QByteArray("{'minSize': 0.02}");
        QTest::newRow("color") << QByteArray("{'color': '#ffcc00'}");
        QTest::newRow("align") << QByteArray("{'align': 'left'}");
        QTest::newRow("verticalAlign") << QByteArray("{'verticalAlign': 'bottom'}");
        QTest::newRow("lineHeight") << QByteArray("{'lineHeight': 1.4}");
        QTest::newRow("shadow") << QByteArray("{'shadow': {'enabled': false, 'color': '#102030', 'opacity': 0.5, 'blur': 0.2, 'offset': 0.01}}");
        QTest::newRow("outline") << QByteArray("{'outline': {'color': '#ff0000', 'width': 0.004}}");
        QTest::newRow("frame") << QByteArray("{'frame': {'x': 0.1, 'y': 0.6, 'w': 0.8, 'h': 0.3}}");
    }

    void eachPropertyAppliesAndPersists()
    {
        QFETCH(QByteArray, change);
        m_editor->create("Ours");
        const QJsonObject wanted = json(change.constData());
        m_editor->setStyle(wanted.toVariantMap());
        QVERIFY2(m_editor->warnings().isEmpty(), qPrintable(m_editor->warnings().join('\n')));
        const QString key = wanted.begin().key();
        // At once in the editor; in the library once saved.
        QCOMPARE(QJsonValue::fromVariant(m_editor->style().value(key)), wanted.value(key));
        QCOMPARE(regionStyleToJson(m_editor->preview().regions.value("main").style).value(key), wanted.value(key));
        QTRY_COMPARE(regionStyleToJson(storedStyle()).value(key), wanted.value(key));
        // And from a fresh read of the library, not just the snapshot.
        const QString id = m_editor->templateId();
        QVERIFY(m_songs->openLibrary(m_dir.filePath(QString::number(m_n))));
        QCOMPARE(regionStyleToJson(m_templates->templates().value(QUuid::fromString(id)).regions[0].style).value(key),
                 wanted.value(key));
    }

    // A nested property changes only the keys it names.
    void aShadowChangeKeepsTheRestOfTheShadow()
    {
        m_editor->create("Ours");
        m_editor->setStyle(map("{'shadow': {'opacity': 0.3}}"));
        m_editor->flush();
        QCOMPARE(storedStyle().shadow.opacity, 0.3);
        QCOMPARE(storedStyle().shadow.blur, builtInLyricsTemplate().regions[0].style.shadow.blur);
        QVERIFY(storedStyle().shadow.enabled);
    }

    void anInvalidValueIsRefusedAndReported()
    {
        m_editor->create("Ours");
        m_editor->setStyle(map("{'color': 'red', 'weight': 700}"));
        QCOMPARE(m_editor->warnings().size(), 1);
        QVERIFY(m_editor->warnings().first().contains("color"));
        QCOMPARE(m_editor->style().value("weight").toInt(), 600);   // nothing of a refused change applies
        m_editor->setStyle(map("{'minSize': 0.2}"));   // above maxSize
        QVERIFY(m_editor->warnings().first().contains("minSize"));
        m_editor->setStyle(map("{'frame': {'x': 0.5, 'w': 0.8}}"));   // off the output
        QVERIFY(m_editor->warnings().first().contains("frame"));
        m_editor->flush();
        QCOMPARE(storedStyle(), builtInLyricsTemplate().regions[0].style);
        m_editor->setStyle(map("{'weight': 700}"));   // a good change clears them
        QVERIFY(m_editor->warnings().isEmpty());
    }

    void theSafeAreaPersists()
    {
        m_editor->create("Ours");
        m_editor->setSafeArea(0.1, 0.05);
        QCOMPARE(m_editor->safeAreaX(), 0.1);
        m_editor->flush();
        QCOMPARE(stored().safeAreaX, 0.1);
        QCOMPARE(stored().safeAreaY, 0.05);
        m_editor->setSafeArea(0.5, 0.05);   // past the middle: refused
        QVERIFY(!m_editor->warnings().isEmpty());
        QCOMPARE(m_editor->safeAreaX(), 0.1);
    }

    void draggingTheFrameUpdatesFractionsClampedToTheScreen()
    {
        m_editor->create("Ours");
        const QRectF start = builtInLyricsTemplate().regions[0].style.frame;   // 0.06, 0.08, 0.88, 0.84
        m_editor->dragFrame(start, int(Qt::RightEdge | Qt::BottomEdge), -0.44, -0.42);
        QVERIFY(qAbs(m_editor->style()["frame"].toMap()["w"].toDouble() - 0.44) < 1e-9);
        m_editor->dragFrame(start, int(Qt::RightEdge | Qt::BottomEdge), -0.44, -0.42);   // the same drag, not added up
        const QRectF half(0.06, 0.08, 0.44, 0.42);
        m_editor->dragFrame(half, kAllEdges, 2, 2);   // far past the corner
        m_editor->flush();
        const QRectF f = storedStyle().frame;
        QVERIFY2(qAbs(f.right() - 1) < 1e-9 && qAbs(f.bottom() - 1) < 1e-9 && qAbs(f.width() - 0.44) < 1e-9,
                 qPrintable(QStringLiteral("%1 %2 %3 %4").arg(f.x()).arg(f.y()).arg(f.width()).arg(f.height())));
    }

    // A slider or a drag changes the style many times a second; the library isn't written each time.
    void quickEditsSaveTogether()
    {
        m_editor->create("Ours");
        QSignalSpy saved(m_templates, &TemplateLibrary::templatesChanged);
        for (int i = 1; i <= 30; ++i)
            m_editor->setStyle({{"lineHeight", 1 + i / 100.0}});
        QCOMPARE(saved.count(), 0);
        QTRY_COMPARE(storedStyle().lineHeight, 1.3);
        QCOMPARE(saved.count(), 1);
    }

    void thePreviewShowsTheSampleTextAtOnce()
    {
        m_editor->create("Ours");
        const Content before = m_editor->preview();
        QCOMPARE(before.regions.value("main").text, m_editor->sampleText());
        QVERIFY(!m_editor->sampleText().isEmpty());
        m_editor->setSampleText("A long line that should shrink to fit the frame of the lyrics region");
        m_editor->setStyle(map("{'color': '#00ff00'}"));
        const ContentRegion main = m_editor->preview().regions.value("main");
        QCOMPARE(main.text, QStringLiteral("A long line that should shrink to fit the frame of the lyrics region"));
        QCOMPARE(main.style.color, QStringLiteral("#00ff00"));   // before it's saved
    }

    void editsReachWhatsLive()
    {
        const QString id = m_editor->create("Ours");
        QVERIFY(m_songs->createFromPaste("Grace", "Verse 1\nAmazing grace\n"));
        Song song = *m_songs->loadSong(QUuid::fromString(m_songs->selectedId()));
        song.defaultTemplate = QUuid::fromString(id);
        QVERIFY(m_songs->saveSong(song, nullptr));
        QVERIFY(m_songs->goLive(0));
        m_editor->setStyle(map("{'color': '#ffcc00', 'frame': {'y': 0.6, 'h': 0.3}}"));
        QTRY_COMPARE(m_live->content().regions.value("main").style.color, QStringLiteral("#ffcc00"));
        QCOMPARE(m_live->content().regions.value("main").style.frame.y(), 0.6);
    }

    // Another library has none of this one's templates. An edit not yet saved when it
    // switched is dropped, never written into the other library.
    void aLibrarySwitchGoesBackToTheBuiltIn()
    {
        m_editor->create("Ours");
        m_editor->setStyle(map("{'color': '#ffcc00'}"));
        QVERIFY(m_songs->openLibrary(m_dir.filePath("other")));
        QCOMPARE(m_editor->templateId(), idText(builtInLyricsTemplateId()));
        QCOMPARE(names(m_editor->templates()), QStringList{"Lyrics"});
        QTest::qWait(300);
        m_editor->flush();
        QCOMPARE(m_templates->templates().size(), 1);
    }

    // Deleted elsewhere (another editor window, a later import): the editor lets it go.
    void aTemplateRemovedElsewhereIsLetGo()
    {
        const QString id = m_editor->create("Ours");
        m_editor->setStyle(map("{'color': '#ffcc00'}"));
        QVERIFY(m_templates->remove(QUuid::fromString(id)));
        QCOMPARE(m_editor->templateId(), idText(builtInLyricsTemplateId()));
        m_editor->flush();
        QVERIFY(!m_templates->templates().contains(QUuid::fromString(id)));
    }

    // Saved elsewhere while an edit here is pending: the edit isn't lost.
    void anotherTemplateSavedMeanwhileKeepsThePendingEdit()
    {
        m_editor->create("Ours");
        m_editor->setStyle(map("{'color': '#ffcc00'}"));
        Template other = builtInLyricsTemplate();
        other.id = QUuid();
        other.name = "Other";
        QVERIFY(m_templates->save(other));
        QCOMPARE(m_editor->style().value("color").toString(), QStringLiteral("#ffcc00"));
        m_editor->flush();
        QCOMPARE(storedStyle().color, QStringLiteral("#ffcc00"));
    }

    void theWindowLoadsAndAFrameDragMovesTheRegion()
    {
        m_engine->loadFromModule("Visualis.Ui", "Main");
        auto *main = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        QVERIFY(main);
        auto *window = main->findChild<QQuickWindow *>("templateEditorWindow");
        QVERIFY(window);
        QMetaObject::invokeMethod(window, "open");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *editor = window->findChild<TemplateEditor *>();
        QVERIFY(editor);
        editor->create("Ours");
        auto *frame = window->findChild<QQuickItem *>("regionFrame");
        QVERIFY(frame);
        QTRY_VERIFY(frame->width() > 0);
        const QPointF centre = frame->mapToScene(QPointF(frame->width() / 2, frame->height() / 2));
        auto *stage = window->findChild<QQuickItem *>("templatePreview");
        QVERIFY(stage);
        const QPoint from = centre.toPoint();
        const QPoint to = from + QPoint(int(stage->width() / 4), 0);
        QTest::mousePress(window, Qt::LeftButton, {}, from);
        for (int i = 1; i <= 10; ++i)
            QTest::mouseMove(window, from + (to - from) * i / 10);
        QTest::mouseRelease(window, Qt::LeftButton, {}, to);
        // A quarter of the width to the right, stopped at the edge (the frame is 0.88 wide).
        QTRY_VERIFY(editor->style()["frame"].toMap()["x"].toDouble() > 0.1);
        editor->flush();
        const QRectF f = m_templates->templates().value(QUuid::fromString(editor->templateId())).regions[0].style.frame;
        QVERIFY(qAbs(f.right() - 1) < 1e-6);
        QCOMPARE(f.y(), builtInLyricsTemplate().regions[0].style.frame.y());   // a level drag

        if (const QString dir = qEnvironmentVariable("VISUALIS_SNAPSHOT_DIR"); !dir.isEmpty()) {
            QTest::qWait(300);
            window->grabWindow().save(dir + "/template-editor.png");
        }
    }

private:
    // The stored copy of the template being edited.
    Template stored() const { return m_templates->templates().value(QUuid::fromString(m_editor->templateId())); }
    RegionStyle storedStyle() const { return stored().regions.value(0).style; }

    QTemporaryDir m_dir;
    int m_n = 0;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<TemplateEditor> m_editor;
    SongLibrary *m_songs = nullptr;
    TemplateLibrary *m_templates = nullptr;
    LiveState *m_live = nullptr;
};

QTEST_MAIN(TemplateEditorTest)
#include "tst_templateeditor.moc"
