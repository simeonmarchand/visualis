import QtQuick
import QtMultimedia
import Visualis.Ui

// An audience or stage output. OutputController places, shows, and hides it; the lyrics
// arrive with the template in #22.
Window {
    id: root
    required property string role
    // The stage display shows text only (plan 5.5).
    readonly property bool showsBackground: role === "audience"

    objectName: role + "Output"
    title: "Visualis " + role
    // Its own top-level window, not a child of the operator window (which would tie the two together).
    transientParent: null
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "black"
    visible: false

    // Background layer: independent of the text, so slide changes never touch it.
    VideoOutput {
        id: background
        objectName: "background"
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop
        visible: root.showsBackground
    }

    Component.onCompleted: {
        if (showsBackground)
            VideoEngine.addTarget(background.videoSink)
    }
}
