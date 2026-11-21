#pragma once

#include <QString>
#include <QtGlobal>

class QScreen;
class QWindow;

// Platform-specific display and window code (macOS in platformdisplay_mac.mm).

// A display's stable identity. Qt reports only a name for the monitors tested (no serial,
// manufacturer, or model), so on macOS this is CoreGraphics' vendor:model:serial from the
// monitor's EDID. Two identical monitors without serials share a key (#20 handles that).
// Elsewhere, and if no match is found, it's the name.
QString displayKey(const QScreen *screen);

// Keeps the displays from idle-sleeping while it's active: the spike saw display sleep
// stall both outputs for 1.1 s (virtual-displays note, finding 8). On macOS this holds an
// IOPMAssertion (PreventUserIdleDisplaySleep), shown in `pmset -g assertions`. Releasing it
// lets the system sleep the displays as usual. No-op off macOS.
class DisplaySleepBlocker
{
public:
    DisplaySleepBlocker() = default;
    ~DisplaySleepBlocker() { setActive(false); }
    DisplaySleepBlocker(const DisplaySleepBlocker &) = delete;
    DisplaySleepBlocker &operator=(const DisplaySleepBlocker &) = delete;

    // Returns false if the system refused (the show goes on; the displays may sleep).
    bool setActive(bool active);
    bool isActive() const { return m_id != 0; }

private:
    quint32 m_id = 0;   // the assertion's id; 0 when none is held
};

// Puts a borderless output window above the menu bar, Dock, and notification banners on
// its display. Qt's WindowStaysOnTopHint only reaches the floating level, which the menu
// bar (level 24) still covers. No-op off macOS.
void raiseAboveMenuBar(QWindow *window);
