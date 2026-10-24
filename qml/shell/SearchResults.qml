import QtQuick
import QtQuick.Controls.Fusion

// Server search results over the Files content: name or content matches
// below the folder where the search started. Double-click opens a file in
// the viewer or enters a folder; "Emplacement" opens the containing folder.
Rectangle {
    id: panel
    property var controller: null
    signal openEntry(var entry)
    signal openLocation(string folder)
    signal closed()

    readonly property var results: controller ? controller.searchResults : []
    readonly property string scope: controller ? controller.searchScope : ""
    readonly property bool busy: controller && controller.searchBusy

    color: "#FA0D1626"
    focus: visible
    onVisibleChanged: if (visible) list.forceActiveFocus()

    function escapeHtml(s) { return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }
    // Highlights the query (case-insensitive) in a snippet; plain text otherwise.
    function highlight(text) {
        const q = controller ? controller.searchQuery : ""
        const t = String(text)
        const i = q.length > 0 ? t.toLowerCase().indexOf(q.toLowerCase()) : -1
        if (i < 0) return escapeHtml(t)
        return escapeHtml(t.slice(0, i)) + "<b><font color='#7CC4FF'>"
            + escapeHtml(t.slice(i, i + q.length)) + "</font></b>" + escapeHtml(t.slice(i + q.length))
    }
    function relative(path) {
        const folder = path.slice(0, path.lastIndexOf("/"))
        if (folder === scope) return "."
        return folder.indexOf(scope + "/") === 0 ? folder.slice(scope.length + 1) : folder
    }

    Keys.onEscapePressed: panel.closed()

    Item {
        id: header
        width: parent.width; height: 44
        Column {
            x: 14; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 28 - actions.width
            Text {
                width: parent.width; elide: Text.ElideRight
                text: "⌕  « " + (panel.controller ? panel.controller.searchQuery : "") + " »  ·  "
                      + (panel.controller && panel.controller.searchMode === "content" ? "contenu" : "nom")
                      + "  ·  dans " + panel.scope
                color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold; font.family: Theme.fontFamily
            }
            Text {
                width: parent.width; elide: Text.ElideRight
                text: panel.controller ? panel.controller.searchInfo : ""
                color: Theme.inkMuted; font.pixelSize: 10; font.family: Theme.fontFamily
            }
        }
        Row {
            id: actions
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            BusyIndicator { running: panel.busy; visible: running; implicitWidth: 24; implicitHeight: 24 }
            Rectangle {
                visible: panel.busy
                anchors.verticalCenter: parent.verticalCenter
                height: 24; radius: 6; width: stopLabel.implicitWidth + 16
                color: stopMouse.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
                Text { id: stopLabel; anchors.centerIn: parent; text: "Arrêter"; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                MouseArea { id: stopMouse; anchors.fill: parent; hoverEnabled: true; onClicked: panel.controller.cancelSearch() }
            }
            ToolButtonGlass { glyph: "×"; tip: "Fermer la recherche (Échap)"; onClicked: panel.closed() }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1FFFFFFF" }
    }

    ListView {
        id: list
        y: header.height + 4
        x: 6; width: parent.width - 12; height: parent.height - y - 4
        model: panel.results
        clip: true
        currentIndex: -1
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        highlightMoveDuration: 0
        highlight: Rectangle { radius: 6; color: "#402F8CFF" }
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Keys.onReturnPressed: if (currentIndex >= 0) panel.openEntry(panel.results[currentIndex])
        delegate: Item {
            id: row
            required property var modelData
            required property int index
            readonly property bool hasText: modelData.text !== undefined
            width: list.width; height: hasText ? 46 : 34
            Rectangle { anchors.fill: parent; radius: 6; color: rowMouse.containsMouse && list.currentIndex !== row.index ? "#12FFFFFF" : "transparent" }
            FileIcon {
                x: 8; y: 7; size: 20
                name: row.modelData.name; directory: row.modelData.directory === true; link: row.modelData.link === true
            }
            Text {
                x: 36; y: 4; width: parent.width - x - locate.width - 16
                elide: Text.ElideMiddle
                textFormat: Text.StyledText
                text: "<b>" + panel.highlight(row.modelData.name) + "</b>  <font color='#7F93B5'>" + panel.escapeHtml(panel.relative(row.modelData.path)) + "</font>"
                color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily
            }
            Text {
                visible: row.hasText
                x: 36; y: 23; width: parent.width - x - 16
                elide: Text.ElideRight
                textFormat: Text.StyledText
                text: "<font color='#7F93B5'>" + row.modelData.line + ":</font> " + panel.highlight(row.modelData.text || "")
                color: "#CFDAEC"; font.pixelSize: 11; font.family: Theme.monoFamily
            }
            Text {
                id: locate
                anchors.right: parent.right; anchors.rightMargin: 10; y: 6
                text: "Emplacement"
                visible: rowMouse.containsMouse || list.currentIndex === row.index
                color: "#9CC6FF"; font.pixelSize: 10; font.underline: locMouse.containsMouse; font.family: Theme.fontFamily
                MouseArea {
                    id: locMouse
                    anchors.fill: parent; anchors.margins: -4; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; z: 2
                    onClicked: panel.openLocation(row.modelData.path.slice(0, row.modelData.path.lastIndexOf("/")) || "/")
                }
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent; hoverEnabled: true
                onClicked: { list.currentIndex = row.index; list.forceActiveFocus() }
                onDoubleClicked: panel.openEntry(row.modelData)
            }
        }
    }

    Text {
        visible: !panel.busy && panel.results.length === 0
        anchors.centerIn: parent
        text: panel.controller ? panel.controller.searchInfo : ""
        color: Theme.inkMuted; font.pixelSize: 13; font.family: Theme.fontFamily
    }
}
