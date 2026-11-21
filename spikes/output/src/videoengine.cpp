#include "videoengine.h"
#include "eventlog.h"

#include <QTimer>
#include <rhi/qrhi.h>
#include <QVideoFrame>

VideoEngine *VideoEngine::s_instance = nullptr;

VideoEngine::VideoEngine(const QString &mode, QObject *parent)
    : QObject(parent)
    , m_mode(mode)
{
    s_instance = this;
    m_clock.start();
}

VideoEngine *VideoEngine::create(QQmlEngine *, QJSEngine *engine)
{
    Q_ASSERT(s_instance && engine->thread() == s_instance->thread());
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void VideoEngine::addTarget(QVideoSink *sink, const QString &name)
{
    m_targets.push_back({sink, name});
    rebuild();
}

void VideoEngine::setMode(const QString &mode)
{
    if (mode == m_mode || (mode != "shared" && mode != "per-window"))
        return;
    m_mode = mode;
    EventLog::write("video mode " + mode);
    rebuild();
    emit changed();
}

void VideoEngine::setSource(const QUrl &source)
{
    if (source == m_source)
        return;
    m_source = source;
    EventLog::write("video source " + source.toString());
    rebuild();
    emit changed();
}

void VideoEngine::setBackgroundVisible(bool visible)
{
    if (visible == m_backgroundVisible)
        return;
    m_backgroundVisible = visible;
    EventLog::write(visible ? "command show_background" : "command clear_background");
    emit changed();
}

void VideoEngine::setStatus(const QString &status)
{
    if (status == m_status)
        return;
    m_status = status;
    EventLog::write("video status: " + status);
    emit changed();
}

// The hub's own GPU context, so shared decoding doesn't depend on any output window being alive.
// Its textures are drawn by the output windows' contexts; on Metal they share the one MTLDevice.
QRhi *VideoEngine::hubRhi()
{
#ifdef Q_OS_APPLE
    if (!m_hubRhi && !m_hubRhiFailed) {
        QRhiMetalInitParams params;
        m_hubRhi.reset(QRhi::create(QRhi::Metal, &params));
        m_hubRhiFailed = !m_hubRhi;
        EventLog::write(m_hubRhi ? "hub GPU context: own Metal QRhi" : "hub GPU context: Metal QRhi failed, borrowing an output's");
    }
#endif
    return m_hubRhi.get();
}

void VideoEngine::rebuild()
{
    const int generation = ++m_rebuildGeneration; // a newer rebuild cancels a pending retry
    m_players.clear();
    for (auto &t : m_targets) {
        if (t.sink)
            t.sink->setVideoFrame(QVideoFrame()); // drop the old picture
    }
    if (m_source.isEmpty() || m_targets.empty()) {
        setStatus("no video");
        return;
    }

    auto start = [this](Player *p, QVideoSink *sink) {
        auto *player = p->player.get();
        connect(player, &QMediaPlayer::errorOccurred, this, [this, p](QMediaPlayer::Error, const QString &msg) {
            setStatus(p->label + " error: " + msg);
        });
        connect(player, &QMediaPlayer::mediaStatusChanged, this, [this, p](QMediaPlayer::MediaStatus s) {
            if (s == QMediaPlayer::BufferedMedia)
                setStatus(QStringLiteral("playing (%1, %2 decoder%3)")
                              .arg(m_mode).arg(m_players.size()).arg(m_players.size() == 1 ? "" : "s"));
        });
        player->setLoops(QMediaPlayer::Infinite);
        player->setVideoSink(sink);
        player->setSource(m_source); // no audio output set: background audio stays silent
        player->play();
    };

    if (m_mode == "shared") {
        // Without an RHI the hub gets frames in CPU memory (measured: ~2x CPU, ~2.5x memory at 4K).
        QRhi *rhi = hubRhi();
        if (!rhi) {
            // Fallback: borrow an output window's. It only exists once that window's scene graph
            // starts, and it dies with the window, so this path isn't safe across hot-plug.
            for (auto &t : m_targets) {
                if (t.sink && t.sink->rhi()) {
                    rhi = t.sink->rhi();
                    break;
                }
            }
        }
        if (!rhi) {
            setStatus("waiting for output GPU context");
            QTimer::singleShot(50, this, [this, generation] {
                if (generation == m_rebuildGeneration)
                    rebuild();
            });
            return;
        }
        auto p = std::make_unique<Player>();
        p->label = "shared";
        p->player = std::make_unique<QMediaPlayer>();
        p->hub = std::make_unique<QVideoSink>();
        QVideoSink *hub = p->hub.get();
        hub->setRhi(rhi);
        // Fan out on the decoder's thread: VideoOutput sinks accept frames from any thread.
        connect(hub, &QVideoSink::videoFrameChanged, hub, [this](const QVideoFrame &frame) {
            for (auto &t : m_targets) {
                if (t.sink)
                    t.sink->setVideoFrame(frame);
            }
        }, Qt::DirectConnection);
        watchFrames(p.get(), hub);
        m_players.push_back(std::move(p));
        start(m_players.back().get(), hub);
    } else {
        for (auto &t : m_targets) {
            if (!t.sink)
                continue;
            auto p = std::make_unique<Player>();
            p->label = t.name;
            p->player = std::make_unique<QMediaPlayer>();
            watchFrames(p.get(), t.sink);
            m_players.push_back(std::move(p));
            start(m_players.back().get(), t.sink);
        }
    }
    setStatus(QStringLiteral("loading (%1)").arg(m_mode));
}

// Detects the loop point (presentation time jumps backwards) and logs how long the
// wall-clock gap between the last frame and the first frame of the next loop was.
void VideoEngine::watchFrames(Player *p, QVideoSink *sink)
{
    connect(sink, &QVideoSink::videoFrameChanged, p->player.get(), [this, p, sink](const QVideoFrame &frame) {
        if (!frame.isValid())
            return;
        if (!p->loggedFirstFrame) {
            // Tells whether the decoder hands over GPU textures or CPU memory (plan 5.6 question).
            p->loggedFirstFrame = true;
            EventLog::write(QStringLiteral("first frame %1: handle=%2 format=%3 sink-rhi=%4")
                                .arg(p->label)
                                .arg(frame.handleType() == QVideoFrame::RhiTextureHandle ? "rhi-texture" : "none")
                                .arg(QVideoFrameFormat::pixelFormatToString(frame.pixelFormat()))
                                .arg(sink->rhi() ? "yes" : "no"));
        }
        const qint64 now = m_clock.nsecsElapsed();
        const qint64 startUs = frame.startTime();
        if (p->lastStartUs >= 0 && startUs >= 0 && startUs + 500'000 < p->lastStartUs) {
            const double wallMs = (now - p->lastFrameNs) / 1e6;
            const qint64 durUs = frame.endTime() - frame.startTime();
            const double nominalMs = durUs > 0 ? durUs / 1000.0 : 0;
            const double gapMs = wallMs - nominalMs;
            EventLog::write(QStringLiteral("loop %1: %2 ms between last and first frame (nominal frame %3 ms, gap %4 ms)")
                                .arg(p->label).arg(wallMs, 0, 'f', 1).arg(nominalMs, 0, 'f', 1).arg(gapMs, 0, 'f', 1));
            QMetaObject::invokeMethod(this, [this, gapMs] {
                m_lastLoopGapMs = gapMs;
                ++m_loopCount;
                emit loopGapChanged();
            }, Qt::QueuedConnection);
        }
        p->lastStartUs = startUs;
        p->lastFrameNs = now;
    }, Qt::DirectConnection);
}
