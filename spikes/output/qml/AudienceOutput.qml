import QtQuick
import QtMultimedia
import Visualis.Spike

Window {
    id: root

    property bool showVideo: true
    property int frameNumber: 0   // set by FrameStats after every swap

    title: "Visualis: Audience"
    color: "black"
    width: 960
    height: 540

    // Background layer: independent of the lyrics, so slide changes never touch it.
    VideoOutput {
        objectName: "video"
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop
        visible: root.showVideo
        opacity: VideoEngine.backgroundVisible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 400 } }
    }

    LyricsLayer {
        anchors.fill: parent
        text: LiveState.currentText
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        text: root.frameNumber
        color: "#ffff00"
        font.pixelSize: 28
        font.family: "Menlo"
        style: Text.Outline
    }
}
