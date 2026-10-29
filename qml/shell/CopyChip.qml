import QtQuick

// Small text pill used for the actions that move work between apps ("Journaux", "Terminal ici") and for "Copier".
Rectangle {
    id: chip
    property string label: "Copier"
    property bool touch: false
    signal clicked()
    readonly property bool iconOnly: label === "Copier"
    height: touch ? 40 : 22; radius: height / 2
    width: iconOnly ? (touch ? 40 : 28) : chipText.implicitWidth + 22
    color: chipMouse.containsMouse ? "#40FFFFFF" : "#26FFFFFF"
    border.width: 1; border.color: "#33FFFFFF"
    Text { id: chipText; anchors.centerIn: parent; text: chip.iconOnly ? "\u29c9" : chip.label; color: Theme.ink; font.pixelSize: chip.iconOnly ? (chip.touch ? 18 : 14) : (chip.touch ? 13 : 11); font.family: Theme.fontFamily }
    MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: chip.clicked() }
    ToolTipLite { text: "Copier"; shown: chip.iconOnly && chipMouse.containsMouse }
}
