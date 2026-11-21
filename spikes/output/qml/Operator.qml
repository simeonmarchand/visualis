import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Visualis.Spike

ApplicationWindow {
    id: root

    width: 1180
    height: 720
    visible: true
    title: "Visualis: Operator (P1 output spike)"

    onClosing: Qt.quit()   // output windows would otherwise keep the app alive

    function clearAll() {
        LiveState.clearText()
        VideoEngine.backgroundVisible = false
    }

    // Application-wide so they work whichever window has focus. Buttons and combos
    // don't take focus, so Space and the arrow keys always reach these.
    Shortcut { sequences: ["Right", "Down", "Space", "PgDown"]; context: Qt.ApplicationShortcut; onActivated: LiveState.next() }
    Shortcut { sequences: ["Left", "Up", "PgUp"]; context: Qt.ApplicationShortcut; onActivated: LiveState.prev() }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; onActivated: LiveState.clearText() }
    Shortcut { sequence: "B"; context: Qt.ApplicationShortcut; onActivated: VideoEngine.backgroundVisible = !VideoEngine.backgroundVisible }
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut; onActivated: root.clearAll() }

    FileDialog {
        id: videoDialog
        title: "Choose background video"
        nameFilters: ["Video (*.mp4 *.mov *.m4v)", "All files (*)"]
        onAccepted: VideoEngine.source = selectedFile
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 16

        // Slide list
        ListView {
            id: slideList
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: LiveState.slides
            currentIndex: LiveState.index
            delegate: Rectangle {
                required property int index
                required property string modelData
                width: ListView.view.width
                height: label.implicitHeight + 16
                radius: 4
                color: index === LiveState.index ? (LiveState.textCleared ? "#7a5" : "#2a6df4") : "#2b2b2b"
                Text {
                    id: label
                    anchors.fill: parent
                    anchors.margins: 8
                    text: (index + 1) + ".  " + modelData
                    color: "white"
                    wrapMode: Text.WordWrap
                }
                MouseArea { anchors.fill: parent; onClicked: LiveState.goTo(index) }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            RowLayout {
                spacing: 8
                Button { text: "◀ Previous"; focusPolicy: Qt.NoFocus; onClicked: LiveState.prev() }
                Button { text: "Next ▶"; focusPolicy: Qt.NoFocus; onClicked: LiveState.next() }
                Button { text: "Clear text (C)"; focusPolicy: Qt.NoFocus; onClicked: LiveState.clearText() }
                Button {
                    text: VideoEngine.backgroundVisible ? "Clear background (B)" : "Show background (B)"
                    focusPolicy: Qt.NoFocus
                    onClicked: VideoEngine.backgroundVisible = !VideoEngine.backgroundVisible
                }
                Button { text: "Clear all (Esc)"; focusPolicy: Qt.NoFocus; onClicked: root.clearAll() }
            }

            Label {
                Layout.fillWidth: true
                text: "Next: " + (LiveState.nextText || "(end)")
                wrapMode: Text.WordWrap
                opacity: 0.7
            }

            GroupBox {
                title: "Displays" + (OutputController.windowed ? " (windowed mode)" : "")
                Layout.fillWidth: true
                GridLayout {
                    anchors.fill: parent
                    columns: 3
                    Repeater {
                        model: ["audience", "stage"]
                        delegate: RowLayout {
                            required property string modelData
                            Layout.columnSpan: 3
                            Label { text: modelData; Layout.preferredWidth: 70 }
                            ComboBox {
                                Layout.preferredWidth: 360
                                focusPolicy: Qt.NoFocus
                                model: OutputController.screens
                                textRole: "label"
                                valueRole: "key"
                                currentIndex: indexOfValue(OutputController.assignments[modelData] || "")
                                displayText: currentIndex < 0 ? "(missing) " + (OutputController.assignments[modelData] || "none") : currentText
                                onActivated: OutputController.assign(modelData, currentValue)
                            }
                            Label { text: OutputController.placement[modelData] || ""; opacity: 0.7 }
                        }
                    }
                }
            }

            GroupBox {
                title: "Video"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    RowLayout {
                        Button { text: "Choose file…"; focusPolicy: Qt.NoFocus; onClicked: videoDialog.open() }
                        ComboBox {
                            focusPolicy: Qt.NoFocus
                            model: ["shared", "per-window"]
                            currentIndex: model.indexOf(VideoEngine.mode)
                            onActivated: VideoEngine.mode = currentText
                        }
                        Label { text: VideoEngine.status }
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: VideoEngine.source.toString() || "(no file)"
                        opacity: 0.7
                    }
                    Label {
                        text: VideoEngine.lastLoopGapMs < 0 ? "Loop gap: not looped yet"
                                                            : "Loop gap: " + VideoEngine.lastLoopGapMs.toFixed(1) + " ms"
                    }
                }
            }

            GroupBox {
                title: "Frame stats (per second)"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Label {
                        text: "CPU " + FrameStats.cpuPercent.toFixed(1) + "%   memory " + FrameStats.rssMb.toFixed(0) + " MB"
                    }
                    Repeater {
                        model: FrameStats.windows
                        delegate: Label {
                            required property var modelData
                            font.family: "Menlo"
                            text: !modelData.visible ? modelData.name + ": hidden"
                                : "%1: %2 fps @ %3 Hz   late %4 (total %5 of %6)   worst %7 ms   cmd→swap %8 ms"
                                    .arg(modelData.name).arg(modelData.fps).arg(modelData.refreshHz.toFixed(0))
                                    .arg(modelData.late).arg(modelData.totalLate).arg(modelData.totalFrames)
                                    .arg(modelData.maxIntervalMs.toFixed(1))
                                    .arg(modelData.latencyMs < 0 ? "–" : modelData.latencyMs.toFixed(1))
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
