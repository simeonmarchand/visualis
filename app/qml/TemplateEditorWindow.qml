pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import Visualis.Ui

// Edit templates (#94, plan 5.4): the list, then the selected region's look and place,
// with a preview at the audience's shape over the live background. Drag the frame or its
// handles on the preview to place the region. Every change saves on its own.
Window {
    id: window
    objectName: "templateEditorWindow"
    title: "Templates"
    width: 1280
    height: 820
    minimumWidth: 1000
    minimumHeight: 620
    color: "#1e1f22"
    transientParent: null

    // The audience output's width / height; the operator window sets it.
    property real aspect: 16 / 9

    function open() {
        show()
        raise()
        requestActivate()
    }

    onClosing: editor.flush()

    TemplateEditor { id: editor }

    readonly property var style: editor.style

    ColorDialog {
        id: colorDialog
        property var apply: function (c) {}
        onAccepted: apply(selectedColor.toString())
    }

    Dialog {
        id: confirmDelete
        objectName: "confirmDeleteTemplate"
        title: "Delete \"" + editor.name + "\"?"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.Cancel
        Label { text: "Songs using it show with the built-in template instead." }
        onAccepted: editor.remove()
    }

    component FieldLabel: Label {
        color: "#d0d3d8"
        Layout.preferredWidth: 90
    }

    // A labelled slider for a fraction or a multiple; `factor` turns it into what's shown.
    component StyleSlider: RowLayout {
        id: row
        required property string label
        required property real value
        property real from: 0
        property real to: 1
        property real factor: 100
        property string unit: "%"
        property int decimals: 1
        signal moved(real value)
        spacing: 6
        FieldLabel { text: row.label }
        Slider {
            Layout.fillWidth: true
            from: row.from
            to: row.to
            value: row.value
            onMoved: row.moved(value)
        }
        Label {
            text: (row.value * row.factor).toFixed(row.decimals) + row.unit
            color: "#8b8f96"
            Layout.preferredWidth: 50
        }
    }

    // A percentage of the output, in tenths of a percent.
    component PercentField: SpinBox {
        id: field
        required property real fraction
        signal edited(real fraction)
        from: 0
        to: 1000
        stepSize: 5
        editable: true
        value: Math.round(field.fraction * 1000)
        textFromValue: (v, locale) => (v / 10).toFixed(1) + "%"
        valueFromText: (text, locale) => Math.round(parseFloat(text) * 10)
        onValueModified: edited(value / 1000)
        Layout.preferredWidth: 110
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

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        // The templates.
        ColumnLayout {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            spacing: 8
            Label { text: "Templates"; color: "#d0d3d8"; font.bold: true }
            ListView {
                objectName: "templateList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: TemplateLibrary.entries
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.name + (modelData.builtIn ? " (built in)" : "")
                    highlighted: modelData.id === editor.templateId
                    onClicked: editor.templateId = modelData.id
                }
            }
            RowLayout {
                spacing: 6
                Button { text: "New"; onClicked: editor.create("New template") }
                Button { text: "Duplicate"; onClicked: editor.duplicate() }
                Button { text: "Delete…"; enabled: !editor.builtIn; onClicked: confirmDelete.open() }
            }
            TextField {
                objectName: "templateName"
                Layout.fillWidth: true
                enabled: !editor.builtIn
                placeholderText: "Name"
                maximumLength: 200   // templatelimits::kTextLength
                text: editor.name
                onEditingFinished: editor.rename(text)
            }
        }

        // The preview, and the sample text it shows.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Label {
                objectName: "templateProblems"
                Layout.fillWidth: true
                visible: text !== ""
                text: [editor.error].concat(editor.warnings).filter(s => s !== "").join("\n")
                wrapMode: Text.WordWrap
                color: "#e0b040"
            }
            Label {
                visible: editor.builtIn
                text: "The built-in template can't be changed. Duplicate it to make one you can."
                color: "#8b8f96"
            }

            Item {
                id: stageArea
                Layout.fillWidth: true
                Layout.fillHeight: true

                Rectangle {
                    id: stage
                    objectName: "templatePreview"
                    readonly property real fit: Math.min(stageArea.width, stageArea.height * window.aspect)
                    width: fit
                    height: fit / window.aspect
                    anchors.centerIn: parent
                    color: "black"
                    clip: true

                    // What's on the audience's background now, if anything.
                    VideoOutput {
                        id: background
                        anchors.fill: parent
                        fillMode: VideoOutput.PreserveAspectCrop
                        visible: VideoEngine.source.toString() !== "" && !LiveState.backgroundCleared
                        Component.onCompleted: VideoEngine.addTarget(background.videoSink)
                    }
                    AudienceLyrics {
                        anchors.fill: parent
                        content: editor.preview
                        showsLive: false
                    }

                    // The safe area: a guide only, not drawn on the outputs.
                    Rectangle {
                        objectName: "safeAreaGuide"
                        x: stage.width * editor.safeAreaX
                        y: stage.height * editor.safeAreaY
                        width: stage.width * (1 - 2 * editor.safeAreaX)
                        height: stage.height * (1 - 2 * editor.safeAreaY)
                        color: "transparent"
                        border.color: "#6040c0ff"
                        border.width: 1
                    }

                    // The region's frame: drag inside to move it, a handle to resize it.
                    Item {
                        id: regionFrame
                        objectName: "regionFrame"
                        readonly property var frame: window.style.frame ?? {x: 0, y: 0, w: 1, h: 1}
                        x: stage.width * frame.x
                        y: stage.height * frame.y
                        width: stage.width * frame.w
                        height: stage.height * frame.h
                        enabled: !editor.builtIn

                        property rect dragStart
                        property point pressAt

                        // In window coordinates, so the preview re-centring mid-drag doesn't move the frame.
                        function begin(area, mouse) {
                            dragStart = Qt.rect(frame.x, frame.y, frame.w, frame.h)
                            pressAt = area.mapToItem(null, mouse.x, mouse.y)
                        }
                        function drag(area, mouse, edges) {
                            const p = area.mapToItem(null, mouse.x, mouse.y)
                            editor.dragFrame(dragStart, edges, (p.x - pressAt.x) / stage.width, (p.y - pressAt.y) / stage.height)
                        }

                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: "#ff9f1a"
                            border.width: 1
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeAllCursor
                            onPressed: mouse => regionFrame.begin(this, mouse)
                            onPositionChanged: mouse => regionFrame.drag(this, mouse, Qt.LeftEdge | Qt.TopEdge | Qt.RightEdge | Qt.BottomEdge)
                        }
                        Repeater {
                            model: [
                                {edges: Qt.LeftEdge | Qt.TopEdge, fx: 0, fy: 0, cursor: Qt.SizeFDiagCursor},
                                {edges: Qt.TopEdge, fx: 0.5, fy: 0, cursor: Qt.SizeVerCursor},
                                {edges: Qt.RightEdge | Qt.TopEdge, fx: 1, fy: 0, cursor: Qt.SizeBDiagCursor},
                                {edges: Qt.RightEdge, fx: 1, fy: 0.5, cursor: Qt.SizeHorCursor},
                                {edges: Qt.RightEdge | Qt.BottomEdge, fx: 1, fy: 1, cursor: Qt.SizeFDiagCursor},
                                {edges: Qt.BottomEdge, fx: 0.5, fy: 1, cursor: Qt.SizeVerCursor},
                                {edges: Qt.LeftEdge | Qt.BottomEdge, fx: 0, fy: 1, cursor: Qt.SizeBDiagCursor},
                                {edges: Qt.LeftEdge, fx: 0, fy: 0.5, cursor: Qt.SizeHorCursor}
                            ]
                            delegate: Rectangle {
                                id: handle
                                required property var modelData
                                width: 10
                                height: 10
                                x: regionFrame.width * modelData.fx - width / 2
                                y: regionFrame.height * modelData.fy - height / 2
                                color: "#ff9f1a"
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    cursorShape: handle.modelData.cursor
                                    onPressed: mouse => regionFrame.begin(this, mouse)
                                    onPositionChanged: mouse => regionFrame.drag(this, mouse, handle.modelData.edges)
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { text: "Sample text"; color: "#d0d3d8" }
                TextArea {
                    objectName: "sampleText"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70
                    wrapMode: TextArea.Wrap
                    text: editor.sampleText
                    onTextChanged: editor.sampleText = text
                }
                Button {
                    text: "Long lines"
                    onClicked: editor.sampleText = "Through many dangers, toils and snares, I have already come\n"
                                                   + "'Tis grace hath brought me safe thus far, and grace will lead me home\n"
                                                   + "The Lord has promised good to me, His word my hope secures\n"
                                                   + "He will my shield and portion be, as long as life endures"
                }
            }
        }

        // The selected region's properties.
        ScrollView {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            enabled: !editor.builtIn
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 8

                ComboBox {
                    Layout.fillWidth: true
                    visible: editor.regions.length > 1
                    model: editor.regions
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: indexOfValue(editor.regionId)
                    onActivated: editor.regionId = currentValue
                }

                Label { text: "Text"; color: "#d0d3d8"; font.bold: true }
                RowLayout {
                    FieldLabel { text: "Font" }
                    ComboBox {
                        objectName: "fontFamily"
                        Layout.fillWidth: true
                        model: Qt.fontFamilies()
                        currentIndex: model.indexOf(window.style.fontFamily ?? "")
                        displayText: window.style.fontFamily ?? ""
                        onActivated: editor.setStyle({fontFamily: currentText})
                    }
                }
                RowLayout {
                    FieldLabel { text: "Weight" }
                    ComboBox {
                        Layout.fillWidth: true
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: "Thin", value: 100}, {text: "Extra light", value: 200}, {text: "Light", value: 300},
                            {text: "Regular", value: 400}, {text: "Medium", value: 500}, {text: "Semibold", value: 600},
                            {text: "Bold", value: 700}, {text: "Extra bold", value: 800}, {text: "Black", value: 900}
                        ]
                        currentIndex: indexOfValue(window.style.weight ?? 400)
                        displayText: currentIndex >= 0 ? currentText : String(window.style.weight ?? "")
                        onActivated: editor.setStyle({weight: currentValue})
                    }
                }
                RowLayout {
                    FieldLabel { text: "Color" }
                    Swatch {
                        colorText: window.style.color ?? "#ffffff"
                        onChosen: c => editor.setStyle({color: c})
                    }
                }
                StyleSlider {
                    label: "Largest size"
                    value: window.style.maxSize ?? 0
                    from: 0.01; to: 0.25
                    onMoved: v => editor.setStyle({maxSize: v})
                }
                StyleSlider {
                    label: "Smallest size"
                    value: window.style.minSize ?? 0
                    from: 0.01; to: 0.25
                    onMoved: v => editor.setStyle({minSize: v})
                }
                StyleSlider {
                    label: "Line height"
                    value: window.style.lineHeight ?? 1
                    from: 0.6; to: 2.5
                    factor: 1; unit: "×"; decimals: 2
                    onMoved: v => editor.setStyle({lineHeight: v})
                }
                RowLayout {
                    FieldLabel { text: "Align" }
                    Repeater {
                        model: ["left", "center", "right"]
                        delegate: Button {
                            required property string modelData
                            text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                            checkable: true
                            checked: window.style.align === modelData
                            onClicked: editor.setStyle({align: modelData})
                        }
                    }
                }
                RowLayout {
                    FieldLabel { text: "" }
                    Repeater {
                        model: ["top", "center", "bottom"]
                        delegate: Button {
                            required property string modelData
                            text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                            checkable: true
                            checked: window.style.verticalAlign === modelData
                            onClicked: editor.setStyle({verticalAlign: modelData})
                        }
                    }
                }

                Label { text: "Shadow"; color: "#d0d3d8"; font.bold: true; Layout.topMargin: 8 }
                RowLayout {
                    CheckBox {
                        text: "Shadow"
                        checked: window.style.shadow?.enabled ?? false
                        onClicked: editor.setStyle({shadow: {enabled: checked}})
                    }
                    Swatch {
                        colorText: window.style.shadow?.color ?? "#000000"
                        onChosen: c => editor.setStyle({shadow: {color: c}})
                    }
                }
                StyleSlider {
                    label: "Opacity"
                    value: window.style.shadow?.opacity ?? 0
                    decimals: 0
                    onMoved: v => editor.setStyle({shadow: {opacity: v}})
                }
                StyleSlider {
                    label: "Blur"
                    value: window.style.shadow?.blur ?? 0
                    decimals: 0
                    onMoved: v => editor.setStyle({shadow: {blur: v}})
                }
                StyleSlider {
                    label: "Offset"
                    value: window.style.shadow?.offset ?? 0
                    from: -0.02; to: 0.02; decimals: 2
                    onMoved: v => editor.setStyle({shadow: {offset: v}})
                }

                Label { text: "Outline"; color: "#d0d3d8"; font.bold: true; Layout.topMargin: 8 }
                RowLayout {
                    FieldLabel { text: "Color" }
                    Swatch {
                        colorText: window.style.outline?.color ?? "#000000"
                        onChosen: c => editor.setStyle({outline: {color: c}})
                    }
                }
                StyleSlider {
                    label: "Width"
                    value: window.style.outline?.width ?? 0
                    from: 0; to: 0.01; decimals: 2
                    onMoved: v => editor.setStyle({outline: {width: v}})
                }

                Label { text: "Position (of the output)"; color: "#d0d3d8"; font.bold: true; Layout.topMargin: 8 }
                GridLayout {
                    columns: 4
                    columnSpacing: 6
                    Label { text: "Left"; color: "#d0d3d8" }
                    PercentField { fraction: window.style.frame?.x ?? 0; onEdited: f => editor.setStyle({frame: {x: f}}) }
                    Label { text: "Top"; color: "#d0d3d8" }
                    PercentField { fraction: window.style.frame?.y ?? 0; onEdited: f => editor.setStyle({frame: {y: f}}) }
                    Label { text: "Width"; color: "#d0d3d8" }
                    PercentField { fraction: window.style.frame?.w ?? 1; onEdited: f => editor.setStyle({frame: {w: f}}) }
                    Label { text: "Height"; color: "#d0d3d8" }
                    PercentField { fraction: window.style.frame?.h ?? 1; onEdited: f => editor.setStyle({frame: {h: f}}) }
                }

                Label { text: "Safe area (each side)"; color: "#d0d3d8"; font.bold: true; Layout.topMargin: 8 }
                GridLayout {
                    columns: 4
                    columnSpacing: 6
                    Label { text: "Sides"; color: "#d0d3d8" }
                    PercentField { to: Math.round(editor.maxSafeArea * 1000); fraction: editor.safeAreaX; onEdited: f => editor.setSafeArea(f, editor.safeAreaY) }
                    Label { text: "Top, bottom"; color: "#d0d3d8" }
                    PercentField { to: Math.round(editor.maxSafeArea * 1000); fraction: editor.safeAreaY; onEdited: f => editor.setSafeArea(editor.safeAreaX, f) }
                }
            }
        }
    }
}
