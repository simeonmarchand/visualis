pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Visualis.Ui

// What a re-import would change (#56), shown before anything is saved: Apply or Cancel.
// The operator's own edits are always kept; the list says where Planning Center differs.
Dialog {
    id: dialog
    objectName: "reimportDialog"
    title: "Update from Planning Center"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 560

    readonly property bool ready: PlanImporter.reimportServiceId !== ""
    readonly property bool upToDate: ready && PlanImporter.reimportChanges.length === 0
    property var addBack: ({})   // Planning Center item id -> true, for deleted items to add back

    onOpened: addBack = ({})
    onRejected: PlanImporter.cancelReimport()   // Cancel, Escape, or a click outside

    function line(c) {
        switch (c.kind) {
        case "added": return "Added: " + c.title
        case "removed": return "Removed in Planning Center: " + c.title + " (kept here, marked removed)"
        case "restored": return "Back in the plan: " + c.title
        case "moved": return "Moved: " + c.title
        case "changed": return c.title + ": " + c.field + " " + c.detail
        case "keptEdit": return c.title + ": " + c.field + " " + c.detail
        case "keptPosition": return c.title + ": kept where you put it; Planning Center moved it"
        case "relink": return c.title + ": Planning Center has another song here, so it needs a song again"
        case "changedButDeleted": return c.title + ": " + c.detail
        }
        return c.title
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: dialog.ready ? PlanImporter.reimportSummary : PlanImporter.status
        }

        ListView {
            objectName: "reimportChanges"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 320)
            clip: true
            visible: dialog.ready && !dialog.upToDate
            model: PlanImporter.reimportChanges
            delegate: RowLayout {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: dialog.line(row.modelData)
                    color: row.modelData.kind === "keptEdit" || row.modelData.kind === "keptPosition" ? "#8b8f96" : palette.text
                }
                CheckBox {
                    objectName: "addBack" + row.index
                    visible: row.modelData.kind === "changedButDeleted"
                    text: "Add back"
                    onToggled: {
                        const next = Object.assign({}, dialog.addBack)
                        next[row.modelData.pcoItemId] = checked
                        dialog.addBack = next
                    }
                }
            }
        }
    }

    footer: DialogButtonBox {
        Button {
            objectName: "applyReimport"
            text: "Apply"
            enabled: dialog.ready && !dialog.upToDate && !PlanImporter.busy
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            text: dialog.upToDate ? "Close" : "Cancel"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    onAccepted: {
        const ids = Object.keys(addBack).filter(k => addBack[k])
        PlanImporter.applyReimport(ids)
    }
}
