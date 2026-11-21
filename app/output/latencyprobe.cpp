#include "output/latencyprobe.h"

#include <QLoggingCategory>

// Off unless enabled: QT_LOGGING_RULES="visualis.latency.debug=true"
Q_LOGGING_CATEGORY(lcLatency, "visualis.latency", QtInfoMsg)

LatencyProbe::LatencyProbe(QQuickWindow *window, const QString &role, QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_role(role)
{
    m_clock.start();
    // Both run on the render thread with the threaded render loop, so they only touch atomics.
    connect(window, &QQuickWindow::afterSynchronizing, this, [this] {
        if (m_armedAtNs.load())
            m_synced = true;
    }, Qt::DirectConnection);
    connect(window, &QQuickWindow::frameSwapped, this, [this] {
        if (!m_synced.exchange(false))
            return;
        const qint64 armedAt = m_armedAtNs.exchange(0);
        if (!armedAt)
            return;
        const double ms = (m_clock.nsecsElapsed() - armedAt) / 1e6;
        QMetaObject::invokeMethod(this, [this, ms] {
            qCDebug(lcLatency).noquote() << QStringLiteral("%1 -> %2 frame: %3 ms").arg(m_command, m_role).arg(ms, 0, 'f', 1);
            emit measured(m_role, m_command, ms);
        }, Qt::QueuedConnection);
    }, Qt::DirectConnection);
}

void LatencyProbe::arm(const QString &command)
{
    // A hidden output never swaps; the next command simply re-arms it.
    m_command = command;
    m_synced = false;
    m_armedAtNs = m_clock.nsecsElapsed();
}
