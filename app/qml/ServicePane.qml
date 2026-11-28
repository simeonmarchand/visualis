pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Visualis.Ui

// The open service's items (#51): pick, create, rename, and delete services; add the
// library's selected song, headers, and text or blank slides; drag items to reorder
// (Option+Up/Down moves the selected one). Click an item to see its slides. Drop media from
// the media pane to add it where it lands (#99); the item and service menus set backgrounds.
ColumnLayout {
    id: pane
    spacing: 8

    readonly property bool dialogOpen: nameDialog.visible || deleteDialog.visible || pasteDialog.visible || reimportDialog.visible
                                       || backgroundPicker.visible
    readonly property var selectedItem: ServiceList.items[ServiceList.selectedIndex] ?? null
    // The selected item, if it's an imported song not linked to the library yet (#55).
    readonly property var selectedPlaceholder: {
        const items = ServiceList.items
        const i = ServiceList.selectedIndex
        return i >= 0 && i < items.length && items[i].songPlaceholder ? items[i] : null
    }
    readonly property int rowHeight: 34
    property int dragFrom: -1
    property int dropAt: -1
    property int mediaDropAt: -1   // where dragged media will go in, between rows

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
                anchors.rightMargin: warning.visible ? warning.width + 12 : 8
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: row.modelData.pcoRemoved ? row.modelData.title + " (removed in Planning Center)"
                      : !row.modelData.songPlaceholder ? row.modelData.title
                      : row.modelData.suggestions.length > 0
                        ? row.modelData.title + " (confirm: " + row.modelData.suggestions[0].title + "?)"
                        : row.modelData.title + " (song not in library)"
                font.bold: row.isHeader
                font.strikeout: row.modelData.pcoRemoved
                font.capitalization: row.isHeader ? Font.AllUppercase : Font.MixedCase
                color: row.isHeader ? "#8b8f96" : (row.modelData.hasSlides ? "#d0d3d8" : "#6b6f76")
            }

            Rectangle {   // it shows, but not as asked (#93): a missing template, a bad override
                id: warning
                objectName: "itemWarning" + row.index
                visible: row.modelData.warnings !== ""
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                radius: 9
                color: "#c98a12"
                Label { anchors.centerIn: parent; text: "!"; color: "black"; font.bold: true }
                HoverHandler { id: warningHover }
                ToolTip.visible: warningHover.hovered
                ToolTip.text: row.modelData.warnings
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
                            (row.modelData.songPlaceholder ? resolveMenu : itemMenu).popup()
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

        Rectangle {   // where dropped media will go in
            parent: list.contentItem
            visible: pane.mediaDropAt >= 0
            width: list.width
            height: 2
            color: "#4a90e2"
            y: pane.mediaDropAt * pane.rowHeight - 1
        }

        DropArea {
            objectName: "serviceMediaDrop"
            anchors.fill: parent
            keys: ["application/x-visualis-media"]
            enabled: ServiceList.openId !== ""
            function rowAt(y) {
                return Math.max(0, Math.min(list.count, Math.round((y + list.contentY) / pane.rowHeight)))
            }
            onPositionChanged: drag => pane.mediaDropAt = rowAt(drag.y)
            onExited: pane.mediaDropAt = -1
            onDropped: drop => {
                const media = drop.source as MediaDrag
                if (media)
                    ServiceList.addMedia(media.assetId, media.mediaTitle, rowAt(drop.y))
                pane.mediaDropAt = -1
                drop.accept()
            }
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
            id: resolveButton
            objectName: "resolvePlaceholder"
            text: "Resolve…"
            focusPolicy: Qt.NoFocus
            visible: pane.selectedPlaceholder !== null
            onClicked: resolveMenu.popup(resolveButton, 0, resolveButton.height)
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
        MenuItem {
            objectName: "reimportService"
            text: "Update from Planning Center…"
            enabled: ServiceList.openImported && !PlanImporter.busy
            onTriggered: {
                reimportDialog.open()
                PlanImporter.reimport(ServiceList.openId)
            }
        }
        MenuSeparator {}
        MenuItem {
            objectName: "serviceBackground"
            text: "Default background…"
            enabled: ServiceList.openId !== ""
            onTriggered: backgroundPicker.pick(ServiceList.serviceBackgroundId, id => ServiceList.setServiceBackground(id))
        }
        MenuSeparator {}
        MenuItem { text: "Delete…"; enabled: ServiceList.openId !== ""; onTriggered: deleteDialog.open() }
    }

    // Shows on the next slide, not at once: a background changes only when a slide does.
    BackgroundPicker {
        id: backgroundPicker
        objectName: "serviceBackgroundPicker"
    }
    // By id, as the items may change while the picker is open.
    function indexOfItem(id) {
        return ServiceList.items.findIndex(it => it.id === id)
    }

    ReimportDialog { id: reimportDialog }
    // Also when the plan picker re-imports a plan that's already a service.
    Connections {
        target: PlanImporter
        function onReimportChanged() {
            if (PlanImporter.reimportServiceId !== "" && !reimportDialog.visible)
                reimportDialog.open()
        }
    }

    Menu {
        id: addMenu
        MenuItem { text: "Header…"; onTriggered: pane.ask("New header", "Header, e.g. Worship", false, "", name => ServiceList.addHeader(name)) }
        MenuItem { text: "Text…"; onTriggered: pane.ask("New text slide", "Title", true, "", (name, body) => ServiceList.addText(name, body)) }
        MenuItem { text: "Blank slide"; onTriggered: ServiceList.addText("Blank", "") }
    }

    Menu {
        id: itemMenu
        // The template for this service only (#95); the song keeps its own.
        Menu {
            id: templateMenu
            objectName: "itemTemplateMenu"
            title: "Template"
            readonly property string chosen: ServiceList.items[ServiceList.selectedIndex]?.templateId ?? ""
            MenuItem {
                text: "The song's own"
                checkable: true
                checked: templateMenu.chosen === ""
                onTriggered: ServiceList.setItemTemplate(ServiceList.selectedIndex, "")
            }
            MenuSeparator {}
            Instantiator {
                model: TemplateLibrary.entries.filter(t => t.kind === "lyrics")
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.name
                    checkable: true
                    checked: templateMenu.chosen === modelData.id
                    onTriggered: ServiceList.setItemTemplate(ServiceList.selectedIndex, modelData.id)
                }
                onObjectAdded: (index, object) => templateMenu.insertItem(index + 2, object)
                onObjectRemoved: (index, object) => templateMenu.removeItem(object)
            }
        }
        // Over the song's and its groups' own, for this service only (#99).
        MenuItem {
            objectName: "itemBackground"
            text: "Background…"
            enabled: pane.selectedItem?.canHaveBackground ?? false
            onTriggered: {
                const itemId = pane.selectedItem.id
                backgroundPicker.pick(pane.selectedItem.backgroundId,
                                      id => ServiceList.setItemBackground(pane.indexOfItem(itemId), id))
            }
        }
        MenuItem { text: "Remove from service"; onTriggered: ServiceList.removeItem(ServiceList.selectedIndex) }
    }

    // An imported song with no library song yet: confirm a suggestion, link the song picked
    // in the library (search there first), make it from pasted lyrics, or leave a blank slide.
    Menu {
        id: resolveMenu
        Instantiator {
            model: pane.selectedPlaceholder ? pane.selectedPlaceholder.suggestions : []
            delegate: MenuItem {
                required property var modelData
                text: "Use \u201c" + modelData.title + "\u201d"
                onTriggered: ServiceList.linkSong(ServiceList.selectedIndex, modelData.id)
            }
            onObjectAdded: (index, object) => resolveMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => resolveMenu.removeItem(object)
        }
        MenuItem {
            text: SongLibrary.selectedTitle !== "" ? "Use library song \u201c" + SongLibrary.selectedTitle + "\u201d"
                                                   : "Use library song (pick one in the library first)"
            enabled: SongLibrary.selectedId !== ""
            onTriggered: ServiceList.linkSong(ServiceList.selectedIndex, SongLibrary.selectedId)
        }
        MenuItem {
            text: "New song from pasted lyrics…"
            onTriggered: {
                pasteDialog.forIndex = ServiceList.selectedIndex
                pasteDialog.initialTitle = pane.selectedPlaceholder ? pane.selectedPlaceholder.title : ""
                pasteDialog.open()
            }
        }
        MenuItem { text: "Leave as a blank slide"; onTriggered: ServiceList.leaveBlank(ServiceList.selectedIndex) }
        MenuSeparator {}
        MenuItem { text: "Remove from service"; onTriggered: ServiceList.removeItem(ServiceList.selectedIndex) }
    }

    PasteSongDialog {
        id: pasteDialog
        property int forIndex: -1
        onCreated: songId => ServiceList.linkSong(forIndex, songId)
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
