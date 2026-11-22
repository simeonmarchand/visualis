pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Visualis.Ui

// The selected song's slides in play order. Each tile renders through the same lyrics
// template as the audience output, under its group's label and color. Click a tile to take
// it live; the live slide and the next one are outlined.
ColumnLayout {
    id: pane
    spacing: 8

    // Only highlight when the song on screen here is the one that's live.
    readonly property bool showingLiveSong: LiveState.songId !== "" && LiveState.songId === SongLibrary.selectedId

    signal editRequested(string songId)

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: SongLibrary.selectedTitle !== "" ? SongLibrary.selectedTitle : "Choose a song"
            color: "#d0d3d8"
            font.pixelSize: 20
            elide: Text.ElideRight
        }
        Button {
            objectName: "editSong"
            text: "Edit…"
            focusPolicy: Qt.NoFocus
            visible: SongLibrary.selectedId !== ""
            onClicked: pane.editRequested(SongLibrary.selectedId)
        }
    }

    GridView {
        id: grid
        objectName: "slideGrid"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: SongLibrary.selectedSlides
        // As many 16:9 tiles of at least 220 px as fit across.
        readonly property int columns: Math.max(1, Math.floor(width / 230))
        cellWidth: Math.floor(width / columns)
        cellHeight: Math.round((cellWidth - 12) * 9 / 16) + 36
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        delegate: SlideTile {
            required property int index
            required property var modelData
            objectName: "slideTile" + index
            width: grid.cellWidth - 12
            text: modelData.text
            groupLabel: modelData.groupLabel
            groupColor: modelData.groupColor
            isCurrent: pane.showingLiveSong && index === LiveState.slideIndex
            isNext: pane.showingLiveSong && index === LiveState.slideIndex + 1
            onClicked: {
                pane.forceActiveFocus()   // out of the search field, so keys drive slides again
                SongLibrary.goLive(index)
            }
        }
    }
}
