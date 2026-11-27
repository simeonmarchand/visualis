import QtQuick
import QtMultimedia
import Visualis.Ui

// The background layer as BackgroundPlayer plays it (#98): its two slots, each a video or a
// still, the one showing (or fading in) on top. A view only: everything comes from the player.
Item {
    id: root
    opacity: BackgroundPlayer.opacity

    component SlotView: Item {
        id: view
        required property backgroundSlot slot
        readonly property alias videoSink: video.videoSink
        anchors.fill: parent
        z: slot.front ? 1 : 0
        opacity: slot.opacity
        visible: slot.kind !== "none" && slot.opacity > 0

        VideoOutput {
            id: video
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectCrop
            visible: view.slot.kind === "video"
        }
        Image {
            objectName: "backgroundStill"
            anchors.fill: parent
            visible: view.slot.kind === "image"
            source: view.slot.image
            cache: false   // each new still has its own URL; keep no old ones around
            fillMode: BackgroundPlayer.fillMode === "fit" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
        }
    }

    SlotView { id: slotA; objectName: "backgroundSlotA"; slot: BackgroundPlayer.slotA }
    SlotView { id: slotB; objectName: "backgroundSlotB"; slot: BackgroundPlayer.slotB }

    Component.onCompleted: BackgroundPlayer.addOutput(slotA.videoSink, slotB.videoSink)
}
