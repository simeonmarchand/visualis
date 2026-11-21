import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Visualis.Ui

// New song from pasted lyrics (MultiTracks, SongSelect, or typed). Section labels on their
// own line ("Verse 1", "[Chorus]") start groups; a blank line starts a new slide; longer
// stanzas are split into 2-line slides.
Dialog {
    id: dialog
    title: "New song from pasted lyrics"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 560
    height: 600
    standardButtons: Dialog.Ok | Dialog.Cancel

    onOpened: {
        titleField.text = ""
        lyricsArea.text = ""
        titleField.forceActiveFocus()
    }
    onAccepted: SongLibrary.createFromPaste(titleField.text, lyricsArea.text)

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        TextField {
            id: titleField
            objectName: "pasteTitle"
            Layout.fillWidth: true
            placeholderText: "Title"
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                id: lyricsArea
                objectName: "pasteLyrics"
                placeholderText: "Verse 1\nAmazing grace how sweet the sound\nThat saved a wretch like me\n\nChorus\n…"
                wrapMode: TextArea.Wrap
            }
        }
    }
}
