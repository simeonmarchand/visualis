#include "output/displaysettle.h"

namespace visualis {

DisplaySettle::DisplaySettle(std::chrono::milliseconds quiet, QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(quiet);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        emit settlingChanged(false);
        emit settled();
    });
}

void DisplaySettle::changed()
{
    const bool wasSettling = m_timer.isActive();
    m_timer.start();
    if (!wasSettling)
        emit settlingChanged(true);
}

} // namespace visualis
