import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Asks for a name, and optionally some text: new and renamed services, headers, text items.
Dialog {
    id: dialog
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 420
    standardButtons: Dialog.Ok | Dialog.Cancel

    property string placeholder: "Name"
    property bool withBody: false
    property string initialName: ""

    signal chosen(string name, string body)

    onOpened: {
        nameField.text = initialName
        bodyArea.text = ""
        nameField.selectAll()
        nameField.forceActiveFocus()
    }
    onAccepted: chosen(nameField.text, bodyArea.text)

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        TextField {
            id: nameField
            objectName: "nameField"
            Layout.fillWidth: true
            placeholderText: dialog.placeholder
            onAccepted: dialog.accept()
        }
        TextArea {
            id: bodyArea
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            visible: dialog.withBody
            placeholderText: "Text (leave empty for a blank slide)"
            wrapMode: TextArea.Wrap
        }
    }
}
