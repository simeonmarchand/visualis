import QtQuick

// Operator window. Song list, slide grid, and controls arrive in #23 and #24.
Window {
    id: root
    objectName: "operatorWindow"
    title: "Visualis"
    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    color: "#1e1f22"

    Text {
        anchors.centerIn: parent
        text: "Visualis"
        color: "#8b8f96"
        font.pixelSize: 28
    }
}
