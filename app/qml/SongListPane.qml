pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Visualis.Ui

// The library's songs with search, plus ways to get songs in.
ColumnLayout {
    id: pane
    spacing: 8

    readonly property bool dialogOpen: pasteDialog.visible || reportDialog.visible

    TextField {
        objectName: "songSearch"
        Layout.fillWidth: true
        placeholderText: "Search songs"
        text: SongLibrary.search
        onTextEdited: SongLibrary.search = text
    }

    ListView {
        id: list
        objectName: "songList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: SongLibrary.songs
        delegate: ItemDelegate {
            required property var modelData
            width: ListView.view.width
            text: modelData.title
            highlighted: modelData.id === SongLibrary.selectedId
            // Doesn't take focus, so keys keep reaching the operator controls (#24).
            focusPolicy: Qt.NoFocus
            onClicked: {
                pane.forceActiveFocus()   // out of the search field, so keys drive slides again
                SongLibrary.select(modelData.id)
                ServiceList.showLibrarySong()
            }
        }

        Label {
            anchors.centerIn: parent
            width: parent.width - 24
            visible: list.count === 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#8b8f96"
            text: SongLibrary.search !== "" ? "No songs match."
                                            : "No songs yet. Paste lyrics or import from ProPresenter."
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Button {
            text: "New song…"
            focusPolicy: Qt.NoFocus
            enabled: SongLibrary.error === ""
            onClicked: pasteDialog.open()
        }
        Button {
            id: importButton
            text: "Import…"
            focusPolicy: Qt.NoFocus
            enabled: SongLibrary.error === "" && !SongLibrary.importing
            onClicked: importMenu.popup(importButton, 0, importButton.height)
        }
    }

    // A summary only: an import can report hundreds of lines, and the pane is narrow. The
    // full report is behind Details.
    Label {
        objectName: "libraryReport"
        Layout.fillWidth: true
        visible: text !== ""
        text: SongLibrary.importing ? SongLibrary.importProgress : SongLibrary.reportSummary
        wrapMode: Text.WordWrap
        maximumLineCount: 10   // long lines wrap; this keeps even those bounded
        elide: Text.ElideRight
        color: "#8b8f96"
    }

    Button {
        objectName: "libraryReportDetails"
        text: "Details…"
        flat: true
        focusPolicy: Qt.NoFocus
        visible: !SongLibrary.importing && SongLibrary.report !== SongLibrary.reportSummary
        onClicked: reportDialog.open()
    }

    ImportReportDialog {
        id: reportDialog
        objectName: "libraryReportDialog"
    }

    Menu {
        id: importMenu
        MenuItem { text: "ProPresenter songs (folder of .pro files)…"; onTriggered: proDialog.open() }
        MenuItem { text: "Visualis song exports (folder)…"; onTriggered: jsonDialog.open() }
    }

    FolderDialog {
        id: proDialog
        title: "Choose the folder with your ProPresenter songs"
        onAccepted: SongLibrary.importProPresenter(selectedFolder)
    }

    FolderDialog {
        id: jsonDialog
        title: "Choose a folder of Visualis song exports"
        onAccepted: SongLibrary.importSongs(selectedFolder)
    }

    PasteSongDialog {
        id: pasteDialog
        objectName: "libraryPasteDialog"
    }
}
