#include "video/framewatchdog.h"

#include <QSignalSpy>
#include <QtTest>

#include <thread>

using namespace visualis;
using namespace std::chrono_literals;

namespace {

// Short times so the tests run quickly; the ratios match the real ones.
const FrameWatchdog::Timing kFast{200ms, 600ms, 10ms};

// Delivers frames every `interval` until `total` has passed.
void feed(FrameWatchdog &w, std::chrono::milliseconds interval, std::chrono::milliseconds total,
          std::chrono::microseconds frameDuration = {})
{
    for (auto t = 0ms; t < total; t += interval) {
        w.frameArrived(frameDuration);
        QTest::qWait(int(interval.count()));
    }
}

} // namespace

class FrameWatchdogTest : public QObject
{
    Q_OBJECT

private slots:
    void steadyFramesNeverStall()
    {
        FrameWatchdog w(kFast);
        QSignalSpy stalled(&w, &FrameWatchdog::stalled);
        QSignalSpy playing(&w, &FrameWatchdog::playing);
        w.start();
        feed(w, 20ms, 500ms);
        QCOMPARE(playing.count(), 1);
        QCOMPARE(stalled.count(), 0);
        QVERIFY(w.isWatching());
    }

    void framesStoppingIsAStall()
    {
        FrameWatchdog w(kFast);
        QSignalSpy stalled(&w, &FrameWatchdog::stalled);
        w.start();
        feed(w, 20ms, 200ms);
        QVERIFY(stalled.wait(2000));
        QCOMPARE(stalled.count(), 1);
        QVERIFY(stalled.at(0).at(0).value<std::chrono::milliseconds>() > 200ms);
        QVERIFY(!w.isWatching());   // stopped until the engine restarts the player
    }

    void noFirstFrameIsAStallAfterTheLongerLimit()
    {
        FrameWatchdog w(kFast);
        QSignalSpy stalled(&w, &FrameWatchdog::stalled);
        QSignalSpy playing(&w, &FrameWatchdog::playing);
        w.start();
        QTest::qWait(350);   // past stallAfter, within firstFrameWithin: opening a file takes time
        QCOMPARE(stalled.count(), 0);
        QVERIFY(stalled.wait(2000));
        QCOMPARE(playing.count(), 0);
    }

    void aSlowClipWaitsThreeFrames()
    {
        // 150 ms frames: the limit is 450 ms, not 200 ms.
        FrameWatchdog w(kFast);
        QSignalSpy stalled(&w, &FrameWatchdog::stalled);
        w.start();
        feed(w, 100ms, 400ms, 150'000us);
        QTest::qWait(250);
        QCOMPARE(stalled.count(), 0);
        QVERIFY(stalled.wait(2000));
    }

    void framesFromTheOldPlayerDontCountAfterARestart()
    {
        FrameWatchdog w(kFast);
        QSignalSpy playing(&w, &FrameWatchdog::playing);
        QSignalSpy stalled(&w, &FrameWatchdog::stalled);
        w.start();
        w.frameArrived();
        QTRY_COMPARE(playing.count(), 1);
        w.start();   // restarted, and the new player never delivers
        QVERIFY(stalled.wait(2000));
        QCOMPARE(playing.count(), 1);
    }

    void framesFromAnotherThreadAreSeen()
    {
        FrameWatchdog w(kFast);
        QSignalSpy stalled(&w, &FrameWatchdog::stalled);
        QSignalSpy playing(&w, &FrameWatchdog::playing);
        w.start();
        std::atomic<bool> stop = false;
        std::thread decoder([&] {
            while (!stop) {
                w.frameArrived(33'333us);
                std::this_thread::sleep_for(20ms);
            }
        });
        QTest::qWait(400);
        stop = true;
        decoder.join();
        QCOMPARE(playing.count(), 1);
        QCOMPARE(stalled.count(), 0);
    }

    void restartsBackOff()
    {
        QCOMPARE(restartDelay(1), 0ms);
        QCOMPARE(restartDelay(2), 1000ms);
        QCOMPARE(restartDelay(3), 2000ms);
        QCOMPARE(restartDelay(6), 16000ms);
        QCOMPARE(restartDelay(7), 30000ms);
        QCOMPARE(restartDelay(100), 30000ms);
    }
};

QTEST_GUILESS_MAIN(FrameWatchdogTest)
#include "tst_framewatchdog.moc"
