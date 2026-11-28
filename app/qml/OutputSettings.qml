import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Visualis.Ui

// Output displays and the background: a stopgap bar until a settings window exists. The
// background is picked from the media library (#98).
ColumnLayout {
    id: settings
    spacing: 8

    signal templatesRequested()

    BackgroundPicker {
        id: backgroundPicker
        objectName: "backgroundPicker"
        currentId: LiveState.backgroundId
        // With the live item's template's background transition.
        onChosen: id => LiveState.pickBackground(id)
    }
    PlanningCenterDialog { id: pcoDialog }
    PlanPickerDialog { id: planPicker }

    RowLayout {
        spacing: 12
        Label {
            text: "Planning Center"
            color: "#d0d3d8"
            Layout.preferredWidth: 80
            elide: Text.ElideRight
        }
        Button {
            objectName: "planningCenterButton"
            text: PlanningCenter.signedIn ? "Account…" : "Sign in…"
            focusPolicy: Qt.NoFocus
            onClicked: pcoDialog.open()
        }
        Button {
            objectName: "importPlanButton"
            text: "Import plan…"
            visible: PlanningCenter.signedIn
            focusPolicy: Qt.NoFocus
            onClicked: planPicker.open()
        }
        Label {
            text: PlanningCenter.signedIn
                  ? (PlanningCenter.organization !== "" ? PlanningCenter.organization : "Signed in")
                  : "Not signed in"
            color: "#8b8f96"
        }
    }

    // The live background, until service items and song defaults set it (#99).
    RowLayout {
        spacing: 12

        Label {
            text: "Background"
            color: "#d0d3d8"
            Layout.preferredWidth: 80
        }
        Button {
            objectName: "chooseBackground"
            text: "Choose…"
            focusPolicy: Qt.NoFocus
            enabled: SongLibrary.error === ""
            onClicked: backgroundPicker.open()
        }
        Button {
            text: "None"
            focusPolicy: Qt.NoFocus
            enabled: LiveState.backgroundId !== ""
            onClicked: LiveState.pickBackground("")
        }
        Button {
            objectName: "showBackground"
            text: "Show (cleared)"
            focusPolicy: Qt.NoFocus
            visible: LiveState.backgroundCleared
            onClicked: Commands.run("show_background")
        }
        ComboBox {
            objectName: "backgroundFillMode"
            focusPolicy: Qt.NoFocus   // arrow keys belong to the slides
            model: [{ text: "Fill", value: "fill" }, { text: "Fit", value: "fit" }]
            textRole: "text"
            valueRole: "value"
            currentIndex: indexOfValue(BackgroundPlayer.fillMode)
            onActivated: {
                BackgroundPlayer.fillMode = currentValue
                currentIndex = Qt.binding(() => indexOfValue(BackgroundPlayer.fillMode))
            }
            ToolTip.visible: hovered
            ToolTip.text: "Stills: fill the output, cropping the edges, or fit inside it with bars"
        }
        Label {
            objectName: "backgroundStatus"
            text: BackgroundPlayer.status
            color: "#8b8f96"
        }
        Button {
            objectName: "templatesButton"
            text: "Templates…"
            focusPolicy: Qt.NoFocus
            onClicked: settings.templatesRequested()
        }
    }

    Label {
        objectName: "backgroundAlert"
        readonly property string message: LiveState.backgroundAlert !== "" ? LiveState.backgroundAlert : BackgroundPlayer.alert
        visible: message !== ""
        text: message
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
            focusPolicy: Qt.NoFocus
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
                focusPolicy: Qt.NoFocus   // arrow keys belong to the slides
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
