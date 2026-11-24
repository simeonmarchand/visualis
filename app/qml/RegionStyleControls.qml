pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// One text region's look and place (plan 5.4), for the template editor (#94) and a slide's
// overrides in the song editor (#95). Properties are named by their template JSON paths
// ("color", "shadow.opacity", "frame.y"). Given `overridden`, those fields are highlighted
// and get a reset button; `mixed` marks the ones whose value differs between selected slides.
ColumnLayout {
    id: controls
    spacing: 8

    // With the template JSON's keys: style.frame.x, style.shadow.opacity...
    property var style: ({})
    // {id, name} per region; the picker shows when there's more than one.
    property var regions: []
    property string regionId
    property var overridden: []
    property var mixed: []

    signal regionChosen(string id)
    signal edited(string path, var value)
    signal resetRequested(string path)

    // {"shadow": {"opacity": v}} for "shadow.opacity", as TemplateEditor.setStyle takes.
    function nested(path, value) {
        const keys = path.split(".")
        let o = value
        for (let i = keys.length - 1; i >= 0; --i)
            o = {[keys[i]]: o}
        return o
    }

    ColorDialog {
        id: colorDialog
        property var apply: function (c) {}
        onAccepted: apply(selectedColor.toString())
    }

    // Highlighted when one of its paths is overridden; "(mixed)" when they differ.
    component FieldLabel: Label {
        id: fieldLabel
        property string label
        property var paths: []
        readonly property bool isOverridden: paths.some(p => controls.overridden.includes(p))
        readonly property bool isMixed: paths.some(p => controls.mixed.includes(p))
        text: label + (isMixed ? " (mixed)" : "")
        color: isOverridden ? "#ff9f1a" : "#d0d3d8"
        font.italic: isMixed
        Layout.preferredWidth: 90
    }

    component ResetButton: ToolButton {
        id: resetButton
        required property string path
        objectName: "reset_" + path
        visible: controls.overridden.includes(path)
        text: "↺"
        ToolTip.visible: hovered
        ToolTip.text: "Back to the template's"
        onClicked: controls.resetRequested(path)
    }

    // A labelled slider for a fraction or a multiple; `factor` turns it into what's shown.
    component StyleSlider: RowLayout {
        id: row
        required property string label
        required property string path
        required property real value
        property real from: 0
        property real to: 1
        property real factor: 100
        property string unit: "%"
        property int decimals: 1
        spacing: 6
        FieldLabel { label: row.label; paths: [row.path] }
        Slider {
            Layout.fillWidth: true
            from: row.from
            to: row.to
            value: row.value
            onMoved: controls.edited(row.path, value)
        }
        Label {
            text: (row.value * row.factor).toFixed(row.decimals) + row.unit
            color: "#8b8f96"
            Layout.preferredWidth: 50
        }
        ResetButton { path: row.path }
    }

    component Swatch: Rectangle {
        id: swatch
        required property string colorText
        signal chosen(string color)
        implicitWidth: 28
        implicitHeight: 22
        radius: 3
        color: colorText
        border.color: "#8b8f96"
        MouseArea {
            anchors.fill: parent
            onClicked: {
                colorDialog.selectedColor = swatch.colorText
                colorDialog.apply = c => swatch.chosen(c)
                colorDialog.open()
            }
        }
    }

    // A frame edge or size, as a percentage of the output.
    component FrameField: RowLayout {
        id: frameField
        required property string label
        required property string key
        property real fallback: 0
        FieldLabel { label: frameField.label; paths: ["frame." + frameField.key]; Layout.preferredWidth: 50 }
        PercentField {
            fraction: controls.style.frame?.[frameField.key] ?? frameField.fallback
            onFractionEdited: f => controls.edited("frame." + frameField.key, f)
        }
        ResetButton { path: "frame." + frameField.key }
    }

    ComboBox {
        Layout.fillWidth: true
        visible: controls.regions.length > 1
        model: controls.regions
        textRole: "name"
        valueRole: "id"
        currentIndex: indexOfValue(controls.regionId)
        onActivated: controls.regionChosen(currentValue)
    }

    Label { text: "Text"; color: "#d0d3d8"; font.bold: true }
    RowLayout {
        FieldLabel { label: "Font"; paths: ["fontFamily"] }
        ComboBox {
            objectName: "fontFamily"
            Layout.fillWidth: true
            model: Qt.fontFamilies()
            currentIndex: model.indexOf(controls.style.fontFamily ?? "")
            displayText: controls.style.fontFamily ?? ""
            onActivated: controls.edited("fontFamily", currentText)
        }
        ResetButton { path: "fontFamily" }
    }
    RowLayout {
        FieldLabel { label: "Weight"; paths: ["weight"] }
        ComboBox {
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: [
                {text: "Thin", value: 100}, {text: "Extra light", value: 200}, {text: "Light", value: 300},
                {text: "Regular", value: 400}, {text: "Medium", value: 500}, {text: "Semibold", value: 600},
                {text: "Bold", value: 700}, {text: "Extra bold", value: 800}, {text: "Black", value: 900}
            ]
            currentIndex: indexOfValue(controls.style.weight ?? 400)
            displayText: currentIndex >= 0 ? currentText : String(controls.style.weight ?? "")
            onActivated: controls.edited("weight", currentValue)
        }
        ResetButton { path: "weight" }
    }
    RowLayout {
        FieldLabel { label: "Color"; paths: ["color"] }
        Swatch {
            colorText: controls.style.color ?? "#ffffff"
            onChosen: c => controls.edited("color", c)
        }
        Item { Layout.fillWidth: true }
        ResetButton { path: "color" }
    }
    StyleSlider {
        label: "Largest size"
        path: "maxSize"
        value: controls.style.maxSize ?? 0
        from: 0.01; to: 0.25
    }
    StyleSlider {
        label: "Smallest size"
        path: "minSize"
        value: controls.style.minSize ?? 0
        from: 0.01; to: 0.25
    }
    StyleSlider {
        label: "Line height"
        path: "lineHeight"
        value: controls.style.lineHeight ?? 1
        from: 0.6; to: 2.5
        factor: 1; unit: "×"; decimals: 2
    }
    RowLayout {
        FieldLabel { label: "Align"; paths: ["align"] }
        Repeater {
            model: ["left", "center", "right"]
            delegate: Button {
                required property string modelData
                text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                checkable: true
                checked: controls.style.align === modelData
                onClicked: controls.edited("align", modelData)
            }
        }
        ResetButton { path: "align" }
    }
    RowLayout {
        FieldLabel { label: ""; paths: ["verticalAlign"] }
        Repeater {
            model: ["top", "center", "bottom"]
            delegate: Button {
                required property string modelData
                text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                checkable: true
                checked: controls.style.verticalAlign === modelData
                onClicked: controls.edited("verticalAlign", modelData)
            }
        }
        ResetButton { path: "verticalAlign" }
    }

    Label { text: "Shadow"; color: "#d0d3d8"; font.bold: true; Layout.topMargin: 8 }
    RowLayout {
        CheckBox {
            text: "Shadow"
            checked: controls.style.shadow?.enabled ?? false
            onClicked: controls.edited("shadow.enabled", checked)
        }
        ResetButton { path: "shadow.enabled" }
        FieldLabel { label: "Color"; paths: ["shadow.color"]; Layout.preferredWidth: 50 }
        Swatch {
            colorText: controls.style.shadow?.color ?? "#000000"
            onChosen: c => controls.edited("shadow.color", c)
        }
        ResetButton { path: "shadow.color" }
    }
    StyleSlider {
        label: "Opacity"
        path: "shadow.opacity"
        value: controls.style.shadow?.opacity ?? 0
        decimals: 0
    }
    StyleSlider {
        label: "Blur"
        path: "shadow.blur"
        value: controls.style.shadow?.blur ?? 0
        decimals: 0
    }
    StyleSlider {
        label: "Offset"
        path: "shadow.offset"
        value: controls.style.shadow?.offset ?? 0
        from: -0.02; to: 0.02; decimals: 2
    }

    Label { text: "Outline"; color: "#d0d3d8"; font.bold: true; Layout.topMargin: 8 }
    RowLayout {
        FieldLabel { label: "Color"; paths: ["outline.color"] }
        Swatch {
            colorText: controls.style.outline?.color ?? "#000000"
            onChosen: c => controls.edited("outline.color", c)
        }
        Item { Layout.fillWidth: true }
        ResetButton { path: "outline.color" }
    }
    StyleSlider {
        label: "Width"
        path: "outline.width"
        value: controls.style.outline?.width ?? 0
        from: 0; to: 0.01; decimals: 2
    }

    Label { text: "Position (of the output)"; color: "#d0d3d8"; font.bold: true; Layout.topMargin: 8 }
    GridLayout {
        columns: 2
        columnSpacing: 6
        FrameField { label: "Left"; key: "x" }
        FrameField { label: "Top"; key: "y" }
        FrameField { label: "Width"; key: "w"; fallback: 1 }
        FrameField { label: "Height"; key: "h"; fallback: 1 }
    }
}
