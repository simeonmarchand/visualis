import QtQuick
import QtQuick.Controls
import Visualis.Ui

// Always asks: deleting removes the file from the library. When a service shows the
// asset, it says which, since that item will have nothing to show.
Dialog {
    id: deleteDialog
    objectName: "mediaDeleteDialog"
    property string assetId: ""
    property string assetName: ""
    property var services: []
    function ask() {
        services = MediaLibrary.usedBy(assetId)
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
        text: deleteDialog.services.length === 0
              ? "It's removed from the library."
              : "It's used in " + deleteDialog.services.join(", ")
                + ". Those items will have nothing to show."
    }
    onAccepted: MediaLibrary.removeAsset(assetId)
}
