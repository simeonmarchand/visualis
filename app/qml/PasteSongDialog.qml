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

    // The title to start with, e.g. a Planning Center song's (#55).
    property string initialTitle: ""
    // The new song, now selected in the library.
    signal created(string songId)
    // Why the last OK didn't save (a song over a limit, #83). The dialog stays open with what
    // was typed, so the operator can fix it rather than paste it again.
    property string problem: ""

    onOpened: {
        if (problem !== "")
            return   // reopened to show the problem
        titleField.text = initialTitle
        lyricsArea.text = ""
        titleField.forceActiveFocus()
    }
    onAccepted: {
        if (SongLibrary.createFromPaste(titleField.text, lyricsArea.text)) {
            problem = ""
            created(SongLibrary.selectedId)
        } else {
            problem = SongLibrary.report
        }
    }
    onRejected: problem = ""
    // Once it has finished closing: opening it while it closes does nothing.
    onClosed: if (problem !== "") open()

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            objectName: "pasteProblem"
            Layout.fillWidth: true
            visible: text !== ""
            text: dialog.problem
            wrapMode: Text.WordWrap
            color: "#e0b040"
        }
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
