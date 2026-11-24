import QtQuick
import QtQuick.Effects
import Visualis.Ui

// The text layer of a slide, drawn from its content (#93): the main region's text in the
// template's frame and style, with the item's and the slide's overrides already applied.
// Two Text items take turns, each keeping its own slide's text and style. With a "fade"
// transition the new slide fades in on one while the old one fades out on the other; with
// "cut" the swap is instant, with no animation frames. Clearing the text uses the live
// slide's transition (#96).
Item {
    id: root

    // What to draw: the live slide's content by default; a preview passes its own.
    property slideContent content: LiveState.content
    // The output hides cleared text; a preview always shows its slide, and always cuts.
    property bool showsLive: true

    property contentRegion blank
    // What's showing now, or fading in; empty when the text is cleared or there's no slide.
    readonly property string text: root.incomingText()
    property bool showingA: true

    function incomingText() {
        return root.showsLive && LiveState.textCleared ? "" : (root.content.regions.main ?? root.blank).text
    }

    // Brings the Text items up to date with `content`. Reads everything from `content` itself,
    // so it doesn't matter which change notified first.
    function sync() {
        const region = root.content.regions.main ?? root.blank
        const text = root.incomingText()
        const shown = root.showingA ? layerA : layerB
        if (text === shown.text) {
            shown.textStyle = region.style   // a template edit: restyle in place
            return
        }
        const incoming = root.showingA ? layerB : layerA
        fade.stop()
        // A Next mid-fade lands on the slide still fading out: it goes at once, so its text
        // never changes while it shows.
        incoming.lyricOpacity = 0
        incoming.text = text
        incoming.textStyle = region.style
        root.showingA = !root.showingA
        const transition = root.content.transition
        if (root.showsLive && transition.kind === "fade" && transition.ms > 0) {
            fadeIn.target = incoming
            fadeOut.target = shown
            fade.duration = transition.ms
            fade.start()
        } else {
            incoming.lyricOpacity = 1
            shown.lyricOpacity = 0
        }
    }

    onContentChanged: sync()
    onTextChanged: sync()
    Component.onCompleted: {
        layerA.text = incomingText()
        layerA.textStyle = (content.regions.main ?? blank).style
    }

    ParallelAnimation {
        id: fade
        objectName: "lyricsFade"
        property int duration
        NumberAnimation { id: fadeIn; property: "lyricOpacity"; to: 1; duration: fade.duration }
        NumberAnimation { id: fadeOut; property: "lyricOpacity"; to: 0; duration: fade.duration }
    }

    // One slide's text in its frame and style, with its shadow. Its Text's opacity is what fades.
    component Layer: Item {
        id: layer
        required property string lyricName
        property string text
        property regionStyle textStyle
        property real lyricOpacity
        anchors.fill: parent

        Item {
            id: frame
            x: layer.width * layer.textStyle.frame.x
            y: layer.height * layer.textStyle.frame.y
            width: layer.width * layer.textStyle.frame.width
            height: layer.height * layer.textStyle.frame.height
            visible: !layer.textStyle.shadow.enabled   // drawn through the shadow effect instead

            Text {
                objectName: layer.lyricName
                anchors.fill: parent
                text: layer.text
                opacity: layer.lyricOpacity
                horizontalAlignment: layer.textStyle.horizontalAlignment
                verticalAlignment: layer.textStyle.verticalAlignment
                textFormat: Text.PlainText   // lyrics are never markup
                wrapMode: Text.WordWrap
                // Shrink to fit the frame; at the smallest size, elide rather than overflow.
                fontSizeMode: Text.Fit
                font.pixelSize: Math.max(1, Math.round(layer.height * layer.textStyle.maxSize))
                minimumPixelSize: Math.max(1, Math.round(layer.height * layer.textStyle.minSize))
                elide: Text.ElideRight
                font.family: layer.textStyle.fontFamily
                font.weight: layer.textStyle.weight
                lineHeight: layer.textStyle.lineHeight
                color: layer.textStyle.color
                // QML draws a 1 px outline; wider ones are left for later.
                style: layer.textStyle.outline.width > 0 ? Text.Outline : Text.Normal
                styleColor: layer.textStyle.outline.color
            }
        }

        MultiEffect {
            visible: layer.textStyle.shadow.enabled
            source: frame
            anchors.fill: frame
            shadowEnabled: true
            shadowColor: layer.textStyle.shadow.color
            shadowOpacity: layer.textStyle.shadow.opacity
            shadowBlur: layer.textStyle.shadow.blur
            shadowHorizontalOffset: layer.height * layer.textStyle.shadow.offset
            shadowVerticalOffset: layer.height * layer.textStyle.shadow.offset
            autoPaddingEnabled: true
        }
    }

    Layer { id: layerA; lyricName: "lyricsA"; lyricOpacity: 1 }
    Layer { id: layerB; lyricName: "lyricsB"; lyricOpacity: 0 }
}
