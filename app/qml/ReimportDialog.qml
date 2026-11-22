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
    readonly property bool upToDate: ready && PlanImporter.reimportChanges.length === 0 && !PlanImporter.reimportNeedsSave
    onRejected: PlanImporter.cancelReimport()   // Cancel, Escape, or a click outside

    function line(c) {
        switch (c.kind) {
        case "added": return (c.pcoItemId !== "" ? "Adding back: " : "Added: ") + c.title
        case "removed": return "Removed in Planning Center: " + c.title + " (kept here, marked removed)"
        case "restored": return "Back in the plan: " + c.title
        case "moved": return "Moved: " + c.title
        case "changed": return c.title + ": " + c.field + " " + c.detail
        case "keptEdit": return c.title + ": " + c.field + " " + c.detail
        case "keptPosition": return c.title + ": kept where you put it; Planning Center moved it"
        case "relink": return c.title + ": Planning Center has another song here, so it needs a song again"
        case "changedButDeleted": return c.title + ": " + c.detail
        case "linked": return c.title + ": " + c.detail
        case "unreadable": return c.title !== "" ? c.title + ": " + c.detail : c.detail.charAt(0).toUpperCase() + c.detail.slice(1)
        }
        return c.title
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // The service changed while this was open: a new diff to look at before applying.
        Label {
            objectName: "reimportNotice"
            Layout.fillWidth: true
            visible: dialog.ready && PlanImporter.reimportNotice !== ""
            wrapMode: Text.Wrap
            font.bold: true
            text: PlanImporter.reimportNotice
        }

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
            visible: dialog.ready && PlanImporter.reimportChanges.length > 0
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
                // Ticking it works the diff out again, so what's shown is what Apply does.
                CheckBox {
                    objectName: "addBack" + row.index
                    visible: row.modelData.pcoItemId !== ""
                    checked: row.modelData.kind === "added"
                    text: "Add back"
                    onToggled: dialog.toggleAddBack(row.modelData.pcoItemId, checked)
                }
            }
        }

        // Partly read: what Planning Center sent that couldn't be used.
        Label {
            objectName: "reimportNotes"
            Layout.fillWidth: true
            visible: dialog.ready && PlanImporter.reimportNotes.length > 0
            wrapMode: Text.Wrap
            color: "#8b8f96"
            text: PlanImporter.reimportNotes.join("\n")
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

    function toggleAddBack(pcoItemId, on) {
        const ids = []
        for (const c of PlanImporter.reimportChanges) {
            if (c.kind === "added" && c.pcoItemId !== "" && c.pcoItemId !== pcoItemId)
                ids.push(c.pcoItemId)
        }
        if (on)
            ids.push(pcoItemId)
        PlanImporter.setReimportAddBack(ids)
    }

    onAccepted: PlanImporter.applyReimport()
}
