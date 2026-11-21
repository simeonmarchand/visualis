import QtQuick

// Crossfades between two Text items so a slide change never blanks the screen.
Item {
    id: layer

    property string text
    property int fadeMs: 300
    property real fontPixelSize: height * 0.07

    property bool showingA: true
    property bool ready: false

    onTextChanged: {
        if (!ready)
            return
        if (showingA)
            b.text = text
        else
            a.text = text
        showingA = !showingA
    }
    Component.onCompleted: {
        a.text = text
        ready = true
    }

    // Inline components can't see the outer ids, so sizes come in as properties.
    component LyricText: Text {
        property int fadeMs
        anchors.fill: parent
        anchors.margins: parent.height * 0.08
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
        color: "white"
        style: Text.Outline
        styleColor: "#cc000000"
        font.weight: Font.DemiBold
        Behavior on opacity { NumberAnimation { duration: fadeMs } }
    }

    LyricText { id: a; fadeMs: layer.fadeMs; font.pixelSize: layer.fontPixelSize; opacity: layer.showingA ? 1 : 0 }
    LyricText { id: b; fadeMs: layer.fadeMs; font.pixelSize: layer.fontPixelSize; opacity: layer.showingA ? 0 : 1 }
}
