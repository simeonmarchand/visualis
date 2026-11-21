#include "video/videoengine.h"

#include <QGuiApplication>
#include <QMediaPlayer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVideoFrame>
#include <QtTest>

#include <atomic>

// Plays a real clip through the engine. The clip is synthetic (ffmpeg's test pattern),
// generated here so no media lives in the repo; without ffmpeg these tests skip.
class VideoEngineTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
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

        auto *player = engine.findChild<QMediaPlayer *>();
        QVERIFY(player);
        player->pause();
        QTRY_COMPARE_WITH_TIMEOUT(engine.restartCount(), 1, 3000);
        QVERIFY(!engine.alert().isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), QStringLiteral("Playing"), 10000);
        const int before = *frames;
        QTRY_VERIFY(*frames > before + 10);
        QVERIFY(engine.findChild<QMediaPlayer *>() != player);
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
