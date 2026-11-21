#pragma once

#include <QObject>
#include <QTimer>

#include <atomic>
#include <chrono>

namespace visualis {

// Notices when a video stops delivering frames. In the P1 soak the background froze for
// 3–11.5 s at loop points while the outputs kept drawing the last frame (soak finding 9),
// so nothing else would notice. The engine restarts the player when this fires.
//
// frameArrived() may be called from the decoder's thread; everything else, and the
// signals, stay on the thread that owns the watchdog.
class FrameWatchdog : public QObject
{
    Q_OBJECT

public:
    struct Timing
    {
        std::chrono::milliseconds stallAfter{1000};        // no frame for this long once playing
        std::chrono::milliseconds firstFrameWithin{5000};  // after start(), opening the file included
        std::chrono::milliseconds checkEvery{100};
    };

    explicit FrameWatchdog(QObject *parent = nullptr) : FrameWatchdog(Timing{}, parent) {}
    explicit FrameWatchdog(Timing timing, QObject *parent = nullptr);

    // A player (re)started: expect a first frame within firstFrameWithin.
    void start();
    void stop();
    bool isWatching() const { return m_timer.isActive(); }

    // frameDuration lets a slow clip (say 1 fps) wait longer than stallAfter: the limit is
    // at least three frames.
    void frameArrived(std::chrono::microseconds frameDuration = {});

signals:
    void playing();                                  // first frame after start()
    void stalled(std::chrono::milliseconds silence); // then stops watching until start()

private:
    void check();
    static qint64 nowNs();

    Timing m_timing;
    QTimer m_timer;
    qint64 m_startedNs = 0;
    bool m_sawFrame = false;
    std::atomic<qint64> m_lastFrameNs{0};
    std::atomic<qint64> m_frameDurationUs{0};
};

// How long to wait before restarting a player that has stalled `failures` times in a row
// without playing in between: at once the first time, then 1, 2, 4… s, at most 30 s, so a
// broken file doesn't spin the decoder while the operator fixes it.
std::chrono::milliseconds restartDelay(int failures);

} // namespace visualis
