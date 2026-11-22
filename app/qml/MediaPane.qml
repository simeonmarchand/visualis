pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Visualis.Ui

// The library's images and videos (#97): a thumbnail grid with search, import, and delete.
ColumnLayout {
    id: pane
    spacing: 8

    readonly property bool dialogOpen: deleteDialog.visible || reportDialog.visible

    TextField {
        objectName: "mediaSearch"
        Layout.fillWidth: true
        placeholderText: "Search media"
        text: MediaLibrary.search
        onTextEdited: MediaLibrary.search = text
    }

    GridView {
        id: grid
        objectName: "mediaGrid"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        cellWidth: Math.floor(width / 2)
        cellHeight: cellWidth * 9 / 16 + 34
        model: MediaLibrary.assets
        delegate: Item {
            id: tile
            required property var modelData
            width: grid.cellWidth
            height: grid.cellHeight

            Rectangle {
                id: frame
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 4 }
                height: width * 9 / 16
                color: "#1b1d21"
                Image {
                    anchors.fill: parent
                    source: tile.modelData.thumbnail
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true   // decoding a thumbnail never holds up the window
                    sourceSize.width: 320
                }
                Label {
                    anchors { right: parent.right; bottom: parent.bottom; margins: 4 }
                    visible: tile.modelData.kind === "video"
                    text: "▶"
                    color: "white"
                }
            }
            Label {
                anchors { left: frame.left; right: frame.right; top: frame.bottom; topMargin: 2 }
                text: tile.modelData.name
                elide: Text.ElideMiddle
                font.pixelSize: 11
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: {
                    deleteDialog.assetId = tile.modelData.id
                    deleteDialog.assetName = tile.modelData.name
                    tileMenu.popup()
                }
            }
            ToolTip.visible: hover.hovered && tile.modelData.details !== ""
            ToolTip.text: tile.modelData.details
            HoverHandler { id: hover }
        }

        Label {
            anchors.centerIn: parent
            width: parent.width - 24
            visible: grid.count === 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#8b8f96"
            text: MediaLibrary.search !== "" ? "No media matches." : "No media yet. Import images or videos."
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Button {
            id: importButton
            text: "Import…"
            focusPolicy: Qt.NoFocus
            enabled: SongLibrary.error === "" && !MediaLibrary.importing
            onClicked: importMenu.popup(importButton, 0, importButton.height)
        }
        Button {
            text: "Stop"
            focusPolicy: Qt.NoFocus
            visible: MediaLibrary.importing
            onClicked: MediaLibrary.stopImport()
        }
    }

    Label {
        objectName: "mediaReport"
        Layout.fillWidth: true
        visible: text !== ""
        text: MediaLibrary.importing ? MediaLibrary.importProgress : MediaLibrary.reportSummary
        wrapMode: Text.WordWrap
        maximumLineCount: 10
        elide: Text.ElideRight
        color: "#8b8f96"
    }

    Button {
        text: "Details…"
        flat: true
        focusPolicy: Qt.NoFocus
        visible: !MediaLibrary.importing && MediaLibrary.report !== MediaLibrary.reportSummary
        onClicked: reportDialog.open()
    }

    ImportReportDialog {
        id: reportDialog
        report: MediaLibrary.report
    }

    Menu {
        id: importMenu
        MenuItem { text: "Images and videos…"; onTriggered: filesDialog.open() }
        MenuItem { text: "Folder…"; onTriggered: folderDialog.open() }
    }

    Menu {
        id: tileMenu
        MenuItem { text: "Delete…"; onTriggered: deleteDialog.ask() }
    }

    FileDialog {
        id: filesDialog
        title: "Choose images and videos to import"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Images and videos (*.jpg *.jpeg *.png *.heic *.heif *.tif *.tiff *.bmp *.gif *.webp *.mp4 *.mov *.m4v)"]
        onAccepted: MediaLibrary.importFiles(selectedFiles)
    }

    FolderDialog {
        id: folderDialog
        title: "Choose a folder of images and videos"
        onAccepted: MediaLibrary.importFiles([selectedFolder])
    }

    MediaDeleteDialog {
        id: deleteDialog
    }
}
