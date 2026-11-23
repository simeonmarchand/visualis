#include "live/livestate.h"
#include "output/latencyprobe.h"

#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include "testkeychain.h"

#include <memory>

Q_IMPORT_QML_PLUGIN(Visualis_UiPlugin)

using namespace visualis;

namespace {

// A song taken live from the library, through the built-in template.
LiveItem libraryItem(const Song &song, const QUuid &arrangement = {})
{
    return liveSong(song, arrangement, templateSet({}));
}

// Joy to the World (public domain): a blank intro, a two-slide verse, and a chorus.
Song sample()
{
    Song s;
    s.title = "Joy to the World";
    s.groups = {
        Group{QUuid::createUuid(), "Intro", "#b3a624", {""}},
        Group{QUuid::createUuid(), "Verse 1", "#0078cc", {"Joy to the world\nThe Lord is come", "Let earth receive her King"}},
        Group{QUuid::createUuid(), "Chorus", "#cc004f", {"And heaven and nature sing"}},
    };
    const auto &g = s.groups;
    s.arrangements = {Arrangement{QUuid::createUuid(), "Default", {g[0].id, g[1].id, g[2].id}, {}}};
    return s;
}

Song oneSlide(const QString &text)
{
    Song s;
    s.title = "Test";
    s.groups = {Group{QUuid::createUuid(), "Verse", "#0078cc", {text}}};
    s.arrangements = {Arrangement{QUuid::createUuid(), "Default", {s.groups[0].id}, {}}};
    return s;
}

int pixelSize(const QObject *text)
{
    return text->property("fontInfo").value<QJSValue>().property("pixelSize").toInt();
}

} // namespace

