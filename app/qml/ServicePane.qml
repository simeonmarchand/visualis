pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Visualis.Ui

// The open service's items (#51): pick, create, rename, and delete services; add the
// library's selected song, headers, and text or blank slides; drag items to reorder
// (Option+Up/Down moves the selected one). Click an item to see its slides.
ColumnLayout {
    id: pane
    spacing: 8

    readonly property bool dialogOpen: nameDialog.visible || deleteDialog.visible
    readonly property int rowHeight: 34
    property int dragFrom: -1
    property int dropAt: -1

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        ComboBox {
            id: picker
            objectName: "servicePicker"
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            enabled: SongLibrary.error === ""
            model: ServiceList.services
            textRole: "title"
            valueRole: "id"
            currentIndex: {
                const list = ServiceList.services
                for (let i = 0; i < list.length; ++i) {
                    if (list[i].id === ServiceList.openId)
                        return i
                }
                return -1
            }
            displayText: ServiceList.openTitle !== "" ? ServiceList.openTitle : "No service open"
            onActivated: ServiceList.openService(currentValue)
        }
        Button {
            id: serviceButton
            text: "Service"
            focusPolicy: Qt.NoFocus
            enabled: SongLibrary.error === ""
            onClicked: serviceMenu.popup(serviceButton, 0, serviceButton.height)
        }
    }

    ListView {
        id: list
        objectName: "serviceItems"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: ServiceList.items
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            readonly property bool isHeader: modelData.kind === "header"
            readonly property bool isLive: modelData.id === LiveState.itemId
            objectName: "serviceItem" + index
            width: ListView.view.width
            height: pane.rowHeight
            color: index === ServiceList.selectedIndex ? "#2f4a6a" : (isHeader ? "#2a2c30" : "transparent")
            opacity: pane.dragFrom === index ? 0.5 : 1

            Rectangle {   // the live item
                width: 4
                height: parent.height
                color: "#d04040"
                visible: row.isLive
            }
            Label {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: row.modelData.kind === "placeholder" ? row.modelData.title + " (song not in library)"
                                                           : row.modelData.title
                font.bold: row.isHeader
                font.capitalization: row.isHeader ? Font.AllUppercase : Font.MixedCase
                color: row.isHeader ? "#8b8f96" : (row.modelData.hasSlides ? "#d0d3d8" : "#6b6f76")
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                property real pressY: 0
                onPressed: mouse => {
                    pressY = mouse.y
                    pane.forceActiveFocus()   // out of the search field, so keys drive slides again
                }
                onPositionChanged: mouse => {
                    if (!(mouse.buttons & Qt.LeftButton))
                        return
                    if (pane.dragFrom < 0 && Math.abs(mouse.y - pressY) < 6)
                        return
                    pane.dragFrom = row.index
                    const y = mapToItem(list.contentItem, mouse.x, mouse.y).y
                    pane.dropAt = Math.max(0, Math.min(list.count - 1, Math.floor(y / pane.rowHeight)))
                }
                onReleased: mouse => {
                    if (pane.dragFrom >= 0) {
                        ServiceList.moveItem(pane.dragFrom, pane.dropAt)
                        pane.dragFrom = -1
                        pane.dropAt = -1
                    } else {
                        ServiceList.select(row.index)
                        if (mouse.button === Qt.RightButton)
                            itemMenu.popup()
                    }
                }
                onCanceled: {
                    pane.dragFrom = -1
                    pane.dropAt = -1
                }
            }
        }

        Rectangle {   // where a dragged item will land
            parent: list.contentItem
            visible: pane.dragFrom >= 0 && pane.dropAt !== pane.dragFrom
            width: list.width
            height: 2
            color: "#4a90e2"
            y: (pane.dropAt + (pane.dropAt > pane.dragFrom ? 1 : 0)) * pane.rowHeight - 1
        }

        Label {
            anchors.centerIn: parent
            width: parent.width - 24
            visible: list.count === 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#8b8f96"
            text: ServiceList.openId === "" ? "Open or create a service." : "Add songs from the library."
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Button {
            objectName: "addSongToService"
            text: "Add song"
            focusPolicy: Qt.NoFocus
            enabled: ServiceList.openId !== "" && SongLibrary.selectedId !== ""
            onClicked: ServiceList.addSong(SongLibrary.selectedId)
        }
        Button {
            id: addButton
            text: "Add…"
            focusPolicy: Qt.NoFocus
            enabled: ServiceList.openId !== ""
            onClicked: addMenu.popup(addButton, 0, addButton.height)
        }
    }

    Label {
        objectName: "serviceReport"
        Layout.fillWidth: true
        visible: text !== ""
        text: ServiceList.report
        wrapMode: Text.WordWrap
        color: "#8b8f96"
    }

    Menu {
        id: serviceMenu
        MenuItem { text: "New service…"; onTriggered: pane.ask("New service", "Service name", false, "", name => ServiceList.createService(name)) }
        MenuItem {
            text: "Rename…"
            enabled: ServiceList.openId !== ""
            onTriggered: pane.ask("Rename service", "Service name", false, ServiceList.openTitle, name => ServiceList.renameService(name))
        }
        MenuItem { text: "Delete…"; enabled: ServiceList.openId !== ""; onTriggered: deleteDialog.open() }
    }

    Menu {
        id: addMenu
        MenuItem { text: "Header…"; onTriggered: pane.ask("New header", "Header, e.g. Worship", false, "", name => ServiceList.addHeader(name)) }
        MenuItem { text: "Text…"; onTriggered: pane.ask("New text slide", "Title", true, "", (name, body) => ServiceList.addText(name, body)) }
        MenuItem { text: "Blank slide"; onTriggered: ServiceList.addText("Blank", "") }
    }

    Menu {
        id: itemMenu
        MenuItem { text: "Remove from service"; onTriggered: ServiceList.removeItem(ServiceList.selectedIndex) }
    }

    property var pendingChoice: null
    function ask(title, placeholder, withBody, initial, then) {
        nameDialog.title = title
        nameDialog.placeholder = placeholder
        nameDialog.withBody = withBody
        nameDialog.initialName = initial
        pane.pendingChoice = then
        nameDialog.open()
    }

    NameDialog {
        id: nameDialog
        onChosen: (name, body) => {
            if (pane.pendingChoice)
                pane.pendingChoice(name, body)
        }
    }

    Dialog {
        id: deleteDialog
        title: "Delete \"" + ServiceList.openTitle + "\"?"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: "The songs stay in the library." }
        onAccepted: ServiceList.removeService(ServiceList.openId)
    }
}
