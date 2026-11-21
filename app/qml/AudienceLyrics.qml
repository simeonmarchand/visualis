import QtQuick
import QtQuick.Effects
import Visualis.Ui

// Centered lyrics inside the safe area, crossfading between slides. Two Text items take
// turns: the new slide fades in on one while the old one fades out on the other.
Item {
    id: root

    // What to show; empty fades the text out (cleared text, no slide).
    property string text: LiveState.hasSlide && !LiveState.textCleared ? LiveState.currentText : ""
    property bool showingA: true

    onTextChanged: {
        if (showingA)
            b.text = text
        else
            a.text = text
        showingA = !showingA
    }
    Component.onCompleted: a.text = text

    // Inline components can't see ids outside them, so sizes come in as properties.
    component LyricText: Text {
        required property real outputHeight
        anchors.fill: parent
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.PlainText   // lyrics are never markup
        wrapMode: Text.WordWrap
        // Shrink to fit the safe area; at the smallest size, elide rather than overflow.
        fontSizeMode: Text.Fit
        font.pixelSize: Math.max(1, Math.round(outputHeight * LyricsTemplate.maxTextSize))
        minimumPixelSize: Math.max(1, Math.round(outputHeight * LyricsTemplate.minTextSize))
        elide: Text.ElideRight
        font.family: LyricsTemplate.fontFamily
        font.weight: LyricsTemplate.fontWeight
        lineHeight: LyricsTemplate.lineHeight
        color: LyricsTemplate.textColor
        Behavior on opacity { NumberAnimation { duration: LyricsTemplate.fadeMs } }
    }

    Item {
        id: safeArea
        anchors.fill: parent
        anchors.leftMargin: root.width * LyricsTemplate.safeAreaX
        anchors.rightMargin: root.width * LyricsTemplate.safeAreaX
        anchors.topMargin: root.height * LyricsTemplate.safeAreaY
        anchors.bottomMargin: root.height * LyricsTemplate.safeAreaY
        visible: !LyricsTemplate.shadow   // drawn through the shadow effect instead

        LyricText { id: a; objectName: "lyricsA"; outputHeight: root.height; opacity: root.showingA ? 1 : 0 }
        LyricText { id: b; objectName: "lyricsB"; outputHeight: root.height; opacity: root.showingA ? 0 : 1 }
    }

    MultiEffect {
        visible: LyricsTemplate.shadow
        source: safeArea
        anchors.fill: safeArea
        shadowEnabled: true
        shadowColor: LyricsTemplate.shadowColor
        shadowOpacity: LyricsTemplate.shadowOpacity
        shadowBlur: LyricsTemplate.shadowBlur
        shadowHorizontalOffset: root.height * LyricsTemplate.shadowOffset
        shadowVerticalOffset: root.height * LyricsTemplate.shadowOffset
        autoPaddingEnabled: true
    }
}
