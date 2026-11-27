#include "live/livestate.h"
#include "output/latencyprobe.h"
#include "background/backgroundplayer.h"

#include <QElapsedTimer>
#include <QFont>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include "testkeychain.h"

#include <memory>
#include <mutex>

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

// `song` through a template of its own, made with `edit` from the built-in.
template <typename Edit>
LiveItem throughTemplate(Song song, Edit edit)
{
    Template t = builtInLyricsTemplate();
    t.id = QUuid::createUuid();
    t.name = "Test";
    edit(t);
    song.defaultTemplate = t.id;
    return liveSong(song, {}, templateSet({t}));
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
        // Media is checked against the files made here, not a library.
        m_live->setMediaCheck([this](const QUuid &id) {
            MediaCheck c;
            c.status = m_media.contains(id) ? MediaStatus::Ok : MediaStatus::UnknownId;
            c.kind = m_media.value(id).first;
            c.path = m_media.value(id).second;
            return c;
        });
    }

    void cleanup()
    {
        m_windows.clear();
        m_engine.reset();
        m_media.clear();
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
        QSignalSpy aChanged(w->findChild<QObject *>("lyricsA"), SIGNAL(opacityChanged()));
        QSignalSpy bChanged(w->findChild<QObject *>("lyricsB"), SIGNAL(opacityChanged()));
        m_live->next();
        // No waiting: the new slide is fully shown and the old one gone at once.
        QCOMPARE(visibleLyrics(w)->opacity(), 1.0);
        QCOMPARE(hiddenLyrics(w)->opacity(), 0.0);
        // #96: a cut has no animation frames: each Text's opacity changed once, straight to its end.
        QVERIFY(!fade(w)->property("running").toBool());
        QTest::qWait(100);
        QCOMPARE(aChanged.count(), 1);
        QCOMPARE(bChanged.count(), 1);
    }

    // #93: the audience text renders from the live slide's template: font, size, alignment
    // and place.
    void theTextRendersFromTheTemplate()
    {
        QQuickWindow *w = output("audience");
        QCOMPARE(w->size(), QSize(1920, 1080));
        m_live->show(throughTemplate(oneSlide("short"), [](Template &t) {
            RegionStyle &s = t.regions[0].style;
            s.fontFamily = "Courier New";
            s.maxSize = 0.05;
            s.weight = 700;
            s.hAlign = HAlign::Left;
            s.vAlign = VAlign::Bottom;
            s.frame = QRectF(0.1, 0.6, 0.8, 0.3);
        }), 0);
        QTRY_VERIFY(visibleLyrics(w) && visibleLyrics(w)->property("text").toString() == "short");
        QQuickItem *text = visibleLyrics(w);
        const QFont font = text->property("font").value<QFont>();
        QCOMPARE(font.family(), QStringLiteral("Courier New"));
        QCOMPARE(font.weight(), QFont::Bold);
        QCOMPARE(pixelSize(text), 54);   // 5% of 1080
        QCOMPARE(text->property("horizontalAlignment").toInt(), int(Qt::AlignLeft));
        QCOMPARE(text->property("verticalAlignment").toInt(), int(Qt::AlignBottom));
        QCOMPARE(text->mapRectToScene(QRectF(0, 0, text->width(), text->height())), QRectF(192, 648, 1536, 324));
        snapshot(w, "audience-template");
    }

    void theFadeTransitionCrossfades()
    {
        QQuickWindow *w = output("audience");
        m_live->show(throughTemplate(sample(), [](Template &t) { t.transitions.content = {TransitionKind::Fade, 300}; }), 1);
        QTRY_COMPARE(visibleLyrics(w)->opacity(), 1.0);
        m_live->next();
        QTest::qWait(50);
        QVERIFY(visibleLyrics(w)->opacity() < 1.0);   // still fading in
        QTRY_COMPARE(visibleLyrics(w)->opacity(), 1.0);
        QTRY_COMPARE(hiddenLyrics(w)->opacity(), 0.0);
    }

    // #96: Next mid-fade ends on the newest slide. The slide that was fading out goes at
    // once, so no more than two slides ever show and none changes its text while visible.
    void aFadeInterruptedByNextEndsOnTheNewSlide()
    {
        QQuickWindow *w = output("audience");
        m_live->show(throughTemplate(sample(), [](Template &t) { t.transitions.content = {TransitionKind::Fade, kLongFadeMs}; }), 1);
        QTRY_COMPARE_WITH_TIMEOUT(visibleLyrics(w)->opacity(), 1.0, 3 * kLongFadeMs);
        m_live->next();
        QTest::qWait(150);
        QQuickItem *second = visibleLyrics(w);
        const double secondAt = second->opacity();
        QVERIFY(secondAt > 0 && secondAt < 1);
        m_live->next();
        QQuickItem *third = visibleLyrics(w);
        QCOMPARE(third->property("text").toString(), QStringLiteral("And heaven and nature sing"));
        QCOMPARE(third->opacity(), 0.0);   // fades in from nothing, not from where the first slide was
        QCOMPARE(second->opacity(), secondAt);   // fades out from where it was
        QTRY_COMPARE_WITH_TIMEOUT(third->opacity(), 1.0, 3 * kLongFadeMs);
        QTRY_COMPARE_WITH_TIMEOUT(second->opacity(), 0.0, 3 * kLongFadeMs);
        QCOMPARE(shownLyrics(w), QStringLiteral("And heaven and nature sing"));
    }

    void clearingDuringAFadeEndsCleared()
    {
        QQuickWindow *w = output("audience");
        m_live->show(throughTemplate(sample(), [](Template &t) { t.transitions.content = {TransitionKind::Fade, kLongFadeMs}; }), 1);
        QTRY_COMPARE_WITH_TIMEOUT(visibleLyrics(w)->opacity(), 1.0, 3 * kLongFadeMs);
        m_live->next();
        QTest::qWait(150);
        m_live->clearText();
        QTRY_VERIFY_WITH_TIMEOUT(!fade(w)->property("running").toBool(), 3 * kLongFadeMs);
        QCOMPARE(shownLyrics(w), QString());
        QCOMPARE(hiddenLyrics(w)->opacity(), 0.0);
    }

    // Clearing uses the layer's transition: the live slide's.
    void clearingTheTextFadesWithTheSlidesTransition()
    {
        QQuickWindow *w = output("audience");
        m_live->show(throughTemplate(sample(), [](Template &t) { t.transitions.content = {TransitionKind::Fade, kLongFadeMs}; }), 1);
        QTRY_COMPARE_WITH_TIMEOUT(visibleLyrics(w)->opacity(), 1.0, 3 * kLongFadeMs);
        QQuickItem *shown = visibleLyrics(w);
        m_live->clearText();
        // Fading out, not cut: seen partway within the fade. Polled, since a slow runner may
        // not have run a frame of it yet after any fixed wait.
        QTRY_VERIFY_WITH_TIMEOUT(shown->opacity() > 0 && shown->opacity() < 1, kLongFadeMs);
        QTRY_COMPARE_WITH_TIMEOUT(shown->opacity(), 0.0, 3 * kLongFadeMs);
    }

    // During a fade each slide keeps its own look: the outgoing one isn't restyled as the new one.
    void eachSlideFadesWithItsOwnStyle()
    {
        Song song = sample();
        song.groups[2].slideOverrides = {{{"regions", QJsonObject{{"main", QJsonObject{{"color", "#ffcc00"}}}}}}};
        QQuickWindow *w = output("audience");
        m_live->show(throughTemplate(song, [](Template &t) { t.transitions.content = {TransitionKind::Fade, 300}; }), 2);
        QTRY_COMPARE(visibleLyrics(w)->opacity(), 1.0);
        QQuickItem *verse = visibleLyrics(w);
        m_live->next();
        QCOMPARE(visibleLyrics(w)->property("color").value<QColor>(), QColor("#ffcc00"));
        QCOMPARE(verse->property("color").value<QColor>(), QColor("#ffffff"));
        QCOMPARE(verse->property("text").toString(), QStringLiteral("Let earth receive her King"));
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

    // #98: a still from the media library reaches the audience's pixels, stays through slide
    // changes, and the stage display never shows it.
    void aStillBackgroundShowsOnTheAudience()
    {
        QQuickWindow *w = output("audience");
        QQuickWindow *stage = output("stage");
        m_live->show(libraryItem(sample()), 0);   // the blank intro: nothing over the still
        const QUuid still = addStill(Qt::red);
        QVERIFY(m_live->setBackground(still, {}));
        QTRY_COMPARE_WITH_TIMEOUT(player()->showing(), still, 5000);
        QTRY_VERIFY(isMostly(w->grabWindow(), Qt::red));
        QVERIFY(!stage->findChild<QQuickItem *>("background"));
        m_live->next();
        m_live->previous();
        QTRY_VERIFY(isMostly(w->grabWindow(), Qt::red));
        snapshot(w, "still-background");
    }

    void switchingStillsShowsTheNewOne()
    {
        QQuickWindow *w = output("audience");
        QVERIFY(m_live->setBackground(addStill(Qt::red), {}));
        QTRY_VERIFY_WITH_TIMEOUT(isMostly(w->grabWindow(), Qt::red), 5000);
        const QUuid blue = addStill(Qt::blue);
        QVERIFY(m_live->setBackground(blue, {}));
        QTRY_VERIFY_WITH_TIMEOUT(isMostly(w->grabWindow(), Qt::blue), 5000);
    }

    // Clearing uses the layer's transition: a cut by default.
    void clearingTheBackgroundUsesItsTransition()
    {
        QQuickWindow *w = output("audience");
        auto *background = w->findChild<QQuickItem *>("background");
        QVERIFY(background);
        QVERIFY(m_live->setBackground(addStill(Qt::red), {}));
        QCOMPARE(background->opacity(), 1.0);
        m_live->clearBackground();
        QCOMPARE(background->opacity(), 0.0);
        m_live->showBackground();
        QCOMPARE(background->opacity(), 1.0);

        QVERIFY(m_live->setBackground(addStill(Qt::blue), {TransitionKind::Fade, kLongFadeMs}));
        m_live->clearBackground();
        QTRY_VERIFY_WITH_TIMEOUT(background->opacity() > 0 && background->opacity() < 1, kLongFadeMs);
        QTRY_COMPARE_WITH_TIMEOUT(background->opacity(), 0.0, 3 * kLongFadeMs);
    }

    void fillOrFitIsTheStillsFillMode()
    {
        QQuickWindow *w = output("audience");
        QVERIFY(m_live->setBackground(addStill(Qt::red), {}));
        auto *image = w->findChild<QQuickItem *>("backgroundStill");
        QVERIFY(image);
        QCOMPARE(image->property("fillMode").toInt(), 2);   // Image.PreserveAspectCrop
        player()->setFillMode("fit");
        QCOMPARE(image->property("fillMode").toInt(), 1);   // Image.PreserveAspectFit
        player()->setFillMode("fill");
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

    // #96, plan 5.5: a fade drops no frames. The P1 benchmarks' check: a frame interval over
    // 1.5 refresh periods is late. Needs a real display, so it runs only when asked:
    //   VISUALIS_FRAME_BENCH=1 QT_QPA_PLATFORM=cocoa tst_outputs fadesDropNoFrames
    // with VISUALIS_BENCH_VIDEO=<clip> to fade over a playing background. Results are in
    // docs/benchmarks/2026-11-24-fade-frame-timing.md.
    void fadesDropNoFrames()
    {
        if (qEnvironmentVariableIsEmpty("VISUALIS_FRAME_BENCH") || QGuiApplication::platformName() == "offscreen")
            QSKIP("a benchmark; run with VISUALIS_FRAME_BENCH=1 and QT_QPA_PLATFORM=cocoa");
        QQuickWindow *w = output("audience");
        // The background fades below need a background that fades.
        const Transition backgroundFade{TransitionKind::Fade, 600};
        if (const QString clip = qEnvironmentVariable("VISUALIS_BENCH_VIDEO"); !clip.isEmpty()) {
            QVERIFY(m_live->setBackground(addMedia(MediaAsset::Kind::Video, clip), backgroundFade));
            QTRY_COMPARE_WITH_TIMEOUT(player()->status(), QStringLiteral("Playing"), 10000);
        } else {
            QVERIFY(m_live->setBackground(addStill(Qt::darkBlue), backgroundFade));
        }
        const double periodMs = 1000.0 / w->screen()->refreshRate();

        QElapsedTimer clock;
        clock.start();
        std::mutex lock;
        QList<qint64> swaps;   // ns on `clock`, from the render thread
        connect(w, &QQuickWindow::frameSwapped, w, [&] {
            const std::lock_guard<std::mutex> guard(lock);
            swaps << clock.nsecsElapsed();
        }, Qt::DirectConnection);

        struct Fade { qint64 from, to; };
        auto run = [&](int ms, int times, const std::function<void(int)> &step) {
            QList<Fade> fades;
            for (int i = 0; i < times; ++i) {
                const qint64 from = clock.nsecsElapsed();
                step(i);
                fades << Fade{from, from + qint64(ms) * 1'000'000};
                spin(ms + 250);
            }
            return fades;
        };
        auto report = [&](const char *what, const QList<Fade> &fades) {
            const std::lock_guard<std::mutex> guard(lock);
            int frames = 0, late = 0, fewest = INT_MAX;
            double longest = 0;
            for (const Fade &f : fades) {
                int inFade = 0;
                for (qsizetype i = 1; i < swaps.size(); ++i) {
                    if (swaps[i - 1] < f.from || swaps[i] > f.to)
                        continue;   // the first frame of a fade includes the command's latency
                    const double ms = (swaps[i] - swaps[i - 1]) / 1e6;
                    ++inFade;
                    late += ms > 1.5 * periodMs;
                    longest = std::max(longest, ms);
                }
                frames += inFade;
                fewest = std::min(fewest, inFade);
            }
            qInfo("%s: %lld fades, %d frame intervals (fewest in one fade %d), %d late, longest %.2f ms, refresh %.2f Hz",
                  what, qlonglong(fades.size()), frames, fewest, late, longest, 1000 / periodMs);
            return late;
        };

        m_live->show(throughTemplate(sample(), [](Template &t) { t.transitions.content = {TransitionKind::Fade, 500}; }), 1);
        spin(1000);
        const int textLate = report("text fade 500 ms", run(500, 20, [&](int i) { i % 2 ? m_live->previous() : m_live->next(); }));
        const int backgroundLate = report("background fade 600 ms", run(600, 10, [&](int i) {
            i % 2 ? m_live->showBackground() : m_live->clearBackground();
        }));
        QCOMPARE(textLate, 0);
        QCOMPARE(backgroundLate, 0);
    }

    // Plan 5.2: an output owns no state, so a reopened window shows what's live.
    void aNewOutputWindowShowsWhatIsLive()
    {
        m_live->show(libraryItem(sample()), 2);
        QQuickWindow *w = output("audience");
        QTRY_COMPARE(shownLyrics(w), QStringLiteral("Let earth receive her King"));
    }

private:
    BackgroundPlayer *player() const
    {
        return m_engine->singletonInstance<BackgroundPlayer *>("Visualis.Ui", "BackgroundPlayer");
    }

    QUuid addMedia(MediaAsset::Kind kind, const QString &path)
    {
        const QUuid id = QUuid::createUuid();
        m_media[id] = {kind, path};
        return id;
    }

    // A still of one color, as a file the media check points at.
    QUuid addStill(const QColor &color)
    {
        QImage image(320, 180, QImage::Format_RGB32);
        image.fill(color);
        const QString path = m_files.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png");
        if (!image.save(path))
            qFatal("can't write a test still");
        return addMedia(MediaAsset::Kind::Image, path);
    }

    static bool isMostly(const QImage &image, const QColor &color)
    {
        int matching = 0, total = 0;
        for (int y = 0; y < image.height(); y += 16) {
            for (int x = 0; x < image.width(); x += 16, ++total) {
                const QColor c = image.pixelColor(x, y);
                matching += qAbs(c.red() - color.red()) < 40 && qAbs(c.green() - color.green()) < 40
                            && qAbs(c.blue() - color.blue()) < 40;
            }
        }
        return total > 0 && matching > total * 9 / 10;
    }

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

    // Runs the event loop for `ms`. QTest::qWait sleeps between batches of events, which
    // starves animations on the GUI thread and reads as dropped frames.
    static void spin(int ms)
    {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    }

    static QObject *fade(QQuickWindow *w) { return w->findChild<QObject *>("lyricsFade"); }

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
    // Long enough that a stalled CI runner can't overshoot a fade the test checks midway (#108).
    static constexpr int kLongFadeMs = 2000;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    LiveState *m_live = nullptr;
    std::vector<std::unique_ptr<QObject>> m_windows;
    QTemporaryDir m_files;
    QHash<QUuid, std::pair<MediaAsset::Kind, QString>> m_media;
};

QTEST_MAIN(OutputsTest)
#include "tst_outputs.moc"
