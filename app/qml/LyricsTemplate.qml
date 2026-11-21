pragma Singleton
import QtQuick

// The built-in lyrics template (Phase 1). Sizes are fractions of the output's height and
// margins fractions of its width and height, so one template fits any resolution. The
// template editor and stored templates arrive in Phase 3 (plan 5.4).
QtObject {
    // Text
    readonly property string fontFamily: "Helvetica Neue"
    readonly property int fontWeight: Font.DemiBold
    readonly property color textColor: "white"
    readonly property real maxTextSize: 0.085   // the size a short slide gets
    readonly property real minTextSize: 0.035   // shrink-to-fit stops here, then the last line elides
    readonly property real lineHeight: 1.1

    // Safe area: the share of each edge kept clear (TVs and projectors crop the edges)
    readonly property real safeAreaX: 0.06
    readonly property real safeAreaY: 0.08

    // Shadow, for legibility over moving video
    readonly property bool shadow: true
    readonly property color shadowColor: "black"
    readonly property real shadowOpacity: 0.85
    readonly property real shadowBlur: 0.5          // 0–1, MultiEffect's scale
    readonly property real shadowOffset: 0.003      // of the output's height, down and right

    // Slide changes crossfade, so the screen never flashes blank between slides
    readonly property int fadeMs: 300
    readonly property int backgroundFadeMs: 600
}
