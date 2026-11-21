#include "video/framewatchdog.h"

#include <algorithm>

using namespace std::chrono;

namespace visualis {

FrameWatchdog::FrameWatchdog(Timing timing, QObject *parent)
    : QObject(parent)
    , m_timing(timing)
{
    m_timer.setInterval(m_timing.checkEvery);
    connect(&m_timer, &QTimer::timeout, this, &FrameWatchdog::check);
}

qint64 FrameWatchdog::nowNs()
{
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void FrameWatchdog::start()
{
    m_startedNs = nowNs();
    m_sawFrame = false;
    m_lastFrameNs = 0;
    m_frameDurationUs = 0;
    m_timer.start();
}

void FrameWatchdog::stop()
{
    m_timer.stop();
}

void FrameWatchdog::frameArrived(microseconds frameDuration)
{
    m_lastFrameNs = nowNs();
    if (frameDuration.count() > 0)
        m_frameDurationUs = frameDuration.count();
}

void FrameWatchdog::check()
{
    const qint64 now = nowNs();
    const qint64 last = m_lastFrameNs;
    // A frame from before start() belongs to the old player.
    const bool hasFrame = last >= m_startedNs;

    if (hasFrame && !m_sawFrame) {
        m_sawFrame = true;
        emit playing();
    }

    milliseconds limit = m_timing.firstFrameWithin;
    qint64 since = m_startedNs;
    if (m_sawFrame) {
        limit = std::max(m_timing.stallAfter, duration_cast<milliseconds>(microseconds(3 * m_frameDurationUs)));
        since = last;
    }
    const auto silence = duration_cast<milliseconds>(nanoseconds(now - since));
    if (silence > limit) {
        m_timer.stop();
        emit stalled(silence);
    }
}

milliseconds restartDelay(int failures)
{
    if (failures <= 1)
        return 0ms;
    return std::min(milliseconds(1000LL << std::min(failures - 2, 5)), milliseconds(30'000));
}

} // namespace visualis
