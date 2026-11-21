#include "framestats.h"
#include "eventlog.h"
#include "livestate.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QScreen>
#include <QVariantMap>

#if defined(Q_OS_MACOS)
#include <mach/mach.h>
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
#include <sys/resource.h>
#endif
#if defined(Q_OS_LINUX)
#include <unistd.h>
#include <fstream>
#endif

FrameStats *FrameStats::s_instance = nullptr;

FrameStats::FrameStats(const QString &videoMode, const QString &backend, QObject *parent)
    : QObject(parent)
    , m_videoMode(videoMode)
    , m_backend(backend)
{
    s_instance = this;
    m_clock.start();
    m_lastCpuSeconds = processCpuSeconds();

    m_csv.setFileName(EventLog::dir() + "/frames.csv");
    if (m_csv.open(QIODevice::WriteOnly | QIODevice::Text))
        m_csv.write("time,window,exposed,frames,fps,late,max_interval_ms,refresh_hz,cpu_percent,rss_mb,video_mode,backend\n");

    connect(LiveState::instance(), &LiveState::changed, this, &FrameStats::onCommand);
    connect(&m_timer, &QTimer::timeout, this, &FrameStats::tick);
    m_timer.start(1000);
}

FrameStats *FrameStats::create(QQmlEngine *, QJSEngine *engine)
{
    Q_ASSERT(s_instance && engine->thread() == s_instance->thread());
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void FrameStats::track(QQuickWindow *window, const QString &name)
{
    auto tracked = std::make_unique<Tracked>();
    tracked->window = window;
    tracked->name = name;
    Tracked *t = tracked.get();
    m_tracked.push_back(std::move(tracked));
    resetTiming(window);

    // Both signals fire on the render thread; handle them there so timestamps aren't
    // delayed by the GUI event queue.
    connect(window, &QQuickWindow::afterSynchronizing, window, [t] {
        QMutexLocker lock(&t->mutex);
        if (t->commandAtNs)
            t->commandSynced = true;
    }, Qt::DirectConnection);
    connect(window, &QQuickWindow::frameSwapped, window, [this, t] { onFrameSwapped(t); },
            Qt::DirectConnection);
}

void FrameStats::resetTiming(QQuickWindow *window)
{
    for (auto &t : m_tracked) {
        if (t->window != window)
            continue;
        QMutexLocker lock(&t->mutex);
        t->lastSwapNs = 0;
        if (window->screen() && window->screen()->refreshRate() > 1)
            t->refreshHz = window->screen()->refreshRate();
    }
}

void FrameStats::onFrameSwapped(Tracked *t)
{
    const qint64 now = m_clock.nsecsElapsed();
    qint64 frameNumber;
    double latencyMs = -1;
    {
        QMutexLocker lock(&t->mutex);
        if (t->lastSwapNs) {
            const double intervalMs = (now - t->lastSwapNs) / 1e6;
            if (intervalMs > 1.5 * 1000.0 / t->refreshHz) {
                ++t->late;
                ++t->totalLate;
            }
            t->maxIntervalMs = qMax(t->maxIntervalMs, intervalMs);
        }
        t->lastSwapNs = now;
        ++t->frames;
        frameNumber = ++t->totalFrames;
        if (t->commandSynced) {
            latencyMs = (now - t->commandAtNs) / 1e6;
            t->lastLatencyMs = latencyMs;
            t->commandAtNs = 0;
            t->commandSynced = false;
        }
    }
    if (latencyMs >= 0)
        EventLog::write(QStringLiteral("latency %1 command->swap %2 ms").arg(t->name).arg(latencyMs, 0, 'f', 1));

    // Changing the counter every frame keeps the window rendering continuously, so a
    // long interval really is a dropped frame and not an idle scene.
    QQuickWindow *window = t->window.data();
    QMetaObject::invokeMethod(window, [window, frameNumber] {
        window->setProperty("frameNumber", frameNumber);
    }, Qt::QueuedConnection);
}

void FrameStats::onCommand()
{
    const qint64 now = m_clock.nsecsElapsed();
    for (auto &t : m_tracked) {
        QMutexLocker lock(&t->mutex);
        t->commandAtNs = now;
        t->commandSynced = false;
    }
}

void FrameStats::tick()
{
    const qint64 now = m_clock.nsecsElapsed();
    const double tickSeconds = m_lastTickNs ? (now - m_lastTickNs) / 1e9 : 1.0;
    const double cpu = processCpuSeconds();
    if (m_lastTickNs)
        m_cpuPercent = 100.0 * (cpu - m_lastCpuSeconds) / tickSeconds;
    m_lastCpuSeconds = cpu;
    m_lastTickNs = now;
    m_rssMb = processRssMb();

    const QString time = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    QVariantList snapshot;
    for (auto &t : m_tracked) {
        QVariantMap row;
        {
            QMutexLocker lock(&t->mutex);
            row["name"] = t->name;
            row["frames"] = t->frames;
            row["fps"] = qRound(t->frames / tickSeconds);
            row["late"] = t->late;
            row["maxIntervalMs"] = t->maxIntervalMs;
            row["totalFrames"] = t->totalFrames;
            row["totalLate"] = t->totalLate;
            row["refreshHz"] = t->refreshHz;
            row["latencyMs"] = t->lastLatencyMs;
            t->frames = 0;
            t->late = 0;
            t->maxIntervalMs = 0;
        }
        const bool visible = t->window && t->window->isVisible();
        // Exposed = the OS is actually letting the window draw. macOS stops drawing
        // windows that are fully covered, which looks exactly like a frozen output.
        const bool exposed = visible && t->window->isExposed();
        if (int(exposed) != t->exposedState) {
            EventLog::write(QStringLiteral("window %1 %2").arg(t->name, exposed ? "exposed" : "not exposed"));
            t->exposedState = exposed;
        }
        row["visible"] = visible;
        row["exposed"] = exposed;
        if (visible && m_csv.isOpen()) {
            m_csv.write(QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12\n")
                            .arg(time, row["name"].toString())
                            .arg(int(exposed))
                            .arg(row["frames"].toInt())
                            .arg(row["fps"].toInt())
                            .arg(row["late"].toInt())
                            .arg(row["maxIntervalMs"].toDouble(), 0, 'f', 2)
                            .arg(row["refreshHz"].toDouble(), 0, 'f', 2)
                            .arg(m_cpuPercent, 0, 'f', 1)
                            .arg(m_rssMb, 0, 'f', 1)
                            .arg(m_videoMode, m_backend)
                            .toUtf8());
        }
        snapshot.append(row);
    }
    m_csv.flush();
    m_snapshot = snapshot;
    emit updated();
}

double FrameStats::processCpuSeconds()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6
         + usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
#else
    return 0; // not measured on this platform in the spike
#endif
}

double FrameStats::processRssMb()
{
#if defined(Q_OS_MACOS)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return -1;
    return info.resident_size / (1024.0 * 1024.0);
#elif defined(Q_OS_LINUX)
    long pages = 0, resident = 0;
    std::ifstream("/proc/self/statm") >> pages >> resident;
    return resident * sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0);
#else
    return -1;
#endif
}
