import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Visualis.Ui

// Sign in to Planning Center with a Personal Access Token (#52). The token goes to the
// macOS Keychain only after Planning Center accepts it.
Dialog {
    id: dialog
    objectName: "planningCenterDialog"
    title: "Planning Center"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 560
    standardButtons: Dialog.Close

    onOpened: {
        appId.text = ""
        secret.text = ""
    }

    Connections {
        target: PlanningCenter
        function onSignInFinished(ok) {
            if (ok)
                secret.text = ""
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            Layout.fillWidth: true
            visible: PlanningCenter.signedIn
            text: PlanningCenter.organization !== ""
                  ? "Signed in to <b>" + PlanningCenter.organization + "</b>."
                  : "Signed in."
            textFormat: Text.StyledText
        }

        Label {
            Layout.fillWidth: true
            visible: !PlanningCenter.signedIn
            wrapMode: Text.WordWrap
            textFormat: Text.StyledText
            onLinkActivated: link => Qt.openUrlExternally(link)
            text: "Make a Personal Access Token at "
                  + "<a href=\"https://api.planningcenteronline.com/oauth/applications\">api.planningcenteronline.com</a>. "
                  + "<b>Switch to your church's organization first</b>: a token only sees the organization it was made in."
        }
        TextField {
            id: appId
            objectName: "pcoAppId"
            Layout.fillWidth: true
            visible: !PlanningCenter.signedIn
            placeholderText: "Application ID"
        }
        TextField {
            id: secret
            objectName: "pcoSecret"
            Layout.fillWidth: true
            visible: !PlanningCenter.signedIn
            placeholderText: "Secret"
            echoMode: TextInput.Password
        }

        RowLayout {
            spacing: 8
            Button {
                text: "Sign in"
                visible: !PlanningCenter.signedIn
                enabled: !PlanningCenter.busy && appId.text !== "" && secret.text !== ""
                onClicked: PlanningCenter.signIn(appId.text, secret.text)
            }
            Button {
                text: "Test connection"
                visible: PlanningCenter.signedIn
                enabled: !PlanningCenter.busy
                onClicked: PlanningCenter.testConnection()
            }
            Button {
                text: "Sign out"
                visible: PlanningCenter.signedIn
                enabled: !PlanningCenter.busy
                onClicked: PlanningCenter.signOut()
            }
        }

        Label {
            objectName: "pcoStatus"
            Layout.fillWidth: true
            visible: text !== ""
            text: PlanningCenter.status
            wrapMode: Text.WordWrap
            color: "#8b8f96"
        }
    }
}
