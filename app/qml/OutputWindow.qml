import QtQuick
import Visualis.Ui

// An audience or stage output: a view of LiveState (plan 5.2) that owns no state of its
// own. OutputController places, shows, and hides it.
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

    // Background layer: independent of the text, so slide changes never touch it. The stage
    // display shows text only, so it never asks for the background's frames.
    Loader {
        anchors.fill: parent
        active: root.showsBackground
        sourceComponent: BackgroundView { objectName: "background" }
    }

    AudienceLyrics {
        objectName: "audienceLyrics"
        anchors.fill: parent
        visible: root.role === "audience"
    }

    StageDisplay {
        anchors.fill: parent
        visible: root.role === "stage"
    }
}
