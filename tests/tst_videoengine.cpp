#include "output/platformdisplay.h"
#include "video/videoengine.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVideoFrame>
#include <QtTest>

#include <atomic>

#ifdef Q_OS_MACOS
#include <IOKit/pwr_mgt/IOPMLib.h>
#endif

// Plays a real clip through the engine. The clip is synthetic (ffmpeg's test pattern),
// generated here so no media lives in the repo; without ffmpeg these tests skip.
class VideoEngineTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // AVFoundation delivers no frames while every display is asleep, which made these
        // tests fail on an unattended Mac. Wake the displays as a key press would, and keep
        // them awake until the tests finish.
#ifdef Q_OS_MACOS
        IOPMAssertionID activity = kIOPMNullAssertionID;
        IOPMAssertionDeclareUserActivity(CFSTR("Visualis video engine tests"), kIOPMUserActiveLocal, &activity);
        m_awake.setActive(true);
        QTest::qWait(1500);   // displays take a moment to come back
#endif
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty())
            QSKIP("ffmpeg not found; can't make a test clip");
        QVERIFY(m_dir.isValid());
        m_clip = m_dir.filePath("pattern.mp4");
        // 2 s at 30 fps, so the playback tests cross loop points.
        QProcess p;
        p.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "testsrc=size=320x180:rate=30:duration=2",
                         "-c:v", "libx264", "-pix_fmt", "yuv420p", m_clip});
        QVERIFY(p.waitForFinished(30000));
        QCOMPARE(p.exitCode(), 0);
    }

    void playsIntoEveryTarget()
    {
        VideoEngine engine;
        QVideoSink a, b;
        auto framesA = countFrames(&a), framesB = countFrames(&b);
        engine.addTarget(&a);
        engine.addTarget(&b);
        QCOMPARE(engine.status(), QStringLiteral("No background"));

        engine.setSource(QUrl::fromLocalFile(m_clip));
        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), QStringLiteral("Playing"), 10000);
        QTRY_VERIFY(*framesA > 10 && *framesB > 10);
    }

    void loopingDoesNotTripTheWatchdog()
    {
        VideoEngine engine;
        QVideoSink sink;
        engine.addTarget(&sink);
        engine.setSource(QUrl::fromLocalFile(m_clip));
        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), QStringLiteral("Playing"), 10000);
        QTest::qWait(4500);   // at least two loop points
        QCOMPARE(engine.restartCount(), 0);
        QCOMPARE(engine.status(), QStringLiteral("Playing"));
        QVERIFY(engine.alert().isEmpty());
    }

    // Soak finding 9: the decoder stopped delivering for seconds. Pausing it does the same.
    void aStalledPlayerIsRestartedAndTheOperatorTold()
    {
        VideoEngine engine;
        QVideoSink sink;
        auto frames = countFrames(&sink);
        engine.addTarget(&sink);
        engine.setSource(QUrl::fromLocalFile(m_clip));
        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), QStringLiteral("Playing"), 10000);

        // A QPointer, not a raw pointer: the replacement is often allocated at the address the
        // stalled player was freed from, so comparing addresses could see "the same player"
        // (#87). The guard clears when that player is destroyed.
        QPointer<QMediaPlayer> stalled = engine.findChild<QMediaPlayer *>();
        QVERIFY(stalled);
        stalled->pause();
        QTRY_COMPARE_WITH_TIMEOUT(engine.restartCount(), 1, 3000);
        QVERIFY(!engine.alert().isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), QStringLiteral("Playing"), 10000);
        const int before = *frames;
        QTRY_VERIFY(*frames > before + 10);
        QVERIFY(stalled.isNull());                     // the stalled player was destroyed...
        QVERIFY(engine.findChild<QMediaPlayer *>());   // ...and a new one is playing
    }

    // #61: AVFoundation delivers nothing while every display sleeps (pausing stands in for
    // that). That's no failure, and video must be back within about 1 s of waking, not at
    // the next backed-off restart.
    void sleepingDisplaysAreNotAFailureAndWakeResumesAtOnce()
    {
        VideoEngine engine;
        QVideoSink sink;
        auto frames = countFrames(&sink);
        engine.addTarget(&sink);
        engine.setSource(QUrl::fromLocalFile(m_clip));
        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), QStringLiteral("Playing"), 10000);

        engine.setDisplaysAsleep(true);
        auto *player = engine.findChild<QMediaPlayer *>();
        QVERIFY(player);
        player->pause();
        QTRY_VERIFY_WITH_TIMEOUT(!engine.findChild<QMediaPlayer *>(), 3000);   // the watchdog gave up on it
        QTest::qWait(1500);   // no restarts while asleep, so the backoff never grows
        QCOMPARE(engine.restartCount(), 0);
        QVERIFY(engine.alert().isEmpty());
        QCOMPARE(engine.status(), QStringLiteral("Paused while the displays sleep"));
        QVERIFY(!engine.findChild<QMediaPlayer *>());
        QVERIFY(sink.videoFrame().isValid());   // outputs keep the last frame

        const int before = *frames;
        QElapsedTimer sinceWake;
        sinceWake.start();
        engine.setDisplaysAsleep(false);
        QTRY_VERIFY_WITH_TIMEOUT(*frames > before, 1500);
        qInfo() << "first frame" << sinceWake.elapsed() << "ms after wake";
        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), QStringLiteral("Playing"), 1000);
        QCOMPARE(engine.restartCount(), 0);
        QVERIFY(engine.alert().isEmpty());
    }

    void clearingTheSourceStopsAndBlanksTheOutputs()
    {
        VideoEngine engine;
        QVideoSink sink;
        engine.addTarget(&sink);
        engine.setSource(QUrl::fromLocalFile(m_clip));
        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), QStringLiteral("Playing"), 10000);

        engine.setSource({});
        QCOMPARE(engine.status(), QStringLiteral("No background"));
        QVERIFY(!engine.findChild<QMediaPlayer *>());
        QVERIFY(!sink.videoFrame().isValid());
        QTest::qWait(1500);
        QCOMPARE(engine.restartCount(), 0);   // stopping on purpose isn't a stall
    }

    void aMissingFileIsReportedNotFatal()
    {
        VideoEngine engine;
        QVideoSink sink;
        engine.addTarget(&sink);
        engine.setSource(QUrl::fromLocalFile(m_dir.filePath("missing.mp4")));
        QTRY_VERIFY_WITH_TIMEOUT(!engine.alert().isEmpty(), 10000);
    }

private:
    // Counted on whichever thread delivers the frame.
    std::shared_ptr<std::atomic<int>> countFrames(QVideoSink *sink)
    {
        auto count = std::make_shared<std::atomic<int>>(0);
        connect(sink, &QVideoSink::videoFrameChanged, this, [count](const QVideoFrame &f) {
            if (f.isValid())
                ++*count;
        }, Qt::DirectConnection);
        return count;
    }

    QTemporaryDir m_dir;
    QString m_clip;
    DisplaySleepBlocker m_awake;
};

int main(int argc, char *argv[])
{
    // As the app does: AVFoundation on macOS (ADR 0001).
#ifdef Q_OS_MACOS
    qputenv("QT_MEDIA_BACKEND", "darwin");
#endif
    QGuiApplication app(argc, argv);
    VideoEngineTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_videoengine.moc"