// Renders real output windows offscreen at 1920x1080 and checks what they show. Set
// VISUALIS_SNAPSHOT_DIR to save a PNG of each check for a look by eye.
class OutputsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { useTestKeychainEntry(); }

    void init()
    {
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_live = m_engine->singletonInstance<LiveState *>("Visualis.Ui", "LiveState");
        QVERIFY(m_live);
    }

    void cleanup()
    {
        m_windows.clear();
        m_engine.reset();
    }

    void audienceShowsTheCurrentSlide()
    {
        QQuickWindow *w = output("audience");
        m_live->show(libraryItem(sample()), 1);
        QTRY_COMPARE(shownLyrics(w), QStringLiteral("Joy to the world\nThe Lord is come"));
        m_live->next();
        QTRY_COMPARE(shownLyrics(w), QStringLiteral("Let earth receive her King"));
        snapshot(w, "audience");
    }

    // The checks above read the Text items; this one checks the pixels, since an effect
    // that fails to render leaves the text "there" but invisible. MultiEffect draws
    // nothing under the offscreen platform, so run it with QT_QPA_PLATFORM=cocoa.
    void lyricsReachTheScreen()
    {
        if (QGuiApplication::platformName() == "offscreen")
            QSKIP("MultiEffect doesn't render offscreen; run with QT_QPA_PLATFORM=cocoa");
        QQuickWindow *w = output("audience");
        m_live->show(libraryItem(oneSlide("Joy to the world")), 0);
        QTRY_COMPARE(shownLyrics(w), QStringLiteral("Joy to the world"));
        QTest::qWait(LyricsFadeWaitMs);
        const QImage image = w->grabWindow();
        int bright = 0;
        for (int y = 0; y < image.height(); y += 4) {
            for (int x = 0; x < image.width(); x += 4)
                bright += qGray(image.pixel(x, y)) > 200;
        }
        QVERIFY2(bright > 100, qPrintable(QStringLiteral("%1 bright pixels").arg(bright)));
    }

    void slidesCutByDefault()
    {
        QQuickWindow *w = output("audience");
        m_live->show(libraryItem(sample()), 1);
        QTRY_COMPARE(shownLyrics(w), QStringLiteral("Joy to the world\nThe Lord is come"));
        m_live->next();
        // No waiting: the new slide is fully shown and the old one gone at once.
        QCOMPARE(visibleLyrics(w)->opacity(), 1.0);
        QCOMPARE(hiddenLyrics(w)->opacity(), 0.0);
    }

    void theFadeTransitionCrossfades()
    {
        auto *tmpl = m_engine->singletonInstance<QObject *>("Visualis.Ui", "LyricsTemplate");
        QVERIFY(tmpl);
        tmpl->setProperty("transition", "fade");
        QQuickWindow *w = output("audience");
        m_live->show(libraryItem(sample()), 1);
        QTRY_COMPARE(visibleLyrics(w)->opacity(), 1.0);
        m_live->next();
        QTest::qWait(50);
        QVERIFY(visibleLyrics(w)->opacity() < 1.0);   // still fading in
        QTRY_COMPARE(visibleLyrics(w)->opacity(), 1.0);
        QTRY_COMPARE(hiddenLyrics(w)->opacity(), 0.0);
        tmpl->setProperty("transition", "cut");
    }

    void clearedTextLeavesTheAudienceAndStaysOnStage()
    {
        QQuickWindow *audience = output("audience");
        QQuickWindow *stage = output("stage");
        m_live->show(libraryItem(sample()), 1);
        m_live->clearText();
        QTRY_COMPARE(shownLyrics(audience), QString());
        QCOMPARE(stage->findChild<QObject *>("stageCurrent")->property("text").toString(),
                 QStringLiteral("Joy to the world\nThe Lord is come"));
        QVERIFY(stage->findChild<QQuickItem *>("stageCleared")->isVisible());
        snapshot(stage, "stage-cleared");
    }

    void longSlidesShrinkToFit()
    {
        QQuickWindow *w = output("audience");
        m_live->show(libraryItem(oneSlide("short")), 0);
        QTRY_VERIFY(visibleLyrics(w));
        const int full = pixelSize(visibleLyrics(w));

        m_live->show(libraryItem(oneSlide(QStringList(8, "A line of a long verse that keeps going").join('\n'))), 0);
        QTRY_VERIFY(visibleLyrics(w) && visibleLyrics(w)->property("text").toString().startsWith("A line"));
        QQuickItem *text = visibleLyrics(w);
        QVERIFY2(pixelSize(text) < full, qPrintable(QString::number(pixelSize(text))));
        QVERIFY(text->property("contentHeight").toReal() <= text->height());
        snapshot(w, "audience-long");
    }

    void textNeverOverflowsTheSafeArea()
    {
        QQuickWindow *w = output("audience");
        m_live->show(libraryItem(oneSlide(QStringList(60, "far too many words for one slide").join('\n'))), 0);
        QTRY_VERIFY(visibleLyrics(w) && visibleLyrics(w)->property("text").toString().startsWith("far"));
        QQuickItem *text = visibleLyrics(w);
        QVERIFY(text->property("contentHeight").toReal() <= text->height());
        QVERIFY(text->property("truncated").toBool());   // elided at the smallest size
        // Inside the safe area, not just inside the window.
        const QRectF box = text->mapRectToScene(QRectF(0, 0, text->width(), text->height()));
        QVERIFY(box.top() >= 1080 * 0.08 - 1 && box.bottom() <= 1080 * 0.92 + 1);
        QVERIFY(box.left() >= 1920 * 0.06 - 1 && box.right() <= 1920 * 0.94 + 1);
    }

    void stageShowsGroupPositionNextAndClock()
    {
        QQuickWindow *stage = output("stage");
        m_live->show(libraryItem(sample()), 1);
        QTRY_COMPARE(text(stage, "stageNext"), QStringLiteral("Let earth receive her King"));
        QCOMPARE(text(stage, "stageTitle"), QStringLiteral("Joy to the World   2 / 4"));
        QCOMPARE(stage->findChild<QQuickItem *>("stageGroup")->property("color").value<QColor>(), QColor("#0078cc"));
        QVERIFY(!text(stage, "stageClock").isEmpty());
        snapshot(stage, "stage");

        m_live->goToSlide(3);
        QTRY_COMPARE(text(stage, "stageNext"), QStringLiteral("(end of song)"));
    }

    void stageSaysNextIsBlankNotTheEnd()
    {
        // Verse, then the blank intro: the next slide is "" but the song isn't over.
        Song s = sample();
        s.arrangements[0].sequence = {s.groups[1].id, s.groups[0].id, s.groups[2].id};
        QQuickWindow *stage = output("stage");
        m_live->show(libraryItem(s), 1);
        QTRY_COMPARE(text(stage, "stageNext"), QString());
    }

    void clearingTheBackgroundFadesTheVideo()
    {
        QQuickWindow *w = output("audience");
        auto *video = w->findChild<QQuickItem *>("background");
        QCOMPARE(video->opacity(), 1.0);
        m_live->clearBackground();
        QTRY_COMPARE(video->opacity(), 0.0);
    }

    // #24 acceptance: a command reaches the output's next frame in under 50 ms.
    void commandsReachTheOutputQuickly()
    {
        QQuickWindow *w = output("audience");
        m_live->show(libraryItem(sample()), 1);
        QTest::qWait(200);
        LatencyProbe probe(w, "audience");
        QSignalSpy measured(&probe, &LatencyProbe::measured);
        QList<double> times;
        for (int i = 0; i < 5; ++i) {
            probe.arm("next_slide");
            i % 2 ? m_live->previous() : m_live->next();
            QVERIFY(measured.wait(1000));
            times << measured.takeFirst().at(2).toDouble();
            QTest::qWait(50);
        }
        qInfo() << "command -> frame, ms:" << times;
        for (double ms : times)
            QVERIFY2(ms < 50, qPrintable(QString::number(ms)));
    }

    // Plan 5.2: an output owns no state, so a reopened window shows what's live.
    void aNewOutputWindowShowsWhatIsLive()
    {
        m_live->show(libraryItem(sample()), 2);
        QQuickWindow *w = output("audience");
        QTRY_COMPARE(shownLyrics(w), QStringLiteral("Let earth receive her King"));
    }

