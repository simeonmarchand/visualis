import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Visualis.Ui

// Operator window: songs on the left, the selected song's slides on the right, output
// and background settings below. Keyboard control arrives in #24.
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            objectName: "libraryError"
            Layout.fillWidth: true
            visible: SongLibrary.error !== ""
            text: "The song library couldn't be opened: " + SongLibrary.error
            wrapMode: Text.WordWrap
            color: "#e0b040"
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            SongListPane {
                // Fixed width: the grid gets the rest.
                Layout.preferredWidth: 300
                Layout.maximumWidth: 300
                Layout.fillHeight: true
            }
            SlideGrid {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }

        OutputSettings {
            Layout.fillWidth: true
        }
    }
}
