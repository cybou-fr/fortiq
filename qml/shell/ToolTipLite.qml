import QtQuick

// Small delayed hint under its parent, in the shell's glass style.
Rectangle {
    id: tip
    property string text: ""
    property bool shown: false
    visible: opacity > 0
    opacity: shown && timer.elapsedEnough ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 120 } }
    z: 1000
    anchors.top: parent.bottom
    anchors.topMargin: 6
    anchors.horizontalCenter: parent.horizontalCenter
    width: label.implicitWidth + 16
    height: 22
    radius: 6
    color: "#F00B1424"
    border.width: 1
    border.color: "#40FFFFFF"

    Timer {
        id: timer
        property bool elapsedEnough: false
        interval: 450
        running: tip.shown
        onTriggered: elapsedEnough = true
    }
    onShownChanged: if (!shown) timer.elapsedEnough = false

    Text {
        id: label
        anchors.centerIn: parent
        text: tip.text
        color: Theme.ink
        font.pixelSize: 11
        font.family: Theme.fontFamily
    }
}
