pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Visualis.Ui

// Operator window: songs and the open service on the left, the selected song's or service
// item's slides on the right, output and background settings below. Keys from #24.
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

    onClosing: Qt.quit()   // visible output windows would otherwise keep the app running

    // Keys (#24). Application-wide, so they work whichever window has focus; buttons, lists,
    // and the grid don't take focus, so Space never presses a button instead (P1 keyboard
    // test, row 8). While typing in a text field the keys type instead, and while a dialog
    // is open even Esc belongs to the dialog.
    readonly property bool editingText: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    // The song editor is its own window: while it's in front, keys edit the song. `visible`
    // too, because `active` isn't always cleared when the window closes.
    readonly property bool editorInFront: songEditor.visible && songEditor.active
    readonly property bool dialogOpen: songList.dialogOpen || servicePane.dialogOpen
    readonly property bool keysEnabled: !editingText && !dialogOpen && !editorInFront

    Shortcut {
        sequences: ["Right", "Down", "Space", "PgDown"]   // clickers send Page Down / Page Up
        context: Qt.ApplicationShortcut
        enabled: root.keysEnabled
        onActivated: Commands.run("next_slide")
    }
    Shortcut {
        sequences: ["Left", "Up", "PgUp"]
        context: Qt.ApplicationShortcut
        enabled: root.keysEnabled
        onActivated: Commands.run("prev_slide")
    }
    Shortcut {
        sequence: "C"
        context: Qt.ApplicationShortcut
        enabled: root.keysEnabled
        onActivated: Commands.run("clear_text")
    }
    Shortcut {
        sequence: "B"
        context: Qt.ApplicationShortcut
        enabled: root.keysEnabled
        onActivated: Commands.run("clear_background")
    }
    Shortcut {
        sequence: "Esc"   // the panic key: works even while typing in search
        context: Qt.ApplicationShortcut
        enabled: !root.dialogOpen && !root.editorInFront
        onActivated: Commands.run("clear_all")
    }
    // The service (#51): Return takes the selected item live; Option+Up/Down moves it.
    Shortcut {
        sequences: ["Return", "Enter"]
        context: Qt.ApplicationShortcut
        enabled: root.keysEnabled && ServiceList.inGrid
        onActivated: Commands.run("goto_item", ServiceList.selectedIndex)
    }
    Shortcut {
        sequence: "Alt+Up"
        context: Qt.ApplicationShortcut
        enabled: root.keysEnabled && ServiceList.selectedIndex > 0
        onActivated: ServiceList.moveItem(ServiceList.selectedIndex, ServiceList.selectedIndex - 1)
    }
    Shortcut {
        sequence: "Alt+Down"
        context: Qt.ApplicationShortcut
        enabled: root.keysEnabled && ServiceList.selectedIndex >= 0
        onActivated: ServiceList.moveItem(ServiceList.selectedIndex, ServiceList.selectedIndex + 1)
    }
    // Shift + a group's first letter: Shift+C the next Chorus, Shift+V the next Verse...
    Instantiator {
        model: 26
        delegate: Shortcut {
            required property int index
            readonly property string letter: String.fromCharCode(65 + index)
            sequence: "Shift+" + letter
            context: Qt.ApplicationShortcut
            enabled: root.keysEnabled
            onActivated: Commands.run("goto_group", letter)
        }
    }

    OutputWindow { id: audienceOutput; role: "audience" }
    OutputWindow { id: stageOutput; role: "stage" }
    SongEditorWindow { id: songEditor }

    Component.onCompleted: {
        OutputController.setOperatorWindow(root)
        OutputController.addOutput("audience", audienceOutput)
        OutputController.addOutput("stage", stageOutput)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            objectName: "recoveryMessage"
            Layout.fillWidth: true
            visible: Recovery.message !== ""
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: Recovery.message
                wrapMode: Text.WordWrap
                color: "#e0b040"
            }
            Button {
                text: "OK"
                focusPolicy: Qt.NoFocus
                onClicked: Recovery.dismiss()
            }
        }

        Label {
            objectName: "libraryError"
            Layout.fillWidth: true
            visible: SongLibrary.error !== ""
            text: "The song library couldn't be opened: " + SongLibrary.error
            wrapMode: Text.WordWrap
            color: "#e0b040"
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            SongListPane {
                id: songList
                // Fixed widths: the grid gets the rest.
                Layout.preferredWidth: 260
                Layout.maximumWidth: 260
                Layout.fillHeight: true
            }
            ServicePane {
                id: servicePane
                Layout.preferredWidth: 260
                Layout.maximumWidth: 260
                Layout.fillHeight: true
            }
            SlideGrid {
                id: slideGrid
                readonly property bool showsItem: ServiceList.inGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: showsItem ? ServiceList.selectedTitle : SongLibrary.selectedTitle
                slides: showsItem ? ServiceList.selectedSlides : SongLibrary.selectedSlides
                songId: showsItem ? ServiceList.selectedSongId : SongLibrary.selectedId
                // A song live from a service item may use another arrangement than the
                // library's, so it's only the live one here when it's the same item.
                showingLive: showsItem ? LiveState.itemId !== "" && LiveState.itemIndex === ServiceList.selectedIndex
                                       : LiveState.songId !== "" && LiveState.itemId === ""
                                         && LiveState.songId === SongLibrary.selectedId
                onTileClicked: index => {
                    if (showingLive)
                        Commands.run("goto_slide", index)
                    else if (showsItem)
                        Commands.run("goto_item", [ServiceList.selectedIndex, index])
                    else
                        SongLibrary.goLive(index)   // a library song isn't a service item: no command names it
                }
                onEditRequested: songId => songEditor.edit(songId)
            }
        }

        OutputSettings {
            Layout.fillWidth: true
        }
    }
}
