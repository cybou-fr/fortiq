import QtQuick
import QtQuick.Controls

// Detailed list view of the Files window.
ListView {
    required property Item files
    signal menuRequested()
    id: list
    visible: files.viewMode === "list"
    anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
    anchors.topMargin: files.inTrash ? 50 : 0
    model: files.shown
    currentIndex: -1
    keyNavigationEnabled: true
    boundsBehavior: Flickable.StopAtBounds
    reuseItems: true
    cacheBuffer: 600
    highlightMoveDuration: 0
    highlight: Rectangle { radius: 6; color: "#402F8CFF" }
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    readonly property real colDate: files.adaptive && files.width < 700 ? 0 : 132
    readonly property real colSize: files.adaptive ? 68 : 76
    readonly property real colMode: files.adaptive && files.width < 700 ? 0 : 88
    readonly property real colName: width - colDate - colSize - colMode - 24
    headerPositioning: ListView.OverlayHeader
    header: Rectangle {
        width: list.width; height: 28; z: 3
        color: "#E6101A2C"
        Row {
            x: 10; height: parent.height
            Repeater {
                model: [
                    { k: "name", t: "Nom", w: list.colName },
                    { k: "mtime", t: "Modifié", w: list.colDate },
                    { k: "size", t: "Taille", w: list.colSize },
                    { k: "", t: "Droits", w: list.colMode }
                ]
                Item {
                    required property var modelData
                    width: modelData.w; height: parent.height
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.t + (files.sortKey === modelData.k ? (files.sortAscending ? "  ▲" : "  ▼") : "")
                        color: "#9FB2D0"; font.pixelSize: 11; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: modelData.k.length > 0
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (files.sortKey === modelData.k) files.sortAscending = !files.sortAscending
                            else { files.sortKey = modelData.k; files.sortAscending = modelData.k === "name" }
                        }
                    }
                }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1FFFFFFF" }
    }
    delegate: Item {
        id: row
        required property var modelData
        required property int index
        width: list.width; height: files.adaptive ? 48 : 30
        Rectangle {
            anchors.fill: parent; radius: 6
            color: rowMouse.containsMouse && list.currentIndex !== row.index ? "#12FFFFFF"
                 : (row.index % 2 ? "#08FFFFFF" : "transparent")
        }
        Row {
            x: 10; height: parent.height
            Item {
                width: list.colName; height: parent.height
                FileIcon {
                    size: 20; anchors.verticalCenter: parent.verticalCenter
                    name: row.modelData.name; directory: row.modelData.directory === true; link: row.modelData.link === true
                }
                Text {
                    x: 30; width: parent.width - 36; anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideMiddle
                    text: row.modelData.name
                    color: row.modelData.hidden ? Theme.inkMuted : Theme.ink
                    font.pixelSize: 12; font.family: Theme.fontFamily
                }
            }
            Text { visible: list.colDate > 0; width: list.colDate; anchors.verticalCenter: parent.verticalCenter; text: files.formatDate(row.modelData.mtime); color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily }
            Text { width: list.colSize; anchors.verticalCenter: parent.verticalCenter; text: row.modelData.directory ? "—" : files.formatSize(row.modelData.size); color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily }
            Text { visible: list.colMode > 0; width: list.colMode; anchors.verticalCenter: parent.verticalCenter; text: row.modelData.mode || ""; color: "#8FA2C2"; font.pixelSize: 11; font.family: "Consolas" }
        }
        MouseArea {
            id: rowMouse
            anchors.fill: parent; hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (m) => {
                list.currentIndex = row.index; list.forceActiveFocus()
                if (m.button === Qt.RightButton) list.menuRequested()
                else if (files.adaptive) files.open(row.modelData)
            }
            onPressAndHold: { list.currentIndex = row.index; list.menuRequested() }
            onDoubleClicked: files.open(row.modelData)
        }
    }
    Keys.onReturnPressed: files.open(files.selected)
}
