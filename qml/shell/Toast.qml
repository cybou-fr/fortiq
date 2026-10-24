import QtQuick

// A short notice at the bottom of the desktop (copied text, refused action). Fades by itself.
Rectangle {
    id: toast
    objectName: "routeToast"
    property string text: ""
    property bool adaptive: false
    function show(message) { text = message; visible = true; hideTimer.restart() }
    visible: false
    anchors.horizontalCenter: parent.horizontalCenter
    y: parent.height - height - (adaptive ? 110 : 96)
    width: Math.min(parent.width - 24, label.implicitWidth + 32); height: label.implicitHeight + 20; radius: 12
    color: "#E61B2740"; border.width: 1; border.color: "#55FFFFFF"; z: 900
    Text { id: label; anchors.centerIn: parent; width: Math.min(toast.parent.width - 56, implicitWidth); wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
           text: toast.text; color: Theme.ink; font.pixelSize: toast.adaptive ? 14 : 12; font.family: Theme.fontFamily }
    Timer { id: hideTimer; interval: 4000; onTriggered: toast.visible = false }
}
