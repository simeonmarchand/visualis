#pragma once

#include <QString>

class QScreen;
class QWindow;

// Platform-specific display and window code (macOS in platformdisplay_mac.mm).

// A display's stable identity. Qt reports only a name for the monitors tested (no serial,
// manufacturer, or model), so on macOS this is CoreGraphics' vendor:model:serial from the
// monitor's EDID. Two identical monitors without serials share a key (#20 handles that).
// Elsewhere, and if no match is found, it's the name.
QString displayKey(const QScreen *screen);

// Puts a borderless output window above the menu bar, Dock, and notification banners on
// its display. Qt's WindowStaysOnTopHint only reaches the floating level, which the menu
// bar (level 24) still covers. No-op off macOS.
void raiseAboveMenuBar(QWindow *window);
