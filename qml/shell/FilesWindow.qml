import QtQuick
import QtQuick.Controls
import QtQuick.Effects

// Smart Files window, dark frosted glass (owner decision 21.10.2026: every
// window is glass). Places sidebar, history, clickable breadcrumb / typed
// path, live filter, grid and detailed list, sorting, hidden files, status
// bar and keyboard control. Data comes from `controller` over one persistent
// SSH/SFTP session; `fixture` substitutes the labelled reference sample.
Item {
    id: win
    property Item backdrop
    property var controller: null
    property bool fixture: false
    signal closeRequested()

    property string viewMode: "grid"
    property string sortKey: "name"
    property bool sortAscending: true
    property bool showHidden: false
    property string filterText: ""
    property bool editingPath: false
    property bool maximized: false
    property rect normalGeometry

    readonly property bool connected: controller !== null && controller.connected
    readonly property bool busy: controller !== null && controller.busy
    readonly property var sampleEntries: [
        { name: "Bureau", directory: true, path: "/home/demo/Bureau", mtime: 1760788800, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Documents", directory: true, path: "/home/demo/Documents", mtime: 1760781600, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Images", directory: true, path: "/home/demo/Images", mtime: 1760695200, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Vidéos", directory: true, path: "/home/demo/Vidéos", mtime: 1760608800, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Musique", directory: true, path: "/home/demo/Musique", mtime: 1760522400, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Téléchargements", directory: true, path: "/home/demo/Téléchargements", mtime: 1760788000, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Projets", directory: true, path: "/home/demo/Projets", mtime: 1760700000, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Travail", directory: true, path: "/home/demo/Travail", mtime: 1760600000, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Montagne.jpg", path: "/home/demo/Montagne.jpg", mtime: 1760780000, size: 2483200, mode: "-rw-r--r--", owner: "demo" },
        { name: "Présentation.pdf", path: "/home/demo/Présentation.pdf", mtime: 1760770000, size: 1205000, mode: "-rw-r--r--", owner: "demo" },
        { name: "Budget.xlsx", path: "/home/demo/Budget.xlsx", mtime: 1760760000, size: 48200, mode: "-rw-r--r--", owner: "demo" },
        { name: "Notes.txt", path: "/home/demo/Notes.txt", mtime: 1760750000, size: 1830, mode: "-rw-r--r--", owner: "demo" }
    ]
    readonly property var entries: fixture ? sampleEntries : (connected ? controller.entries : [])
    readonly property string location: fixture ? "/home/demo" : (connected ? controller.currentDirectory : "")
    readonly property string home: fixture ? "/home/demo" : (connected ? controller.homeDirectory : "")

    // Filter + sort is done here, in RAM, so it reacts per keystroke.
    readonly property var shown: {
        const needle = filterText.trim().toLowerCase()
        const rows = entries.filter(e => (showHidden || !String(e.name).startsWith("."))
                                         && (needle.length === 0 || String(e.name).toLowerCase().indexOf(needle) >= 0))
        const dir = sortAscending ? 1 : -1
        const key = sortKey
        rows.sort((a, b) => {
            if (a.directory !== b.directory) return a.directory ? -1 : 1
            let r = 0
            if (key === "size") r = (a.size || 0) - (b.size || 0)
            else if (key === "mtime") r = (a.mtime || 0) - (b.mtime || 0)
            if (r === 0) r = String(a.name).localeCompare(String(b.name), undefined, { numeric: true, sensitivity: "base" })
            return r * dir
        })
        return rows
    }
    readonly property int folderCount: shown.filter(e => e.directory).length
    readonly property var selected: view.currentIndex >= 0 && view.currentIndex < shown.length ? shown[view.currentIndex] : null
    readonly property Item view: viewMode === "grid" ? grid : list

    function formatSize(bytes) {
        if (bytes === undefined || bytes === null) return ""
        const units = ["o", "Ko", "Mo", "Go", "To"]
        let v = bytes, u = 0
        while (v >= 1024 && u < units.length - 1) { v /= 1024; ++u }
        return (u === 0 ? v : v.toFixed(v < 10 ? 1 : 0)) + " " + units[u]
    }
    function formatDate(secs) {
        if (!secs) return ""
        const d = new Date(secs * 1000)
        const today = new Date()
        return d.toDateString() === today.toDateString() ? "Aujourd'hui " + Qt.formatTime(d, "HH:mm")
                                                         : Qt.formatDateTime(d, "dd/MM/yyyy HH:mm")
    }
    function open(entry) {
        if (!entry || fixture || !connected) return
        // A symbolic link may point to a folder; the server answers on open.
        if (entry.directory || entry.link) { filterText = ""; controller.navigate(entry.path) }
    }
    function crumbs() {
        if (location.length === 0) return []
        const out = [{ label: "/", path: "/" }]
        let acc = ""
        for (const part of location.split("/").filter(p => p.length > 0)) {
            acc += "/" + part
            out.push({ label: acc === home ? "⌂ " + part : part, path: acc })
        }
        return out
    }
    function toggleMaximize() {
        if (!maximized) {
            normalGeometry = Qt.rect(x, y, width, height)
            x = 96; y = 54; width = parent.width - 96 - 324; height = parent.height - 54 - 86
        } else {
            x = normalGeometry.x; y = normalGeometry.y; width = normalGeometry.width; height = normalGeometry.height
        }
        maximized = !maximized
    }

    onLocationChanged: { grid.currentIndex = -1; list.currentIndex = -1 }
    // The frosted backdrop is sampled at the window's position.
    onXChanged: body.syncOrigin()
    onYChanged: body.syncOrigin()

    // ── keyboard ───────────────────────────────────────────────────────
    focus: true
    Keys.onPressed: (event) => {
        const ctrl = event.modifiers & Qt.ControlModifier
        const alt = event.modifiers & Qt.AltModifier
        if (!win.connected) return
        if (ctrl && event.key === Qt.Key_F) { search.forceActiveFocus(); event.accepted = true }
        else if (ctrl && event.key === Qt.Key_L) { win.editingPath = true; event.accepted = true }
        else if (ctrl && event.key === Qt.Key_H) { win.showHidden = !win.showHidden; event.accepted = true }
        else if (ctrl && event.key === Qt.Key_1) { win.viewMode = "grid"; event.accepted = true }
        else if (ctrl && event.key === Qt.Key_2) { win.viewMode = "list"; event.accepted = true }
        else if (event.key === Qt.Key_F5 || (ctrl && event.key === Qt.Key_R)) { controller.refresh(); event.accepted = true }
        else if (alt && event.key === Qt.Key_Left) { controller.goBack(); event.accepted = true }
        else if (alt && event.key === Qt.Key_Right) { controller.goForward(); event.accepted = true }
        else if ((alt && event.key === Qt.Key_Up) || event.key === Qt.Key_Backspace) { controller.goUp(); event.accepted = true }
        else if (alt && event.key === Qt.Key_Home) { controller.goHome(); event.accepted = true }
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { win.open(win.selected); event.accepted = true }
        else if (event.key === Qt.Key_Escape) { win.filterText = ""; win.editingPath = false; event.accepted = true }
        else if (event.text.length === 1 && event.text.trim().length === 1 && !ctrl && !alt) {
            // Type-to-filter, like a desktop file manager.
            search.forceActiveFocus(); win.filterText += event.text; event.accepted = true
        }
    }

    RectangularShadow {
        anchors.fill: parent
        offset.y: 14; radius: 16; blur: 44
        color: "#73000814"
    }

    Glass {
        id: body
        anchors.fill: parent
        backdrop: win.backdrop
        radius: 16
        shadow: false
        tint: "#B3121C30"
        blurAmount: 0.9
    }

    // ── sidebar ────────────────────────────────────────────────────────
    Rectangle {
        id: sidebar
        width: 172; height: parent.height
        topLeftRadius: 16; bottomLeftRadius: 16
        color: "#40050B18"
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#1FFFFFFF" }

        Row {
            x: 14; y: 17; spacing: 9
            Repeater {
                model: [ { c: "#FF5F57", a: "close" }, { c: "#FEBC2E", a: "none" }, { c: "#28C840", a: "max" } ]
                Rectangle {
                    required property var modelData
                    width: 12; height: 12; radius: 6; color: modelData.c
                    MouseArea {
                        anchors.fill: parent
                        onClicked: modelData.a === "close" ? win.closeRequested() : (modelData.a === "max" ? win.toggleMaximize() : null)
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
                    { g: "⏏", t: "Médias", p: "/media" }
                ]
                Rectangle {
                    required property var modelData
                    readonly property string target: modelData.p === "~" ? win.home : modelData.p
                    readonly property bool active: target.length > 0 && win.location === target
                    width: parent.width; height: 29; radius: 7
                    color: active ? Theme.selection : (placeMouse.containsMouse ? "#1AFFFFFF" : "transparent")
                    Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: modelData.g; color: "white"; font.pixelSize: 13; width: 16; horizontalAlignment: Text.AlignHCenter }
                    Text { x: 33; anchors.verticalCenter: parent.verticalCenter; text: modelData.t; color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily }
                    MouseArea {
                        id: placeMouse
                        anchors.fill: parent; hoverEnabled: true
                        enabled: win.connected
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { win.filterText = ""; win.controller.navigate(parent.target) }
                    }
                }
            }
        }

        Column {
            visible: win.connected || win.fixture
            anchors.bottom: parent.bottom; anchors.bottomMargin: 12
            x: 16; width: parent.width - 32
            spacing: 4
            Text { text: "SERVEUR"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2; font.family: Theme.fontFamily }
            Row {
                spacing: 6
                Rectangle { width: 7; height: 7; radius: 3.5; anchors.verticalCenter: parent.verticalCenter; color: "#4ADE80" }
                Text {
                    width: 120; elide: Text.ElideMiddle
                    text: win.fixture ? "demo@linux" : (win.connected ? win.controller.serverLabel : "")
                    color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily
                }
            }
            Text {
                visible: !win.fixture
                text: "Se déconnecter"
                color: "#9CC6FF"; font.pixelSize: 11; font.underline: discoMouse.containsMouse; font.family: Theme.fontFamily
                MouseArea { id: discoMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.controller.disconnectServer() }
            }
        }
    }

    // ── toolbar (also the drag handle) ─────────────────────────────────
    Item {
        id: toolbar
        x: sidebar.width; width: parent.width - x; height: 48

        MouseArea {
            anchors.fill: parent
            drag.target: win.maximized ? null : win
            drag.minimumX: -win.width + 120; drag.maximumX: win.parent ? win.parent.width - 120 : 0
            drag.minimumY: 47; drag.maximumY: win.parent ? win.parent.height - 60 : 0
            onDoubleClicked: win.toggleMaximize()
        }

        Row {
            id: nav
            x: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 2
            ToolButtonGlass { glyph: "‹"; tip: "Précédent (Alt+←)"; enabledState: win.connected && win.controller.canGoBack; onClicked: win.controller.goBack() }
            ToolButtonGlass { glyph: "›"; tip: "Suivant (Alt+→)"; enabledState: win.connected && win.controller.canGoForward; onClicked: win.controller.goForward() }
            ToolButtonGlass { glyph: "↑"; tip: "Dossier parent (Alt+↑)"; enabledState: win.connected && win.location !== "/"; onClicked: win.controller.goUp() }
        }

        Rectangle {
            id: pathBar
            anchors.left: nav.right; anchors.leftMargin: 6
            anchors.right: tools.left; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            height: 30; radius: 8
            color: "#1AFFFFFF"
            border.width: 1; border.color: win.editingPath ? Theme.accent : "#1FFFFFFF"
            clip: true

            Flickable {
                id: crumbFlick
                visible: !win.editingPath
                anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
                contentWidth: crumbRow.width; contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                onContentWidthChanged: contentX = Math.max(0, contentWidth - width)
                Row {
                    id: crumbRow
                    height: parent.height
                    Repeater {
                        model: win.fixture ? [{ label: "⌂ Dossier personnel", path: "/home/demo" }] : win.crumbs()
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
                                    font.weight: modelData.path === win.location ? Font.DemiBold : Font.Normal
                                    font.family: Theme.fontFamily
                                }
                                MouseArea {
                                    id: crumbMouse
                                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: if (win.connected) win.controller.navigate(modelData.path)
                                    onDoubleClicked: win.editingPath = true
                                }
                            }
                        }
                    }
                }
            }
            MouseArea {
                visible: !win.editingPath && win.connected
                anchors.left: parent.left; anchors.leftMargin: Math.min(crumbRow.width + 8, parent.width)
                anchors.right: parent.right; height: parent.height
                cursorShape: Qt.IBeamCursor
                onClicked: win.editingPath = true
            }
            TextInput {
                id: pathInput
                visible: win.editingPath
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.ink; selectionColor: Theme.accent
                font.pixelSize: 12; font.family: Theme.fontFamily
                clip: true
                onVisibleChanged: if (visible) { text = win.location; forceActiveFocus(); selectAll() }
                onAccepted: { win.editingPath = false; win.controller.navigate(text); win.forceActiveFocus() }
                onActiveFocusChanged: if (!activeFocus) win.editingPath = false
                Keys.onEscapePressed: { win.editingPath = false; win.forceActiveFocus() }
            }
        }

        Row {
            id: tools
            anchors.right: parent.right; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Rectangle {
                width: Math.max(100, Math.min(200, toolbar.width * 0.2)); height: 30; radius: 15
                color: "#1AFFFFFF"
                border.width: 1; border.color: search.activeFocus ? Theme.accent : "#1FFFFFFF"
                Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: "⌕"; color: Theme.inkMuted; font.pixelSize: 15 }
                TextInput {
                    id: search
                    x: 30; width: parent.width - 52
                    anchors.verticalCenter: parent.verticalCenter
                    text: win.filterText
                    onTextEdited: win.filterText = text
                    color: Theme.ink; selectionColor: Theme.accent
                    font.pixelSize: 12; font.family: Theme.fontFamily
                    clip: true
                    Keys.onEscapePressed: { win.filterText = ""; win.forceActiveFocus() }
                    Keys.onDownPressed: { win.view.forceActiveFocus(); if (win.view.currentIndex < 0) win.view.currentIndex = 0 }
                    Keys.onReturnPressed: { win.view.forceActiveFocus(); if (win.view.currentIndex < 0) win.view.currentIndex = 0 }
                    Text {
                        visible: parent.text.length === 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Filtrer ce dossier"
                        color: "#7F93B5"; font.pixelSize: 12; font.family: Theme.fontFamily
                    }
                }
                Text {
                    visible: win.filterText.length > 0
                    anchors.right: parent.right; anchors.rightMargin: 9
                    anchors.verticalCenter: parent.verticalCenter
                    text: "×"; color: Theme.inkMuted; font.pixelSize: 15
                    MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: win.filterText = "" }
                }
            }
            Item { width: 6; height: 1 }
            ToolButtonGlass { glyph: "grid"; tip: "Icônes (Ctrl+1)"; checked: win.viewMode === "grid"; onClicked: win.viewMode = "grid" }
            ToolButtonGlass { glyph: "list"; tip: "Liste détaillée (Ctrl+2)"; checked: win.viewMode === "list"; onClicked: win.viewMode = "list" }
            ToolButtonGlass { id: sortButton; glyph: "sort"; tip: "Trier"; onClicked: sortMenu.popup(sortButton, 0, sortButton.height + 4) }
            ToolButtonGlass { glyph: "eye"; tip: "Fichiers cachés (Ctrl+H)"; checked: win.showHidden; onClicked: win.showHidden = !win.showHidden }
            ToolButtonGlass { glyph: "↻"; tip: "Actualiser (F5)"; enabledState: win.connected; onClicked: win.controller.refresh() }
        }

        Menu {
            id: sortMenu
            palette.window: "#152238"; palette.windowText: "#F5F9FF"; palette.highlight: "#2D6FE0"
            palette.text: "#F5F9FF"; palette.base: "#152238"; palette.light: "#152238"; palette.mid: "#2A3A58"
            Repeater {
                model: [ { k: "name", t: "Nom" }, { k: "mtime", t: "Date de modification" }, { k: "size", t: "Taille" } ]
                MenuItem {
                    required property var modelData
                    text: (win.sortKey === modelData.k ? "✓  " : "     ") + modelData.t
                    onTriggered: win.sortKey = modelData.k
                }
            }
            MenuSeparator {}
            MenuItem { text: (win.sortAscending ? "✓  " : "     ") + "Croissant"; onTriggered: win.sortAscending = true }
            MenuItem { text: (!win.sortAscending ? "✓  " : "     ") + "Décroissant"; onTriggered: win.sortAscending = false }
        }

        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1AFFFFFF" }
    }

    // ── content ────────────────────────────────────────────────────────
    Item {
        id: content
        x: sidebar.width; y: toolbar.height
        width: parent.width - x; height: parent.height - y - statusBar.height
        clip: true
        visible: win.fixture || win.connected

        // Thin progress line while a listing is in flight.
        Rectangle {
            visible: win.busy
            z: 5; width: parent.width; height: 2
            color: "transparent"
            Rectangle {
                id: runner
                width: parent.width * 0.25; height: 2; radius: 1; color: Theme.accent
                NumberAnimation on x { from: -runner.width; to: content.width; duration: 900; loops: Animation.Infinite; running: win.busy }
            }
        }

        GridView {
            id: grid
            visible: win.viewMode === "grid"
            anchors.fill: parent; anchors.margins: 10
            cellWidth: Math.max(104, width / Math.max(1, Math.floor(width / 116)))
            cellHeight: 108
            model: win.shown
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
                    onClicked: { grid.currentIndex = cell.index; grid.forceActiveFocus() }
                    onDoubleClicked: win.open(cell.modelData)
                }
            }
            Keys.onReturnPressed: win.open(win.selected)
        }

        ListView {
            id: list
            visible: win.viewMode === "list"
            anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
            model: win.shown
            currentIndex: -1
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            cacheBuffer: 600
            highlightMoveDuration: 0
            highlight: Rectangle { radius: 6; color: "#402F8CFF" }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            readonly property real colDate: 132
            readonly property real colSize: 76
            readonly property real colMode: 88
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
                                text: modelData.t + (win.sortKey === modelData.k ? (win.sortAscending ? "  ▲" : "  ▼") : "")
                                color: "#9FB2D0"; font.pixelSize: 11; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: modelData.k.length > 0
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (win.sortKey === modelData.k) win.sortAscending = !win.sortAscending
                                    else { win.sortKey = modelData.k; win.sortAscending = modelData.k === "name" }
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
                width: list.width; height: 30
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
                    Text { width: list.colDate; anchors.verticalCenter: parent.verticalCenter; text: win.formatDate(row.modelData.mtime); color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily }
                    Text { width: list.colSize; anchors.verticalCenter: parent.verticalCenter; text: row.modelData.directory ? "—" : win.formatSize(row.modelData.size); color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily }
                    Text { width: list.colMode; anchors.verticalCenter: parent.verticalCenter; text: row.modelData.mode || ""; color: "#8FA2C2"; font.pixelSize: 11; font.family: "Consolas" }
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent; hoverEnabled: true
                    onClicked: { list.currentIndex = row.index; list.forceActiveFocus() }
                    onDoubleClicked: win.open(row.modelData)
                }
            }
            Keys.onReturnPressed: win.open(win.selected)
        }

        Column {
            visible: !win.busy && win.shown.length === 0
            anchors.centerIn: parent
            spacing: 6
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: win.entries.length === 0 ? "Dossier vide" : "Aucun élément ne correspond à « " + win.filterText + " »"
                color: Theme.ink; font.pixelSize: 14; font.family: Theme.fontFamily
            }
            Text {
                visible: win.entries.length > 0 && !win.showHidden && win.filterText.length === 0
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Les fichiers cachés sont masqués (Ctrl+H)"
                color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
            }
        }
    }

    ConnectPane {
        visible: !win.fixture && !win.connected
        x: sidebar.width + 24; y: toolbar.height + 16
        width: parent.width - x - 24; height: parent.height - y - 16
        controller: win.controller
    }

    // ── status bar ─────────────────────────────────────────────────────
    Item {
        id: statusBar
        x: sidebar.width; y: parent.height - height
        width: parent.width - x; height: 26
        visible: win.fixture || win.connected
        Rectangle { width: parent.width; height: 1; color: "#14FFFFFF" }
        Text {
            x: 14; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 120; elide: Text.ElideRight
            color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
            text: {
                const base = win.shown.length + " élément" + (win.shown.length > 1 ? "s" : "")
                    + " · " + win.folderCount + " dossier" + (win.folderCount > 1 ? "s" : "")
                if (!win.selected) return base
                const s = win.selected
                return base + "   ·   " + s.name + (s.directory ? "" : " — " + win.formatSize(s.size))
                    + (s.owner ? " — " + s.owner : "") + (s.mode ? " — " + s.mode : "")
            }
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 22
            anchors.verticalCenter: parent.verticalCenter
            color: "#7F93B5"
            font.pixelSize: 10; font.family: Theme.fontFamily
            text: win.fixture || !win.connected ? "SFTP" : (win.busy ? "chargement…" : "SFTP · " + win.controller.lastLatencyMs + " ms")
        }
    }

    // ── resize grip ────────────────────────────────────────────────────
    MouseArea {
        width: 16; height: 16
        anchors.right: parent.right; anchors.bottom: parent.bottom
        cursorShape: Qt.SizeFDiagCursor
        enabled: !win.maximized
        property point press
        onPressed: (m) => press = Qt.point(m.x, m.y)
        onPositionChanged: (m) => {
            win.width = Math.max(560, win.width + m.x - press.x)
            win.height = Math.max(340, win.height + m.y - press.y)
        }
    }
}
