#pragma once

class QWindow;

// Puts a borderless output window above the menu bar, Dock, and notification banners on its
// display. Qt's WindowStaysOnTopHint only reaches the floating level, which the menu bar
// (level 24) still covers. No-op off macOS.
void raiseAboveMenuBar(QWindow *window);