private:
    QQuickWindow *output(const QString &role)
    {
        QQmlComponent component(m_engine.get(), "Visualis.Ui", "OutputWindow");
        std::unique_ptr<QObject> obj(component.createWithInitialProperties({{"role", role}}));
        auto *w = qobject_cast<QQuickWindow *>(obj.get());
        if (!w)
            qFatal("%s", qPrintable(component.errorString()));
        w->setGeometry(0, 0, 1920, 1080);
        w->show();
        if (!QTest::qWaitForWindowExposed(w))
            qFatal("output window not exposed");
        m_windows.push_back(std::move(obj));
        return w;
    }

    // The lyric Text that's fading in or shown (the other one is fading out).
    static QQuickItem *visibleLyrics(QQuickWindow *w)
    {
        auto *layer = w->findChild<QObject *>("audienceLyrics");
        if (!layer)
            return nullptr;
        return w->findChild<QQuickItem *>(layer->property("showingA").toBool() ? "lyricsA" : "lyricsB");
    }

    static QQuickItem *hiddenLyrics(QQuickWindow *w)
    {
        auto *layer = w->findChild<QObject *>("audienceLyrics");
        return w->findChild<QQuickItem *>(layer->property("showingA").toBool() ? "lyricsB" : "lyricsA");
    }

    static QString shownLyrics(QQuickWindow *w)
    {
        QQuickItem *t = visibleLyrics(w);
        return t ? t->property("text").toString() : QString();
    }

    static QString text(QQuickWindow *w, const char *name)
    {
        auto *o = w->findChild<QObject *>(name);
        return o ? o->property("text").toString() : QStringLiteral("<missing %1>").arg(name);
    }

    static void snapshot(QQuickWindow *w, const QString &name)
    {
        const QString dir = qEnvironmentVariable("VISUALIS_SNAPSHOT_DIR");
        if (dir.isEmpty())
            return;
        QTest::qWait(LyricsFadeWaitMs);
        w->grabWindow().save(dir + "/" + name + ".png");
    }

    static constexpr int LyricsFadeWaitMs = 700;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    LiveState *m_live = nullptr;
    std::vector<std::unique_ptr<QObject>> m_windows;
};

QTEST_MAIN(OutputsTest)
#include "tst_outputs.moc"
