pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Visualis.Ui

// What the people on stage see (plan 5.5): song, group, and position on top with a clock,
// the current slide large, and the next slide below. Text-only, high contrast. When the
// audience text is cleared it stays here, dimmed, so singers don't lose their place.
Item {
    id: root

    readonly property real unit: height / 100   // sizes in hundredths of the output's height

    component Fitted: Text {
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        fontSizeMode: Text.Fit
        minimumPixelSize: Math.max(1, Math.round(root.unit * 2.5))
        elide: Text.ElideRight
        font.family: "Helvetica Neue"   // the stage keeps its own layout; stage templates come later
        font.weight: Font.DemiBold
        verticalAlignment: Text.AlignTop
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.unit * 3
        spacing: root.unit * 2

        RowLayout {
            Layout.fillWidth: true
            spacing: root.unit * 2

            Rectangle {
                objectName: "stageGroup"
                visible: LiveState.groupLabel !== ""
                color: LiveState.groupColor !== "" ? LiveState.groupColor : "#555"
                radius: root.unit
                Layout.preferredHeight: groupLabel.implicitHeight + root.unit * 1.5
                Layout.preferredWidth: groupLabel.implicitWidth + root.unit * 3
                Text {
                    id: groupLabel
                    anchors.centerIn: parent
                    text: LiveState.groupLabel
                    color: "white"
                    font.pixelSize: Math.round(root.unit * 5)
                    font.weight: Font.Bold
                }
            }
            Text {
                objectName: "stageTitle"
                Layout.fillWidth: true
                text: LiveState.hasSlide
                      ? LiveState.songTitle + "   " + (LiveState.slideIndex + 1) + " / " + LiveState.slideCount
                      : LiveState.songTitle
                color: "#9aa4b2"
                elide: Text.ElideRight
                font.pixelSize: Math.round(root.unit * 4.5)
            }
            Text {
                id: clock
                objectName: "stageClock"
                color: "white"
                font.pixelSize: Math.round(root.unit * 6)
                font.family: "Menlo"
                Timer {
                    interval: 500; running: true; repeat: true; triggeredOnStart: true
                    onTriggered: clock.text = Qt.formatTime(new Date(), "h:mm:ss")
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 60

            Fitted {
                objectName: "stageCurrent"
                anchors.fill: parent
                text: LiveState.currentText
                color: "white"
                opacity: LiveState.textCleared ? 0.35 : 1
                font.pixelSize: Math.round(root.unit * 10)
            }
            Text {
                objectName: "stageCleared"
                visible: LiveState.hasSlide && LiveState.textCleared
                anchors.right: parent.right
                anchors.top: parent.top
                text: "TEXT CLEARED"
                color: "#e0b040"
                font.pixelSize: Math.round(root.unit * 3.5)
                font.weight: Font.Bold
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(1, root.unit * 0.3)
            color: "#444"
        }

        Fitted {
            objectName: "stageNext"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 30
            // Not nextText === "": a blank slide (an intro) is empty too.
            text: !LiveState.hasSlide ? ""
                  : LiveState.slideIndex + 1 < LiveState.slideCount ? LiveState.nextText : "(end of song)"
            color: "#e8b04a"
            font.pixelSize: Math.round(root.unit * 7)
        }
    }
}
