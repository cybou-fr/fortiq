import QtQuick
import QtQuick.Controls

// Places, window controls and server footer of the Files window.
Rectangle {
    required property Item files
    id: sidebar
    visible: !files.drawer || files.navigationOpen
    z: files.drawer ? 40 : 0
    width: files.drawer ? Math.min(260, files.width - 32) : 172; height: parent.height
    topLeftRadius: 16; bottomLeftRadius: 16
    color: "#40050B18"
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#1FFFFFFF" }

    // Empty sidebar space is a window handle; places and window controls
    // above this area keep their own clicks and touch gestures.
    WindowDragArea {
        objectName: "filesSidebarDrag"
        anchors.fill: parent
        windowItem: files
        movable: !files.adaptive && !files.maximized
        onDoubleClicked: files.toggleMaximize()
    }

    Row {
        visible: !files.adaptive
        x: 14; y: 17; spacing: 9
        Repeater {
            model: [ { c: "#FF5F57", a: "close" }, { c: "#FEBC2E", a: "none" }, { c: "#28C840", a: "max" } ]
            Rectangle {
                required property var modelData
                width: 12; height: 12; radius: 6; color: modelData.c
                MouseArea {
                    anchors.fill: parent
                    onClicked: modelData.a === "close" ? files.closeRequested() : (modelData.a === "max" ? files.toggleMaximize() : null)
                }
            }
        }
    }

    Column {
        x: 8; y: 46; width: parent.width - 16
        spacing: 1
        Text { x: 10; text: "EMPLACEMENTS"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2; font.family: Theme.fontFamily; bottomPadding: 4 }
        Repeater {
            model: [
                { g: "⌂", t: "Dossier personnel", p: "~" },
                { g: "/", t: "Racine", p: "/" },
                { g: "≣", t: "Journaux", p: "/var/log" },
                { g: "⚙", t: "Configuration", p: "/etc" },
                { g: "◷", t: "Temporaire", p: "/tmp" },
                { g: "⛁", t: "Services web", p: "/var/www" },
                { g: "⏏", t: "Médias", p: "/media" },
                { g: "🗑", t: "Corbeille", p: "trash" }
            ]
            Rectangle {
                required property var modelData
                readonly property string target: modelData.p === "~" ? files.home
                    : (modelData.p === "trash" ? (files.connected ? files.controller.trashDirectory : "") : modelData.p)
                readonly property bool active: target.length > 0 && files.location === target
                width: parent.width; height: files.adaptive ? 44 : 29; radius: 7
                color: active ? Theme.selection : (placeMouse.containsMouse ? "#1AFFFFFF" : "transparent")
                SymbolGlyph { x: 10; anchors.verticalCenter: parent.verticalCenter; glyph: modelData.g; color: "white"; size: 13; width: 16 }
                Text { x: 33; anchors.verticalCenter: parent.verticalCenter; text: modelData.t; color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily }
                MouseArea {
                    id: placeMouse
                    anchors.fill: parent; hoverEnabled: true
                    enabled: files.connected
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { files.navigationOpen = false; files.filterText = ""; files.controller.navigate(parent.target) }
                }
            }
        }
    }

    Column {
        visible: files.connected || files.fixture
        anchors.bottom: parent.bottom; anchors.bottomMargin: 12
        x: 16; width: parent.width - 32
        spacing: 4
        Text { text: "SERVEUR"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2; font.family: Theme.fontFamily }
        Row {
            spacing: 6
            Rectangle { width: 7; height: 7; radius: 3.5; anchors.verticalCenter: parent.verticalCenter; color: "#4ADE80" }
            Text {
                width: 120; elide: Text.ElideMiddle
                text: files.fixture ? "demo@linux" : (files.connected ? files.controller.serverLabel : "")
                color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily
            }
        }
        Text {
            visible: !files.fixture
            text: "Se déconnecter"
            color: "#9CC6FF"; font.pixelSize: 11; font.underline: discoMouse.containsMouse; font.family: Theme.fontFamily
            MouseArea { id: discoMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: files.controller.disconnectServer() }
        }
    }
}
