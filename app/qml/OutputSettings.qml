import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Visualis.Ui

// Output displays and the background video: a stopgap bar until a settings window and the
// media library exist.
ColumnLayout {
    spacing: 8

    FileDialog {
        id: backgroundDialog
        title: "Choose a background video"
        nameFilters: ["Video (*.mp4 *.mov *.m4v)", "All files (*)"]
        onAccepted: VideoEngine.source = selectedFile
    }

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
