import QtQuick
import QtMultimedia
import Visualis.Spike

Window {
    id: root

    property bool showVideo: false
    property int frameNumber: 0   // set by FrameStats after every swap

    title: "Visualis: Stage"
    color: "black"
    width: 960
    height: 540

    // Only used for the decoder test (--stage-video).
    VideoOutput {
        objectName: "video"
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop
        visible: root.showVideo
        opacity: VideoEngine.backgroundVisible ? 0.35 : 0
    }

    Column {
        anchors.fill: parent
        anchors.margins: root.height * 0.04
        spacing: root.height * 0.03

        Row {
            width: parent.width
            Text {
                width: parent.width / 2
                text: LiveState.itemTitle + "  ·  " + (LiveState.index + 1) + "/" + LiveState.slides.length
                color: "#9ad"
                font.pixelSize: root.height * 0.045
            }
            Text {
                id: clock
                width: parent.width / 2
                horizontalAlignment: Text.AlignRight
                color: "white"
                font.pixelSize: root.height * 0.06
                font.family: "Menlo"
                Timer {
                    interval: 250; running: true; repeat: true; triggeredOnStart: true
                    onTriggered: clock.text = Qt.formatTime(new Date(), "h:mm:ss")
                }
            }
        }

        Text {
            width: parent.width
            text: LiveState.textCleared ? "(text cleared)" : LiveState.currentText
            color: LiveState.textCleared ? "#888" : "white"
            wrapMode: Text.WordWrap
            font.pixelSize: root.height * 0.08
            font.weight: Font.DemiBold
        }

        Rectangle { width: parent.width; height: 2; color: "#444" }

        Text {
            width: parent.width
            text: LiveState.nextText ? "Next: " + LiveState.nextText : "Next: (end)"
            color: "#e8b04a"
            wrapMode: Text.WordWrap
            font.pixelSize: root.height * 0.055
        }
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        text: root.frameNumber
        color: "#ffff00"
        font.pixelSize: 28
        font.family: "Menlo"
    }
}
