import QtQuick
import QtQuick.Controls.Fusion

// The primary action of an empty or error state ("Choisir un serveur…"): one size and colour in every application,
// large enough to hit with a finger (touch = 48 px).
Button {
    id: button
    property bool touch: false
    implicitWidth: Math.max(190, label.implicitWidth + 44)
    implicitHeight: touch ? 48 : 38
    hoverEnabled: true
    background: Rectangle {
        radius: 10
        color: !button.enabled ? "#33FFFFFF" : (button.down ? Theme.accentDeep : (button.hovered ? Theme.accent : Theme.selection))
        border.width: button.visualFocus ? 2 : 0
        border.color: "#CFE3FF"
    }
    contentItem: Text {
        id: label
        text: button.text
        textFormat: Text.PlainText
        color: button.enabled ? "white" : Theme.inkMuted
        font.pixelSize: button.touch ? 15 : 14
        font.family: Theme.fontFamily
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
