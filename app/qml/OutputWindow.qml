import QtQuick

// An audience or stage output. OutputController places, shows, and hides it; the content
// (lyrics over the background) arrives with the lyrics template in #22.
Window {
    required property string role

    objectName: role + "Output"
    title: "Visualis " + role
    // Its own top-level window, not a child of the operator window (which would tie the two together).
    transientParent: null
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "black"
    visible: false
}
