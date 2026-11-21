#pragma once

#include <QObject>
#include <QTimer>

#include <chrono>

namespace visualis {

// Waits for the display setup to stop changing before outputs are moved. After a wake, a
// USB-C monitor was seen disconnecting and reconnecting about once a second for 12 s, and
// a newly added display reports an empty name at first (spike P1, #4). Moving outputs on
// every flicker would make it worse, so they move once, after `quiet` with no change.
class DisplaySettle : public QObject
{
    Q_OBJECT

public:
    explicit DisplaySettle(std::chrono::milliseconds quiet, QObject *parent = nullptr);

    // A display was added, removed, or changed: (re)start the quiet period.
    void changed();
    bool isSettling() const { return m_timer.isActive(); }

signals:
    void settlingChanged(bool settling);
    void settled();

private:
    QTimer m_timer;
};

} // namespace visualis
