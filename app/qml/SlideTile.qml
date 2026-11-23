import QtQuick
import Visualis.Ui

// One slide in the grid: its group's label on the group color, and a 16:9 preview drawn
// by the audience's lyrics layer from the slide's content, so it matches the output.
Item {
    id: tile

    property slideContent content
    property string groupLabel
    property string groupColor
    property bool isCurrent: false
    property bool isNext: false
    signal clicked()

    readonly property color accent: groupColor !== "" ? groupColor : "#555b63"
    height: label.height + preview.height

    Rectangle {
        id: label
        width: parent.width
        height: 24
        color: tile.accent
        radius: 3
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 6
            width: parent.width - 12
            text: tile.groupLabel
            color: "white"
            font.pixelSize: 13
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }

    Rectangle {
        id: preview
        anchors.top: label.bottom
        width: parent.width
        height: Math.round(width * 9 / 16)
        color: "black"
        clip: true

        AudienceLyrics {
            id: lyrics
            anchors.fill: parent
            content: tile.content
            showsLive: false
        }
        Text {
            anchors.centerIn: parent
            visible: lyrics.text.trim() === ""
            text: "Blank"
            color: "#55595f"
            font.pixelSize: 13
        }
    }

    // Live: a thick outline in the group's color with a LIVE tag. Next: a thin gray one.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        color: "transparent"
        radius: 4
        border.width: tile.isCurrent ? 3 : tile.isNext ? 2 : 0
        border.color: tile.isCurrent ? "#ff9f1a" : "#8b8f96"
    }
    Rectangle {
        visible: tile.isCurrent
        anchors.right: preview.right
        anchors.bottom: preview.bottom
        anchors.margins: 4
        width: liveText.implicitWidth + 8
        height: liveText.implicitHeight + 2
        color: "#ff9f1a"
        radius: 2
        Text { id: liveText; anchors.centerIn: parent; text: "LIVE"; color: "black"; font.pixelSize: 11; font.bold: true }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: tile.clicked()
    }
}
