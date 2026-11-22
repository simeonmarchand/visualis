#include "output/platformdisplay.h"

#include <QScreen>
#include <QWindow>

#import <AppKit/AppKit.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

bool DisplaySleepBlocker::setActive(bool active)
{
    if (active == isActive())
        return true;
    if (!active) {
        IOPMAssertionRelease(m_id);
        m_id = 0;
        return true;
    }
    IOPMAssertionID id = kIOPMNullAssertionID;
    const IOReturn result = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleDisplaySleep,
                                                        kIOPMAssertionLevelOn,
                                                        CFSTR("Visualis is presenting to an output display"), &id);
    if (result != kIOReturnSuccess)
        return false;
    m_id = id;
    return true;
}

DisplaySleepWatcher::DisplaySleepWatcher(QObject *parent)
    : QObject(parent)
{
    // Asleep already if every online display is (the app started on a sleeping Mac).
    constexpr uint32_t maxDisplays = 32;
    CGDirectDisplayID ids[maxDisplays];
    uint32_t count = 0;
    if (CGGetOnlineDisplayList(maxDisplays, ids, &count) == kCGErrorSuccess && count > 0) {
        m_asleep = true;
        for (uint32_t i = 0; i < count; ++i)
            m_asleep = m_asleep && CGDisplayIsAsleep(ids[i]);
    }

    NSNotificationCenter *center = NSWorkspace.sharedWorkspace.notificationCenter;
    const auto observe = [&](NSNotificationName name, bool asleep) {
        id observer = [center addObserverForName:name object:nil queue:NSOperationQueue.mainQueue
                                      usingBlock:^(NSNotification *) { setAsleep(asleep); }];
        m_observers.append([observer retain]);
    };
    observe(NSWorkspaceScreensDidSleepNotification, true);
    observe(NSWorkspaceScreensDidWakeNotification, false);
}

DisplaySleepWatcher::~DisplaySleepWatcher()
{
    NSNotificationCenter *center = NSWorkspace.sharedWorkspace.notificationCenter;
    for (void *observer : m_observers) {
        [center removeObserver:static_cast<id>(observer)];
        [static_cast<id>(observer) release];
    }
}

void DisplaySleepWatcher::setAsleep(bool asleep)
{
    if (asleep == m_asleep)
        return;
    m_asleep = asleep;
    emit displaysAsleepChanged(asleep);
}

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
