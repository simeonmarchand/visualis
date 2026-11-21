#include "macwindow.h"

#include <QWindow>
#import <AppKit/AppKit.h>

void raiseAboveMenuBar(QWindow *window)
{
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    // Above NSMainMenuWindowLevel; NSStatusWindowLevel is the conventional choice for this.
    view.window.level = NSStatusWindowLevel;
    // Stay on this display in every Space, and don't follow the operator into Mission Control.
    view.window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                                   | NSWindowCollectionBehaviorStationary
                                   | NSWindowCollectionBehaviorIgnoresCycle;
}
