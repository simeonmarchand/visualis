pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
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

                RegionStyleControls {
                    id: regionControls
                    Layout.fillWidth: true
                    style: window.style
                    regions: editor.regions
                    regionId: editor.regionId
                    onRegionChosen: id => editor.regionId = id
                    onEdited: (path, value) => editor.setStyle(regionControls.nested(path, value))
                }

                Label { text: "Safe area (each side)"; color: "#d0d3d8"; font.bold: true; Layout.topMargin: 8 }
                GridLayout {
                    columns: 4
                    columnSpacing: 6
                    Label { text: "Sides"; color: "#d0d3d8" }
                    PercentField { to: Math.round(editor.maxSafeArea * 1000); fraction: editor.safeAreaX; onFractionEdited: f => editor.setSafeArea(f, editor.safeAreaY) }
                    Label { text: "Top, bottom"; color: "#d0d3d8" }
                    PercentField { to: Math.round(editor.maxSafeArea * 1000); fraction: editor.safeAreaY; onFractionEdited: f => editor.setSafeArea(editor.safeAreaX, f) }
                }
            }
        }
    }
}
