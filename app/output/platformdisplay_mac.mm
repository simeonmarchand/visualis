#include "output/platformdisplay.h"

#include <QScreen>
#include <QWindow>

#import <AppKit/AppKit.h>

QString displayKey(const QScreen *screen)
{
    // QScreen geometry and CGDisplayBounds are both in global points with the origin at the
    // top-left of the main display, so the bounds find the CoreGraphics display for a QScreen.
    constexpr uint32_t maxDisplays = 32;
    CGDirectDisplayID ids[maxDisplays];
    uint32_t count = 0;
    if (CGGetActiveDisplayList(maxDisplays, ids, &count) == kCGErrorSuccess) {
        for (uint32_t i = 0; i < count; ++i) {
            const CGRect b = CGDisplayBounds(ids[i]);
            if (QRectF(b.origin.x, b.origin.y, b.size.width, b.size.height).toRect() != screen->geometry())
                continue;
            // From the monitor's EDID, so it survives replugs, restarts, and other ports. Not
            // the display UUID: a replugged virtual display got a new one depending on what
            // else was connected (#19). Displays that report no vendor or model (AirPlay,
            // Sidecar) fall back to the name.
            const uint32_t vendor = CGDisplayVendorNumber(ids[i]);
            const uint32_t model = CGDisplayModelNumber(ids[i]);
            if (vendor == 0 && model == 0)
                break;
            return QStringLiteral("%1:%2:%3").arg(vendor).arg(model).arg(CGDisplaySerialNumber(ids[i]));
        }
    }
    return screen->name();
}

void raiseAboveMenuBar(QWindow *window)
{
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    view.window.level = NSStatusWindowLevel;
    // Stay on this display in every Space, and don't follow the operator into Mission Control.
    view.window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                                   | NSWindowCollectionBehaviorStationary
                                   | NSWindowCollectionBehaviorIgnoresCycle;
}
