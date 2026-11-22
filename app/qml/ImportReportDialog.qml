import QtQuick
import QtQuick.Controls
import Visualis.Ui

// The library's full import report, scrollable: the pane shows only a summary (#80).
Dialog {
    title: "Import report"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(640, parent.width - 48)
    height: Math.min(520, parent.height - 48)
    standardButtons: Dialog.Close

    ScrollView {
        anchors.fill: parent
        TextArea {
            objectName: "libraryReportText"
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            text: SongLibrary.report
        }
    }
}
