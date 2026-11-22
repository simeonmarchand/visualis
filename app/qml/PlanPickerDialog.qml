pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Visualis.Ui

// Pick a Planning Center plan and import it into a new service (#54). Upcoming plans come
// first. Nothing here waits on the network: lists fill in when Planning Center answers.
Dialog {
    id: dialog
    objectName: "planPickerDialog"
    title: "Import a plan from Planning Center"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 560
    standardButtons: Dialog.Close

    readonly property string serviceTypeId: types.currentIndex >= 0 && types.currentIndex < PlanImporter.serviceTypes.length
                                            ? PlanImporter.serviceTypes[types.currentIndex].id : ""
    readonly property var plan: plans.currentIndex >= 0 && plans.currentIndex < PlanImporter.plans.length
                                ? PlanImporter.plans[plans.currentIndex] : null

    onOpened: PlanImporter.loadServiceTypes()
    onServiceTypeIdChanged: if (serviceTypeId !== "") PlanImporter.loadPlans(serviceTypeId)

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        ComboBox {
            id: types
            objectName: "serviceTypes"
            Layout.fillWidth: true
            model: PlanImporter.serviceTypes
            textRole: "name"
        }

        ListView {
            id: plans
            objectName: "plans"
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            clip: true
            model: PlanImporter.plans
            currentIndex: 0
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                text: modelData.label + "  (" + modelData.itemsCount + (modelData.itemsCount === 1 ? " item)" : " items)")
                onClicked: plans.currentIndex = index
                onDoubleClicked: PlanImporter.importPlan(dialog.serviceTypeId, modelData.id)
            }
            ScrollBar.vertical: ScrollBar {}
        }

        RowLayout {
            spacing: 8
            Button {
                objectName: "importPlan"
                text: "Import"
                enabled: !PlanImporter.busy && dialog.plan !== null
                onClicked: PlanImporter.importPlan(dialog.serviceTypeId, dialog.plan.id)
            }
            Button {
                text: "Open in Planning Center"
                enabled: dialog.plan !== null && dialog.plan.url !== ""
                onClicked: Qt.openUrlExternally(dialog.plan.url)
            }
        }

        Label {
            objectName: "planImportStatus"
            Layout.fillWidth: true
            visible: text !== ""
            text: PlanImporter.status
            wrapMode: Text.WordWrap
            color: "#8b8f96"
        }
    }
}
