import QtQuick
import QtQuick.Controls
import Visualis.Ui

// Always asks: deleting removes the file from the library. When a service or song uses the
// asset (as a media item, a background, or a default), it says which, since nothing will
// show in its place.
Dialog {
    id: deleteDialog
    objectName: "mediaDeleteDialog"
    property string assetId: ""
    property string assetName: ""
    property var users: []
    function ask() {
        users = MediaLibrary.usedBy(assetId)
        open()
    }
    title: "Delete \"" + assetName + "\"?"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(420, parent.width - 48)
    standardButtons: Dialog.Ok | Dialog.Cancel
    Label {
        objectName: "mediaDeleteText"
        width: parent.width
        wrapMode: Text.WordWrap
        text: deleteDialog.users.length === 0
              ? "It's removed from the library."
              : "It's used by " + deleteDialog.users.join(", ")
                + ". Nothing will show in its place."
    }
    onAccepted: MediaLibrary.removeAsset(assetId)
}
