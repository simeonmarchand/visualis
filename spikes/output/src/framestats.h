#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantList>
#include <memory>
#include <vector>

// Per-output frame timing, process CPU and memory, logged once a second to frames.csv.
// Frame callbacks arrive on the render thread, so per-window counters sit behind a mutex.
class FrameStats : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList windows READ windows NOTIFY updated)
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY updated)
    Q_PROPERTY(double rssMb READ rssMb NOTIFY updated)

public:
    FrameStats(const QString &videoMode, const QString &backend, QObject *parent = nullptr);

    static FrameStats *instance() { return s_instance; }
    static FrameStats *create(QQmlEngine *, QJSEngine *engine);

    void track(QQuickWindow *window, const QString &name);
    // Call when a window is (re)placed: resets interval timing so a hidden
    // period doesn't count as a dropped frame, and picks up the new refresh rate.
    void resetTiming(QQuickWindow *window);
    void setVideoMode(const QString &mode) { m_videoMode = mode; }

    QVariantList windows() const { return m_snapshot; }
    double cpuPercent() const { return m_cpuPercent; }
    double rssMb() const { return m_rssMb; }

signals:
    void updated();

private:
    struct Tracked {
        QPointer<QQuickWindow> window;
        QString name;
        QMutex mutex;
        double refreshHz = 60;
        qint64 lastSwapNs = 0;
        qint64 totalFrames = 0;
        qint64 totalLate = 0;
        int frames = 0;      // this second
        int late = 0;        // this second
        double maxIntervalMs = 0;
        double lastLatencyMs = -1;
        qint64 commandAtNs = 0;   // pending command, 0 = none
        bool commandSynced = false;
        int exposedState = -1;    // GUI thread only; -1 = not logged yet
    };

    void onFrameSwapped(Tracked *t);
    void onCommand();
    void tick();
    static double processCpuSeconds();
    static double processRssMb();

    static FrameStats *s_instance;
    std::vector<std::unique_ptr<Tracked>> m_tracked;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QFile m_csv;
    QString m_videoMode;
    QString m_backend;
    QVariantList m_snapshot;
    double m_lastCpuSeconds = 0;
    qint64 m_lastTickNs = 0;
    double m_cpuPercent = 0;
    double m_rssMb = 0;
};
