pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Visualis.Ui

// Slides in play order: the library's selected song, or the selected service item (#51).
// Each tile renders through the same lyrics template as the audience output, under its
// group's label and color. Click a tile to take it live; the live slide and the next one
// are outlined.
ColumnLayout {
    id: pane
    spacing: 8

    property string title: ""
    property var slides: []
    property string songId: ""   // for Edit…; empty when what's shown isn't a library song
    // Whether what's shown here is what's live, so the live slide is outlined.
    property bool showingLive: false

    signal tileClicked(int index)
    signal editRequested(string songId)

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: pane.title !== "" ? pane.title : "Choose a song"
            color: "#d0d3d8"
            font.pixelSize: 20
            elide: Text.ElideRight
        }
        Button {
            objectName: "editSong"
            text: "Edit…"
            focusPolicy: Qt.NoFocus
            visible: pane.songId !== ""
            onClicked: pane.editRequested(pane.songId)
        }
    }

    GridView {
        id: grid
        objectName: "slideGrid"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: pane.slides
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
            isCurrent: pane.showingLive && index === LiveState.slideIndex
            isNext: pane.showingLive && index === LiveState.slideIndex + 1
            onClicked: {
                pane.forceActiveFocus()   // out of the search field, so keys drive slides again
                pane.tileClicked(index)
            }
        }
    }
}
