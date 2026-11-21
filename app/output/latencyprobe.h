#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>

#include <atomic>

// Times an operator command to the first frame an output shows with the change (plan 5.8,
// #24 acceptance: under 50 ms). As in the P1 spike, that's the first frame swapped after a
// scene-graph sync that started after the command. It doesn't include scan-out or
// projector lag; film the screen for cue-to-photon time.
class LatencyProbe : public QObject
{
    Q_OBJECT

public:
    LatencyProbe(QQuickWindow *window, const QString &role, QObject *parent = nullptr);

    // A command just changed the live state (GUI thread).
    void arm(const QString &command);

signals:
    void measured(const QString &role, const QString &command, double ms);

private:
    QPointer<QQuickWindow> m_window;
    QString m_role;
    QString m_command;   // GUI thread only
    QElapsedTimer m_clock;
    // Render thread reads these (the GUI thread is blocked during sync, not during swap).
    std::atomic<qint64> m_armedAtNs{0};
    std::atomic<bool> m_synced{false};
};
