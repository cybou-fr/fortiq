import QtQuick
import QtQuick.Controls

// Navigation, path bar, filter field and view tools of the Files window (also its drag handle).
Item {
    required property Item files
    property real adaptiveHeight: 0
    readonly property Item searchField: search
    id: toolbar
    x: files.contentLeft; width: parent.width - x; height: files.adaptive ? toolbar.adaptiveHeight : 48

    WindowDragArea {
        anchors.fill: parent
        windowItem: files
        movable: !files.adaptive && !files.maximized
        onDoubleClicked: files.toggleMaximize()
    }

    Row {
        id: nav
        visible: !files.adaptive
        x: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 2
        ToolButtonGlass { glyph: "‹"; tip: "Précédent (Alt+←)"; enabledState: files.connected && files.controller.canGoBack; onClicked: files.controller.goBack() }
        ToolButtonGlass { glyph: "›"; tip: "Suivant (Alt+→)"; enabledState: files.connected && files.controller.canGoForward; onClicked: files.controller.goForward() }
        ToolButtonGlass { glyph: "↑"; tip: "Dossier parent (Alt+↑)"; enabledState: files.connected && files.location !== "/"; onClicked: files.controller.goUp() }
    }

    Rectangle {
        id: pathBar
        visible: !files.adaptive
        anchors.left: nav.right; anchors.leftMargin: 6
        anchors.right: tools.left; anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        height: 30; radius: 8
        color: "#1AFFFFFF"
        border.width: 1; border.color: files.editingPath ? Theme.accent : "#1FFFFFFF"
        clip: true

        Flickable {
            id: crumbFlick
            visible: !files.editingPath
            anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
            contentWidth: crumbRow.width; contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            onContentWidthChanged: contentX = Math.max(0, contentWidth - width)
            Row {
                id: crumbRow
                height: parent.height
                Repeater {
                    model: files.fixture ? [{ label: "⌂ Dossier personnel", path: "/home/demo" }] : files.crumbs()
                    Row {
                        required property var modelData
                        required property int index
                        height: crumbRow.height
                        Text { visible: index > 1; text: "›"; color: "#7F93B5"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter; rightPadding: 2 }
                        Rectangle {
                            height: 22; anchors.verticalCenter: parent.verticalCenter
                            width: crumbText.implicitWidth + 12; radius: 5
                            color: crumbMouse.containsMouse ? "#26FFFFFF" : "transparent"
                            Text {
                                id: crumbText
                                anchors.centerIn: parent
                                text: modelData.label
                                color: Theme.ink
                                font.pixelSize: 12
                                font.weight: modelData.path === files.location ? Font.DemiBold : Font.Normal
                                font.family: Theme.fontFamily
                            }
                            MouseArea {
                                id: crumbMouse
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: if (files.connected) files.controller.navigate(modelData.path)
                                onDoubleClicked: files.editingPath = true
                            }
                        }
                    }
                }
            }
        }
        MouseArea {
            visible: !files.editingPath && files.connected
            anchors.left: parent.left; anchors.leftMargin: Math.min(crumbRow.width + 8, parent.width)
            anchors.right: parent.right; height: parent.height
            cursorShape: Qt.IBeamCursor
            onClicked: files.editingPath = true
        }
        TextInput {
            id: pathInput
            visible: files.editingPath
            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.ink; selectionColor: Theme.accent
            font.pixelSize: 12; font.family: Theme.fontFamily
            clip: true
            onVisibleChanged: if (visible) { text = files.location; forceActiveFocus(); selectAll() }
            onAccepted: { files.editingPath = false; files.controller.navigate(text); files.forceActiveFocus() }
            onActiveFocusChanged: if (!activeFocus) files.editingPath = false
            Keys.onEscapePressed: { files.editingPath = false; files.forceActiveFocus() }
        }
    }

    Row {
        id: tools
        visible: !files.adaptive
        anchors.right: parent.right; anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Rectangle {
            width: Math.max(100, Math.min(200, toolbar.width * 0.2)); height: 30; radius: 15
            color: "#1AFFFFFF"
            border.width: 1; border.color: search.activeFocus ? Theme.accent : "#1FFFFFFF"
            SymbolGlyph { x: 10; anchors.verticalCenter: parent.verticalCenter; glyph: "⌕"; color: Theme.inkMuted; size: 15 }
            TextInput {
                id: search
                x: 30; width: parent.width - 52
                anchors.verticalCenter: parent.verticalCenter
                text: files.filterText
                onTextEdited: files.filterText = text
                color: Theme.ink; selectionColor: Theme.accent
                font.pixelSize: 12; font.family: Theme.fontFamily
                clip: true
                Keys.onEscapePressed: { files.filterText = ""; files.forceActiveFocus() }
                Keys.onDownPressed: { files.view.forceActiveFocus(); if (files.view.currentIndex < 0) files.view.currentIndex = 0 }
                // Enter: server search by name; Ctrl+Enter: by content.
                Keys.onReturnPressed: (e) => files.serverSearch(e.modifiers & Qt.ControlModifier ? "content" : "name")
                Keys.onEnterPressed: (e) => files.serverSearch(e.modifiers & Qt.ControlModifier ? "content" : "name")
                Text {
                    visible: parent.text.length === 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Filtrer / chercher"
                    color: "#7F93B5"; font.pixelSize: 12; font.family: Theme.fontFamily
                }
            }
            Text {
                visible: files.filterText.length > 0
                anchors.right: parent.right; anchors.rightMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                text: "×"; color: Theme.inkMuted; font.pixelSize: 15
                MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: files.filterText = "" }
            }
        }
        Item { width: 6; height: 1 }
        ToolButtonGlass { glyph: "grid"; tip: "Icônes (Ctrl+1)"; checked: files.viewMode === "grid"; onClicked: files.viewMode = "grid" }
        ToolButtonGlass { glyph: "list"; tip: "Liste détaillée (Ctrl+2)"; checked: files.viewMode === "list"; onClicked: files.viewMode = "list" }
        ToolButtonGlass { id: sortButton; glyph: "sort"; tip: "Trier"; onClicked: sortMenu.popup(sortButton, 0, sortButton.height + 4) }
        ToolButtonGlass { glyph: "eye"; tip: "Fichiers cachés (Ctrl+H)"; checked: files.showHidden; onClicked: files.showHidden = !files.showHidden }
        ToolButtonGlass { glyph: "↻"; tip: "Actualiser (F5)"; enabledState: files.connected; onClicked: files.controller.refresh() }
        Item { width: 4; height: 1 }
        ToolButtonGlass { glyph: "⤓"; tip: "Télécharger sur cet appareil (Ctrl+D)"; enabledState: files.connected && files.selected !== null && !files.selected.directory; onClicked: files.askDownload(files.selected) }
        ToolButtonGlass { glyph: "⤒"; tip: "Envoyer des fichiers ici (Ctrl+U)"; enabledState: files.connected; onClicked: files.pickUpload() }
        ToolButtonGlass { glyph: "+"; tip: "Nouveau dossier (Ctrl+Maj+N)"; enabledState: files.connected; onClicked: files.askNewFolder() }
    }

    Menu {
        id: sortMenu
        palette.window: "#152238"; palette.windowText: "#F5F9FF"; palette.highlight: "#2D6FE0"
        palette.text: "#F5F9FF"; palette.base: "#152238"; palette.light: "#152238"; palette.mid: "#2A3A58"
        Repeater {
            model: [ { k: "name", t: "Nom" }, { k: "mtime", t: "Date de modification" }, { k: "size", t: "Taille" } ]
            MenuItem {
                required property var modelData
                text: (files.sortKey === modelData.k ? "✓  " : "     ") + modelData.t
                onTriggered: files.sortKey = modelData.k
            }
        }
        MenuSeparator {}
        MenuItem { text: (files.sortAscending ? "✓  " : "     ") + "Croissant"; onTriggered: files.sortAscending = true }
        MenuItem { text: (!files.sortAscending ? "✓  " : "     ") + "Décroissant"; onTriggered: files.sortAscending = false }
    }

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1AFFFFFF" }
}
