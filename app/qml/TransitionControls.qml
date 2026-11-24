import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One layer's transition (#96, plan 5.4): a cut, or a fade of some length. For a template's
// layers in the template editor and a slide's override in the song editor, which passes
// `overridden` (highlighted, with a reset) and `mixed` (selected slides differ).
RowLayout {
    id: controls
    spacing: 6

    required property string label
    // As in the template JSON: {kind: "cut" | "fade", ms}.
    property var transition: ({kind: "cut", ms: 300})
    property bool overridden: false
    property bool mixed: false
    required property int maxMs

    signal edited(string kind, int ms)
    signal resetRequested()

    Label {
        text: controls.label + (controls.mixed ? " (mixed)" : "")
        color: controls.overridden ? "#ff9f1a" : "#d0d3d8"
        font.italic: controls.mixed
        Layout.preferredWidth: 90
    }
    ComboBox {
        id: kind
        objectName: "transitionKind"
        model: [{value: "cut", text: "Cut"}, {value: "fade", text: "Fade"}]
        textRole: "text"
        valueRole: "value"
        currentIndex: indexOfValue(controls.transition.kind ?? "cut")
        onActivated: controls.edited(currentValue, length.value)
    }
    SpinBox {
        id: length
        objectName: "transitionMs"
        enabled: kind.currentValue === "fade"
        from: 0
        to: controls.maxMs
        stepSize: 50
        editable: true
        value: controls.transition.ms ?? 300
        textFromValue: v => v + " ms"
        valueFromText: t => parseInt(t)
        onValueModified: controls.edited(kind.currentValue, value)
    }
    ToolButton {
        objectName: "resetTransition"
        visible: controls.overridden
        text: "↺"
        ToolTip.visible: hovered
        ToolTip.text: "Back to the template's"
        onClicked: controls.resetRequested()
    }
}
