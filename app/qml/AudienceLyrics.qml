import QtQuick
import QtQuick.Effects
import Visualis.Ui

// The text layer of a slide, drawn from its content (#93): the main region's text in the
// template's frame and style, with the item's and the slide's overrides already applied.
// Two Text items take turns, so with a "fade" transition the new slide fades in on one
// while the old one fades out on the other; with "cut" the swap is instant.
Item {
    id: root

    // What to draw: the live slide's content by default; a preview passes its own.
    property slideContent content: LiveState.content
    // The output hides cleared text; a preview always shows its slide.
    property bool showsLive: true

    property contentRegion blank
    readonly property contentRegion region: root.content.regions.main ?? root.blank
    readonly property regionStyle textStyle: root.region.style
    readonly property layerTransition transition: root.content.transition
    // What to show; empty clears the text (cleared text, no slide).
    readonly property string text: root.showsLive && LiveState.textCleared ? "" : root.region.text
    property bool showingA: true

    onTextChanged: {
        if (showingA)
            b.text = text
        else
            a.text = text
        showingA = !showingA
    }
    Component.onCompleted: a.text = text

    // Inline components can't see ids outside them, so what they need comes in as properties.
    component LyricText: Text {
        id: lyric
        required property real outputHeight
        required property regionStyle textStyle
        required property layerTransition transition
        anchors.fill: parent
        horizontalAlignment: textStyle.horizontalAlignment
        verticalAlignment: textStyle.verticalAlignment
        textFormat: Text.PlainText   // lyrics are never markup
        wrapMode: Text.WordWrap
        // Shrink to fit the frame; at the smallest size, elide rather than overflow.
        fontSizeMode: Text.Fit
        font.pixelSize: Math.max(1, Math.round(outputHeight * textStyle.maxSize))
        minimumPixelSize: Math.max(1, Math.round(outputHeight * textStyle.minSize))
        elide: Text.ElideRight
        font.family: textStyle.fontFamily
        font.weight: textStyle.weight
        lineHeight: textStyle.lineHeight
        color: textStyle.color
        // QML draws a 1 px outline; wider ones are left for later.
        style: textStyle.outline.width > 0 ? Text.Outline : Text.Normal
        styleColor: textStyle.outline.color
        Behavior on opacity {
            enabled: lyric.transition.kind === "fade"
            NumberAnimation { duration: lyric.transition.ms }
        }
    }

    Item {
        id: frame
        x: root.width * root.textStyle.frame.x
        y: root.height * root.textStyle.frame.y
        width: root.width * root.textStyle.frame.width
        height: root.height * root.textStyle.frame.height
        visible: !root.textStyle.shadow.enabled   // drawn through the shadow effect instead

        LyricText {
            id: a
            objectName: "lyricsA"
            outputHeight: root.height
            textStyle: root.textStyle
            transition: root.transition
            opacity: root.showingA ? 1 : 0
        }
        LyricText {
            id: b
            objectName: "lyricsB"
            outputHeight: root.height
            textStyle: root.textStyle
            transition: root.transition
            opacity: root.showingA ? 0 : 1
        }
    }

    MultiEffect {
        visible: root.textStyle.shadow.enabled
        source: frame
        anchors.fill: frame
        shadowEnabled: true
        shadowColor: root.textStyle.shadow.color
        shadowOpacity: root.textStyle.shadow.opacity
        shadowBlur: root.textStyle.shadow.blur
        shadowHorizontalOffset: root.height * root.textStyle.shadow.offset
        shadowVerticalOffset: root.height * root.textStyle.shadow.offset
        autoPaddingEnabled: true
    }
}
