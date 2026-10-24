import QtQuick
import QtQuick.Controls.Fusion

// Icon view of the Files window.
GridView {
    required property Item files
    signal menuRequested()
    id: grid
    visible: files.viewMode === "grid"
    anchors.fill: parent; anchors.margins: 10
    anchors.topMargin: files.inTrash ? 52 : 10
    cellWidth: Math.max(104, width / Math.max(1, Math.floor(width / 116)))
    cellHeight: 108
    model: files.shown
    currentIndex: -1
    keyNavigationEnabled: true
    boundsBehavior: Flickable.StopAtBounds
    cacheBuffer: 400
    reuseItems: true
    highlightFollowsCurrentItem: true
    highlightMoveDuration: 0
    highlight: Rectangle { radius: 10; color: "#402F8CFF"; border.width: 1; border.color: "#992F8CFF" }
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    delegate: Item {
        id: cell
        required property var modelData
        required property int index
        width: grid.cellWidth; height: grid.cellHeight
        Rectangle {
            anchors.fill: parent; anchors.margins: 3; radius: 10
            color: cellMouse.containsMouse && grid.currentIndex !== cell.index ? "#14FFFFFF" : "transparent"
        }
        FileIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 10; size: 52
            name: cell.modelData.name; directory: cell.modelData.directory === true; link: cell.modelData.link === true
            opacity: cell.modelData.hidden ? 0.6 : 1
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 68; width: parent.width - 12
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere; maximumLineCount: 2; elide: Text.ElideMiddle
            text: cell.modelData.name
            color: Theme.ink
            font.pixelSize: 12; font.family: Theme.fontFamily
        }
        MouseArea {
            id: cellMouse
            anchors.fill: parent; hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (m) => {
                grid.currentIndex = cell.index; grid.forceActiveFocus()
                if (m.button === Qt.RightButton) grid.menuRequested()
                else if (files.adaptive) files.open(cell.modelData)
            }
            onPressAndHold: { grid.currentIndex = cell.index; grid.menuRequested() }
            onDoubleClicked: files.open(cell.modelData)
        }
    }
    Keys.onReturnPressed: files.open(files.selected)
}
