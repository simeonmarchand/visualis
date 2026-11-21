#include "output/platformdisplay.h"

#include <QScreen>

QString displayKey(const QScreen *screen)
{
    return screen->name();
}

void raiseAboveMenuBar(QWindow *) {}
