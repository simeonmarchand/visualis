#include "video/videoengine.h"

#include <QMediaPlayer>
#include <QMutexLocker>
#include <QTimer>
#include <QVideoFrame>
#include <rhi/qrhi.h>

using namespace visualis;
using namespace std::chrono;

Q_LOGGING_CATEGORY(lcVideo, "visualis.video")
Q_LOGGING_CATEGORY(lcVideoFrames, "visualis.video.frames", QtInfoMsg)

VideoEngine::VideoEngine(QObject *parent)
    : QObject(parent)
    , m_status(QStringLiteral("No background"))
{
    m_clock.start();
    connect(&m_watchdog, &FrameWatchdog::playing, this, [this] {
        m_failuresInARow = 0;
        setStatus(QStringLiteral("Playing"), m_alert);
    });
    connect(&m_watchdog, &FrameWatchdog::stalled, this, &VideoEngine::onStalled);
}

VideoEngine::~VideoEngine()
{
    // The player is a child, which QObject would delete after the hub and its GPU context.
    stopPlayer();
}

void VideoEngine::addTarget(QVideoSink *sink)
{
    if (!sink)
        return;
    {
        QMutexLocker lock(&m_targetsMutex);
        m_targets.push_back(sink);
    }
    connect(sink, &QObject::destroyed, this, [this, sink] {
        QMutexLocker lock(&m_targetsMutex);
        std::erase(m_targets, sink);
    }, Qt::DirectConnection);
    if (!m_player)
        startPlayer();
}

void VideoEngine::setSource(const QUrl &source)
{
    if (source == m_source)
        return;
    m_source = source;
    qCInfo(lcVideo).noquote() << "background" << (source.isEmpty() ? QStringLiteral("none") : source.toString());
    m_failuresInARow = 0;
    m_alert.clear();
    stopPlayer();
    {
        QMutexLocker lock(&m_targetsMutex);
        for (QVideoSink *sink : m_targets)
            sink->setVideoFrame(QVideoFrame());   // drop the old picture
    }
    startPlayer();
    emit sourceChanged();
}

// The hub's own GPU context, so decoding doesn't depend on any output window being alive
// (an output's context dies with its window on hot-plug). Without one, frames arrive in
// CPU memory: about 2x the CPU and 2.5x the memory at 4K (P1). On Metal the outputs draw
// the hub's textures because they share the one MTLDevice.
QRhi *VideoEngine::hubRhi()
{
#ifdef Q_OS_APPLE
    if (!m_hubRhi && !m_hubRhiFailed) {
        QRhiMetalInitParams params;
        m_hubRhi.reset(QRhi::create(QRhi::Metal, &params));
        m_hubRhiFailed = !m_hubRhi;
        if (m_hubRhiFailed)
            qCWarning(lcVideo) << "no Metal context for the video hub; decoding to CPU memory";
    }
#endif
    return m_hubRhi.get();
}

void VideoEngine::startPlayer()
{
    ++m_generation;
    bool hasTargets;
    {
        QMutexLocker lock(&m_targetsMutex);
        hasTargets = !m_targets.empty();
    }
    if (m_source.isEmpty() || !hasTargets) {
        setStatus(QStringLiteral("No background"), {});
        return;
    }

    m_hub = std::make_unique<QVideoSink>();
    m_hub->setRhi(hubRhi());
    // Fan out on the decoder's thread: VideoOutput sinks take frames from any thread.
    connect(m_hub.get(), &QVideoSink::videoFrameChanged, this, &VideoEngine::onFrame, Qt::DirectConnection);

    m_player = new QMediaPlayer(this);
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        // The watchdog notices the missing frames and retries, backing off.
        qCWarning(lcVideo).noquote() << "player error:" << message;
        setStatus(QStringLiteral("Can't play: %1").arg(message), QStringLiteral("Background video can't play: %1").arg(message));
    });
    m_player->setLoops(QMediaPlayer::Infinite);
    m_player->setVideoSink(m_hub.get());
    m_player->setSource(m_source);   // no audio output: backgrounds are silent
    m_loggedFirstFrame = false;
    m_lastStartUs = -1;
    m_player->play();
    m_watchdog.start();
    if (m_failuresInARow == 0)
        setStatus(QStringLiteral("Loading"), m_alert);
}

