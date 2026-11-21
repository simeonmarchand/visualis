import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Visualis.Ui

// The library's songs with search, plus ways to get songs in.
ColumnLayout {
    id: pane
    spacing: 8

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
            onClicked: SongLibrary.select(modelData.id)
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
            enabled: SongLibrary.error === ""
            onClicked: importMenu.popup(importButton, 0, importButton.height)
        }
    }

    Label {
        objectName: "libraryReport"
        Layout.fillWidth: true
        visible: text !== ""
        text: SongLibrary.report
        wrapMode: Text.WordWrap
        color: "#8b8f96"
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
    }
}
