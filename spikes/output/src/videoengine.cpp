#include "videoengine.h"
#include "eventlog.h"

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

void VideoEngine::rebuild()
{
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
        auto p = std::make_unique<Player>();
        p->label = "shared";
        p->player = std::make_unique<QMediaPlayer>();
        p->hub = std::make_unique<QVideoSink>();
        QVideoSink *hub = p->hub.get();
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
    connect(sink, &QVideoSink::videoFrameChanged, p->player.get(), [this, p](const QVideoFrame &frame) {
        if (!frame.isValid())
            return;
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
                emit loopGapChanged();
            }, Qt::QueuedConnection);
        }
        p->lastStartUs = startUs;
        p->lastFrameNs = now;
    }, Qt::DirectConnection);
}
