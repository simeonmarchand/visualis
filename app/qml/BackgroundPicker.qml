pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Visualis.Ui

// Picks a background from the media library: the live one (#98), or a service's, item's,
// song's, or group's default (#99). Whoever opens it acts on `chosen`. The search is the
// media pane's.
Popup {
    id: picker
    width: 520
    height: 420
    modal: true
    focus: true

    // Highlighted as the current choice.
    property string currentId: ""
    signal chosen(string id)

    function choose(id) {
        picker.chosen(id)
        picker.close()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        TextField {
            Layout.fillWidth: true
            placeholderText: "Search media"
            text: MediaLibrary.search
            onTextEdited: MediaLibrary.search = text
        }

        GridView {
            id: grid
            objectName: "backgroundChoices"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.floor(width / 3)
            cellHeight: cellWidth * 9 / 16 + 30
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
                    border.width: tile.modelData.id === picker.currentId ? 2 : 0
                    border.color: "#4a90d9"
                    Image {
                        anchors.fill: parent
                        anchors.margins: 2
                        source: tile.modelData.thumbnail
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
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
                    onClicked: picker.choose(tile.modelData.id)
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 24
                visible: grid.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: "#8b8f96"
                text: MediaLibrary.search !== "" ? "No media matches." : "No media yet. Import images or videos in the media pane."
            }
        }
    }
}
