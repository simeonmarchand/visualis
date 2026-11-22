#include "output/platformdisplay.h"

#include <QScreen>

QString displayKey(const QScreen *screen)
{
    return screen->name();
}

void raiseAboveMenuBar(QWindow *) {}

bool DisplaySleepBlocker::setActive(bool)
{
    return true;   // not implemented off macOS yet
}

DisplaySleepWatcher::DisplaySleepWatcher(QObject *parent) : QObject(parent) {}
DisplaySleepWatcher::~DisplaySleepWatcher() = default;
void DisplaySleepWatcher::setAsleep(bool) {}
