import QtQuick

// Restore / empty actions shown above the listing while the Trash is open.
Rectangle {
    required property Item files
    id: trashBanner
    visible: files.inTrash
    z: 4
    x: 10; y: 8; width: parent.width - 20; height: 36; radius: 10
    color: "#1FFFFFFF"
    border.width: 1; border.color: "#26FFFFFF"
    Text {
        x: 12; anchors.verticalCenter: parent.verticalCenter
        width: parent.width - trashActions.width - 30; elide: Text.ElideRight
        text: "🗑  Corbeille · " + files.entries.length + " élément" + (files.entries.length > 1 ? "s" : "")
              + "  —  les éléments peuvent être restaurés à leur emplacement d'origine"
        color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily
    }
    Row {
        id: trashActions
        anchors.right: parent.right; anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        Repeater {
            model: [
                { k: "restore", t: "Restaurer", on: files.selected !== null, danger: false },
                { k: "empty", t: "Vider la corbeille…", on: files.entries.length > 0, danger: true }
            ]
            Rectangle {
                required property var modelData
                height: 26; radius: 7
                width: bl.implicitWidth + 20
                opacity: modelData.on && !(files.connected && files.controller.operationBusy) ? 1 : 0.4
                color: modelData.danger ? (bm.containsMouse ? "#C2410C" : "#80C2410C") : (bm.containsMouse ? Theme.selection : "#802D6FE0")
                Text { id: bl; anchors.centerIn: parent; text: modelData.t; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                MouseArea {
                    id: bm
                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    enabled: modelData.on && files.connected && !files.controller.operationBusy
                    onClicked: modelData.k === "restore" ? files.restore(files.selected) : files.askEmptyTrash()
                }
            }
        }
    }
}
