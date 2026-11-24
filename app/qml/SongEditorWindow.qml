pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Visualis.Ui

// Edit a song (#44, plan W3): title and credits, lyrics as text with "[Label]" headers and
// blank-line slide breaks, its groups, its arrangements, and its template, with a preview
// through that template. Every change saves at once.
Window {
    id: window
    objectName: "songEditorWindow"
    title: editor.loaded ? "Edit: " + editor.title : "Edit song"
    width: 1280
    height: 820
    minimumWidth: 960
    minimumHeight: 600
    color: "#1e1f22"
    transientParent: null

    function edit(songId) {
        editor.songId = songId
        show()
        raise()
        requestActivate()
    }

    SongEditor {
        id: editor
        onLyricsReset: {
            lyricsArea.text = editor.lyrics
            lyricsDirty.stop()
        }
    }

    // Typing saves after a short pause, not on every key.
    Timer {
        id: lyricsDirty
        interval: 600
        onTriggered: editor.applyLyrics(lyricsArea.text)
    }

    ColorDialog {
        id: colorDialog
        property string groupId
        onAccepted: editor.setGroupColor(groupId, selectedColor.toString())
    }

    Dialog {
        id: confirmDelete
        objectName: "confirmDelete"
        title: "Delete \"" + editor.title + "\"?"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.Cancel
        Label { text: "The song and its arrangements are removed from the library. What's on screen now stays." }
        onAccepted: {
            if (editor.deleteSong())
                window.close()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10
        enabled: editor.loaded

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                objectName: "songTitle"
                Layout.fillWidth: true
                font.pixelSize: 18
                placeholderText: "Title"
                text: editor.title
                onEditingFinished: editor.setTitle(text)
            }
            Label { text: "Template"; color: "#d0d3d8" }
            ComboBox {
                id: templateChoice
                objectName: "songTemplate"
                Layout.preferredWidth: 200
                model: TemplateLibrary.entries.filter(t => t.kind === "lyrics")
                textRole: "name"
                valueRole: "id"
                currentIndex: indexOfValue(editor.templateId)
                function resync() { currentIndex = Qt.binding(() => indexOfValue(editor.templateId)) }
                // indexOfValue doesn't re-run when the list changes around the chosen one; after
                // the new list is in, so the index is looked up in it.
                onModelChanged: Qt.callLater(resync)
                // A template deleted since: the song shows with the built-in (#93).
                displayText: currentIndex < 0 ? "Missing (built-in shows)" : currentText
                onActivated: editor.templateId = currentValue
            }
            Button {
                text: "Delete song…"
                onClicked: confirmDelete.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField { id: ccli; placeholderText: "CCLI number (optional)"; text: editor.ccliNumber; Layout.preferredWidth: 180
                        onEditingFinished: editor.setCredits(ccli.text, authors.text, copyright.text) }
            TextField { id: authors; placeholderText: "Authors"; text: editor.authors; Layout.fillWidth: true
                        onEditingFinished: editor.setCredits(ccli.text, authors.text, copyright.text) }
            TextField { id: copyright; placeholderText: "Copyright"; text: editor.copyright; Layout.fillWidth: true
                        onEditingFinished: editor.setCredits(ccli.text, authors.text, copyright.text) }
        }

        Label {
            objectName: "editorProblems"
            Layout.fillWidth: true
            visible: text !== ""
            text: [editor.error].concat(editor.warnings).filter(s => s !== "").join("\n")
            wrapMode: Text.WordWrap
            color: "#e0b040"
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // Lyrics as text.
            ColumnLayout {
                Layout.preferredWidth: 420
                Layout.fillHeight: true
                Label { text: "Lyrics: [Verse 1] starts a group, a blank line starts a slide, (blank) is an empty slide"; color: "#8b8f96" }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TextArea {
                        id: lyricsArea
                        objectName: "lyricsText"
                        font.family: "Menlo"
                        font.pixelSize: 13
                        wrapMode: TextArea.Wrap
                        onTextChanged: if (activeFocus) lyricsDirty.restart()
                        onActiveFocusChanged: if (!activeFocus && lyricsDirty.running) { lyricsDirty.stop(); editor.applyLyrics(text) }
                    }
                }
            }

            // Groups and the arrangement.
            ColumnLayout {
                Layout.preferredWidth: 360
                Layout.fillHeight: true
                spacing: 8

                Label { text: "Groups"; color: "#d0d3d8"; font.bold: true }
                ListView {
                    objectName: "groupList"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, 260)
                    clip: true
                    model: editor.groups
                    spacing: 4
                    delegate: RowLayout {
                        id: groupRow
                        required property var modelData
                        width: ListView.view.width
                        spacing: 6
                        Rectangle {
                            Layout.preferredWidth: 22; Layout.preferredHeight: 22; radius: 3
                            color: groupRow.modelData.color !== "" ? groupRow.modelData.color : "#555b63"
                            border.color: "#8b8f96"
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    colorDialog.groupId = groupRow.modelData.id
                                    colorDialog.selectedColor = parent.color
                                    colorDialog.open()
                                }
                            }
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: groupRow.modelData.label
                            onEditingFinished: editor.renameGroup(groupRow.modelData.id, text)
                        }
                        Label {
                            text: groupRow.modelData.slideCount + (groupRow.modelData.slideCount === 1 ? " slide" : " slides")
                                  + (groupRow.modelData.arranged ? "" : " · not arranged")
                            color: groupRow.modelData.arranged ? "#8b8f96" : "#e0b040"
                        }
                        ToolButton { text: "✕"; onClicked: editor.removeGroup(groupRow.modelData.id) }
                    }
                }

                Label { text: "Arrangement"; color: "#d0d3d8"; font.bold: true; Layout.topMargin: 8 }
                RowLayout {
                    spacing: 6
                    ComboBox {
                        id: arrangementChoice
                        objectName: "arrangementChoice"
                        Layout.fillWidth: true
                        model: editor.arrangements
                        textRole: "name"
                        valueRole: "id"
                        displayText: currentIndex < 0 ? "Groups in order" : currentText + (editor.arrangements[currentIndex].isDefault ? " (default)" : "")
                        currentIndex: indexOfValue(editor.arrangementId)
                        onActivated: editor.arrangementId = currentValue
                    }
                    Button { text: "New"; onClicked: editor.addArrangement("Arrangement " + (editor.arrangements.length + 1)) }
                    Button {
                        text: "Default"
                        enabled: arrangementChoice.currentIndex >= 0 && !editor.arrangements[arrangementChoice.currentIndex].isDefault
                        onClicked: editor.makeDefaultArrangement(editor.arrangementId)
                    }
                    Button { text: "Delete"; enabled: arrangementChoice.currentIndex >= 0; onClicked: editor.removeArrangement(editor.arrangementId) }
                }
                TextField {
                    Layout.fillWidth: true
                    visible: arrangementChoice.currentIndex >= 0
                    placeholderText: "Arrangement name"
                    text: arrangementChoice.currentIndex >= 0 ? editor.arrangements[arrangementChoice.currentIndex].name : ""
                    onEditingFinished: editor.renameArrangement(editor.arrangementId, text)
                }

                // The play order, one row per step.
                ListView {
                    objectName: "sequenceList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: editor.sequence
                    spacing: 3
                    delegate: Rectangle {
                        id: step
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        height: 30
                        radius: 3
                        color: step.modelData.color !== "" ? step.modelData.color : "#555b63"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            Label { Layout.fillWidth: true; text: (step.index + 1) + ".  " + step.modelData.label; color: "white"; font.bold: true }
                            ToolButton { text: "▲"; enabled: step.index > 0; onClicked: editor.moveStep(step.index, -1) }
                            ToolButton { text: "▼"; enabled: step.index < editor.sequence.length - 1; onClicked: editor.moveStep(step.index, 1) }
                            ToolButton { text: "✕"; onClicked: editor.removeStep(step.index) }
                        }
                    }
                }
                RowLayout {
                    spacing: 6
                    ComboBox {
                        id: addChoice
                        Layout.fillWidth: true
                        model: editor.groups
                        textRole: "label"
                        valueRole: "id"
                    }
                    Button { text: "Add to order"; enabled: addChoice.currentIndex >= 0; onClicked: editor.addStep(addChoice.currentValue) }
                }
            }

            // Preview through the song's template, in play order.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Label { text: "Preview (" + editor.previewSlides.length + " slides)"; color: "#d0d3d8"; font.bold: true }
                GridView {
                    id: preview
                    objectName: "editorPreview"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: editor.previewSlides
                    readonly property int columns: Math.max(1, Math.floor(width / 200))
                    cellWidth: Math.floor(width / columns)
                    cellHeight: Math.round((cellWidth - 10) * 9 / 16) + 34
                    ScrollBar.vertical: ScrollBar {}
                    delegate: SlideTile {
                        required property var modelData
                        width: preview.cellWidth - 10
                        content: modelData.content
                        groupLabel: modelData.groupLabel
                        groupColor: modelData.groupColor
                    }
                }
            }
        }
    }
}
