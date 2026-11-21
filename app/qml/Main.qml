import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Visualis.Ui

// Operator window. Song list, slide grid, and controls arrive in #23 and #24.
Window {
    id: root
    objectName: "operatorWindow"
    title: "Visualis"
    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    color: "#1e1f22"

    onClosing: Qt.quit()   // visible output windows would otherwise keep the app running

    OutputWindow { id: audienceOutput; role: "audience" }
    OutputWindow { id: stageOutput; role: "stage" }

    Component.onCompleted: {
        OutputController.setOperatorWindow(root)
        OutputController.addOutput("audience", audienceOutput)
        OutputController.addOutput("stage", stageOutput)
    }

    FileDialog {
        id: backgroundDialog
        title: "Choose a background video"
        nameFilters: ["Video (*.mp4 *.mov *.m4v)", "All files (*)"]
        onAccepted: VideoEngine.source = selectedFile
    }

    Text {
        anchors.centerIn: parent
        text: "Visualis"
        color: "#8b8f96"
        font.pixelSize: 28
    }

    // Display choice for each output. A minimal picker until the operator window's real
    // layout (#23).
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        spacing: 8

        // Background video: a stopgap until the media library and service items.
        RowLayout {
            spacing: 12

            Label {
                text: "Background"
                color: "#d0d3d8"
                Layout.preferredWidth: 80
            }
            Button {
                text: "Choose video…"
                onClicked: backgroundDialog.open()
            }
            Button {
                text: "None"
                enabled: VideoEngine.source.toString() !== ""
                onClicked: VideoEngine.source = ""
            }
            Label {
                objectName: "backgroundStatus"
                text: VideoEngine.status
                color: "#8b8f96"
            }
        }

        Label {
            objectName: "backgroundAlert"
            visible: VideoEngine.alert !== ""
            text: VideoEngine.alert
            color: "#e0b040"
        }

        RowLayout {
            objectName: "identicalDisplays"
            visible: OutputController.identicalDisplays.length > 0
            spacing: 12

            Label {
                text: "Identical displays (" + OutputController.identicalDisplays.join(", ")
                      + ") are told apart by position. Check each output is on the right one."
                color: "#e0b040"
            }
            Button {
                text: "They're right"
                onClicked: OutputController.confirmIdenticalDisplays()
            }
        }

        Label {
            objectName: "displaysSettling"
            visible: OutputController.settling
            text: "Displays are reconnecting…"
            color: "#e0b040"
        }

        Repeater {
            model: [{ role: "audience", title: "Audience" }, { role: "stage", title: "Stage" }]

            delegate: RowLayout {
                id: row
                required property var modelData
                spacing: 12

                Label {
                    text: row.modelData.title
                    color: "#d0d3d8"
                    Layout.preferredWidth: 80
                }
                ComboBox {
                    id: choice
                    objectName: row.modelData.role + "DisplayChoice"
                    Layout.preferredWidth: 320
                    model: OutputController.displays
                    textRole: "label"
                    valueRole: "ref"
                    currentIndex: assignedIndex()
                    displayText: currentIndex < 0 ? "Choose a display" : currentText
                    function assignedIndex() { return indexOfValue(OutputController.assignments[row.modelData.role] ?? "") }
                    onActivated: {
                        OutputController.assign(row.modelData.role, currentValue)
                        // Picking breaks the binding; restore it so the other role's picker
                        // and replugs keep this one showing the real assignment.
                        currentIndex = Qt.binding(choice.assignedIndex)
                    }
                }
                Label {
                    text: OutputController.status[row.modelData.role] ?? ""
                    color: "#8b8f96"
                }
            }
        }
    }
}