void VideoEngine::stopPlayer()
{
    ++m_generation;
    m_watchdog.stop();
    if (m_player) {
        m_player->stop();
        delete m_player;   // before the hub it renders into
        m_player = nullptr;
    }
    m_hub.reset();
}

void VideoEngine::onFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    const qint64 durationUs = frame.endTime() - frame.startTime();
    m_watchdog.frameArrived(microseconds(durationUs > 0 ? durationUs : 0));
    {
        QMutexLocker lock(&m_targetsMutex);
        for (QVideoSink *sink : m_targets)
            sink->setVideoFrame(frame);
    }

    if (!lcVideoFrames().isDebugEnabled())
        return;
    const qint64 now = m_clock.nsecsElapsed();
    if (!m_loggedFirstFrame) {
        // Whether frames stay on the GPU (rhi-texture) or come through CPU memory.
        m_loggedFirstFrame = true;
        m_secondStartNs = now;
        m_framesThisSecond = 0;
        qCDebug(lcVideoFrames).noquote()
            << "first frame:" << (frame.handleType() == QVideoFrame::RhiTextureHandle ? "rhi-texture" : "cpu-memory")
            << QVideoFrameFormat::pixelFormatToString(frame.pixelFormat()) << frame.size();
    }
    ++m_framesThisSecond;
    if (now - m_secondStartNs >= 1'000'000'000) {
        qCDebug(lcVideoFrames).noquote() << "decoded" << m_framesThisSecond << "frames in the last second";
        m_secondStartNs = now;
        m_framesThisSecond = 0;
    }
    // The loop point: presentation time jumps back. The gap is the wall time between the
    // last frame and the first of the next loop, beyond one frame's duration.
    const qint64 startUs = frame.startTime();
    if (m_lastStartUs >= 0 && startUs >= 0 && startUs + 500'000 < m_lastStartUs) {
        const double wallMs = (now - m_lastFrameNs) / 1e6;
        qCDebug(lcVideoFrames).noquote() << QStringLiteral("loop: %1 ms between frames, gap %2 ms")
                                                .arg(wallMs, 0, 'f', 1).arg(wallMs - durationUs / 1000.0, 0, 'f', 1);
    }
    m_lastStartUs = startUs;
    m_lastFrameNs = now;
}

void VideoEngine::onStalled(milliseconds silence)
{
    ++m_failuresInARow;
    ++m_restartCount;
    const milliseconds delay = restartDelay(m_failuresInARow);
    qCWarning(lcVideo).noquote() << QStringLiteral("no frames for %1 ms; restarting the player in %2 ms (restart %3)")
                                        .arg(silence.count()).arg(delay.count()).arg(m_restartCount);
    setStatus(QStringLiteral("Restarting"),
              QStringLiteral("Background video stopped for %1 s and was restarted (%2 %3).")
                  .arg(silence.count() / 1000.0, 0, 'f', 1).arg(m_restartCount)
                  .arg(m_restartCount == 1 ? "time" : "times"));
    // Outputs keep the last frame meanwhile, which looks better than black.
    stopPlayer();
    const int generation = m_generation;
    QTimer::singleShot(delay, this, [this, generation] {
        if (generation == m_generation)
            startPlayer();
    });
}

void VideoEngine::setStatus(const QString &status, const QString &alert)
{
    if (status == m_status && alert == m_alert)
        return;
    m_status = status;
    m_alert = alert;
    qCInfo(lcVideo).noquote() << "status:" << status;
    emit statusChanged();
}
