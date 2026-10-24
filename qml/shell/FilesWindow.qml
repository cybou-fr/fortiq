import QtQuick
import QtQuick.Controls
import QtCore
import QtQuick.Dialogs
import QtQuick.Effects

// Smart Files window, dark frosted glass (owner decision 21.10.2026: every
// window is glass). Places sidebar, history, clickable breadcrumb / typed
// path, live filter, grid and detailed list, sorting, hidden files, status
// bar and keyboard control. Data comes from `controller` over one persistent
// SSH/SFTP session; `fixture` substitutes the labelled reference sample.
Item {
    id: win
    objectName: "filesWindow"
    property bool adaptive: false
    property bool navigationOpen: false
    readonly property bool drawer: adaptive && width < 900
    readonly property real contentLeft: drawer ? 0 : 172
    function pickUpload() { uploadDialog.open() }
    function handleBack() {
        if (navigationOpen) { navigationOpen = false; return true }
        if (fileViewer.visible) { fileViewer.requestClose(); return true }
        return false
    }
    property Item backdrop
    property var controller: null
    property bool fixture: false
    // Fixture only: "text" or "image" shows the viewer with sample content.
    property string fixturePreview: ""
    signal requestDocuments(string path)
    signal closeRequested()
    signal requestServers()
    signal requestDisconnect()

    property string viewMode: "grid"
    property string sortKey: "name"
    property bool sortAscending: true
    property bool showHidden: false
    property string filterText: ""
    property bool editingPath: false
    property bool maximized: false
    property rect normalGeometry

    readonly property bool connected: controller !== null && controller.connected
    readonly property bool inTrash: fixture ? fixturePreview === "trashview"
        : (connected && controller.trashDirectory.length > 0 && location === controller.trashDirectory)
    readonly property bool busy: controller !== null && controller.busy
    readonly property var entries: fixture ? (fixturePreview === "trashview" ? fixtureData.sampleTrash : fixtureData.sampleEntries) : (connected ? controller.entries : [])
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
        else if(/\.(md|markdown)$/i.test(entry.path))requestDocuments(entry.path)
        else controller.openFile(entry.path)
    }
    // ── file operations ────────────────────────────────────────────────
    property string pendingOp: ""
    property var pendingEntry: null
    property string notice: ""
    property bool noticeError: false
    function showNotice(text, isError) { notice = text; noticeError = isError; noticeTimer.restart() }
    function askNewFolder() {
        if (!connected) return
        pendingOp = "newFolder"; pendingEntry = null
        opDialog.openWith("Nouveau dossier", "Dans " + location, "Créer", false, true, "Nouveau dossier")
    }
    function askRename(entry) {
        if (!entry || !connected) return
        pendingOp = "rename"; pendingEntry = entry
        opDialog.openWith("Renommer", entry.directory ? "Dossier" : "Fichier", "Renommer", false, true, entry.name)
    }
    function restore(entry) {
        if (!entry || !connected) return
        pendingOp = "restore"; pendingEntry = entry
        controller.restoreEntry(entry.path, false)
    }
    function askDeletePermanently(entry) {
        if (!entry || !connected) return
        pendingOp = "delete"; pendingEntry = entry
        opDialog.openWith("Supprimer définitivement ?",
            "« " + entry.name + " »" + (entry.directory ? " et tout son contenu" : "")
            + " sera supprimé de " + controller.serverLabel + ". Cette action est irréversible.",
            "Supprimer définitivement", true, false, "")
    }
    function askEmptyTrash() {
        if (!connected) return
        pendingOp = "empty"; pendingEntry = null
        const n = entries.length
        opDialog.openWith("Vider la corbeille ?",
            n + " élément" + (n > 1 ? "s" : "") + " de la corbeille de " + controller.serverLabel
            + " seront supprimés définitivement, y compris les fichiers cachés. Cette action est irréversible.",
            "Vider la corbeille", true, false, "")
    }
    function askTrash(entry) {
        if (!entry || !connected) return
        if (inTrash) { askDeletePermanently(entry); return }
        if (controller.trashDirectory.length > 0 && String(entry.path).startsWith(controller.trashDirectory + "/")) {
            showNotice("Déjà dans la corbeille. La suppression définitive n'est pas encore disponible.", true)
            return
        }
        pendingOp = "trash"; pendingEntry = entry
        opDialog.openWith("Mettre à la corbeille ?",
            "« " + entry.name + " »" + (entry.directory ? " et tout son contenu" : "")
            + " sera déplacé dans la corbeille de " + controller.serverLabel
            + ". Vous pourrez le restaurer depuis la Corbeille.",
            "Mettre à la corbeille", true, false, "")
    }

    function serverSearch(mode) {
        if (!connected || filterText.trim().length === 0) return
        controller.startSearch(filterText, mode)
    }

    function askDownload(entry) {
        if (!entry || entry.directory || !connected) return
        downloadDialog.entry = entry
        downloadDialog.defaultSuffix = String(entry.name).indexOf(".") >= 0 ? String(entry.name).split(".").pop() : ""
        // Android's native SaveFile picker uses only the URL basename as its
        // suggested display name; the accepted result is the provider URI.
        downloadDialog.selectedFile = Qt.platform.os === "android" ? "file:///" + encodeURIComponent(entry.name)
            : downloadDialog.currentFolder + "/" + encodeURIComponent(entry.name)
        downloadDialog.open()
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
        if (adaptive) return
        if (!maximized) {
            normalGeometry = Qt.rect(x, y, width, height)
            x = 96; y = 54; width = parent.width - 96 - 324; height = parent.height - 54 - 86
        } else {
            x = normalGeometry.x; y = normalGeometry.y; width = normalGeometry.width; height = normalGeometry.height
        }
        maximized = !maximized
    }

    FilesFixtureData { id: fixtureData; fixturePreview: win.fixturePreview }

    onLocationChanged: { grid.currentIndex = -1; list.currentIndex = -1 }
    Component.onCompleted: if (fixture && fixturePreview === "trash")
        opDialog.openWith("Mettre à la corbeille ?", "« Budget.xlsx » sera déplacé dans la corbeille de demo@linux. Vous pourrez le restaurer depuis la Corbeille.",
                          "Mettre à la corbeille", true, false, "")
    // The frosted backdrop is sampled at the window's position.
    onXChanged: body.syncOrigin()
    onYChanged: body.syncOrigin()

    // ── keyboard ───────────────────────────────────────────────────────
    focus: true
    Keys.onPressed: (event) => {
        const ctrl = event.modifiers & Qt.ControlModifier
        const alt = event.modifiers & Qt.AltModifier
        if (!win.connected) return
        if (ctrl && event.key === Qt.Key_F) { toolbar.searchField.forceActiveFocus(); event.accepted = true }
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
        else if (event.key === Qt.Key_Space && win.selected && !win.selected.directory) { controller.openFile(win.selected.path); event.accepted = true }
        else if (ctrl && event.key === Qt.Key_U) { uploadDialog.open(); event.accepted = true }
        else if (ctrl && event.key === Qt.Key_D) { win.askDownload(win.selected); event.accepted = true }
        else if (ctrl && (event.modifiers & Qt.ShiftModifier) && event.key === Qt.Key_N) { win.askNewFolder(); event.accepted = true }
        else if (event.key === Qt.Key_F2) { win.askRename(win.selected); event.accepted = true }
        else if (event.key === Qt.Key_Delete) { win.askTrash(win.selected); event.accepted = true }
        else if (event.key === Qt.Key_Escape) { win.filterText = ""; win.editingPath = false; event.accepted = true }
        else if (event.text.length === 1 && event.text.trim().length === 1 && !ctrl && !alt) {
            // Type-to-filter, like a desktop file manager.
            toolbar.searchField.forceActiveFocus(); win.filterText += event.text; event.accepted = true
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
        tint: "#8C121C30"
        blurAmount: 0.9
    }

    FilesSidebar { files: win }


    // ── toolbar (also the drag handle) ─────────────────────────────────
    FilesToolbar { id: toolbar; files: win; adaptiveHeight: adaptiveToolbar.implicitHeight }

    Rectangle {
        anchors.fill: parent; z: 35; visible: win.drawer && win.navigationOpen
        color: "#80000000"
        MouseArea { anchors.fill: parent; onClicked: win.navigationOpen = false }
    }
    AdaptiveFilesToolbar { id: adaptiveToolbar; x: win.contentLeft; width: parent.width - x; height: implicitHeight; visible: win.adaptive; files: win }

    // ── content ────────────────────────────────────────────────────────
    Item {
        id: content
        x: win.contentLeft; y: toolbar.height
        width: parent.width - x; height: Math.max(0, parent.height - y - statusBar.height)
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

        FilesTrashBanner { files: win }

        FilesGrid { id: grid; files: win; onMenuRequested: itemMenu.popup() }

        FilesList { id: list; files: win; onMenuRequested: itemMenu.popup() }

        Column {
            visible: !win.busy && win.shown.length === 0
            anchors.centerIn: parent
            spacing: 6
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: win.entries.length === 0 ? (win.inTrash ? "La corbeille est vide" : "Dossier vide")
                                               : "Aucun élément ne correspond à « " + win.filterText + " »"
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

    // ── transfers ──────────────────────────────────────────────────────
    FileDialog {
        id: downloadDialog
        property var entry: null
        title: "Télécharger « " + (entry ? entry.name : "") + " »"
        fileMode: FileDialog.SaveFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        onAccepted: if (entry) win.controller.download(entry.path, selectedFile)
    }
    FileDialog {
        id: uploadDialog
        title: "Envoyer vers " + win.location
        fileMode: FileDialog.OpenFiles
        onAccepted: win.controller.upload(selectedFiles, win.location)
    }

    // Drop local files anywhere on the content to upload them here.
    DropArea {
        id: drop
        x: content.x; y: content.y; width: content.width; height: content.height
        z: 8
        enabled: win.connected && !win.fixture
        keys: ["text/uri-list"]
        onDropped: (d) => { if (d.hasUrls) { win.controller.upload(d.urls, win.location); d.acceptProposedAction() } }
        Rectangle {
            anchors.fill: parent; anchors.margins: 8
            visible: drop.containsDrag
            radius: 12
            color: "#262F8CFF"
            border.width: 2; border.color: Theme.accent
            Text {
                anchors.centerIn: parent
                text: "Déposer pour envoyer dans\n" + win.location
                horizontalAlignment: Text.AlignHCenter
                color: Theme.ink; font.pixelSize: 15; font.family: Theme.fontFamily
            }
        }
    }


    Menu {
        id: itemMenu
        palette.window: "#152238"; palette.windowText: "#F5F9FF"; palette.highlight: "#2D6FE0"
        palette.text: "#F5F9FF"; palette.base: "#152238"; palette.light: "#152238"; palette.mid: "#2A3A58"
        palette.highlightedText: "#FFFFFF"; palette.buttonText: "#F5F9FF"
        MenuItem { text: "Ouvrir"; onTriggered: win.open(win.selected) }
        MenuItem { text: "Télécharger…"; enabled: win.selected !== null && !win.selected.directory; onTriggered: win.askDownload(win.selected) }
        MenuSeparator {}
        MenuItem { text: "Restaurer"; visible: win.inTrash; height: visible ? implicitHeight : 0; onTriggered: win.restore(win.selected) }
        MenuItem { text: "Supprimer définitivement…    Suppr"; visible: win.inTrash; height: visible ? implicitHeight : 0; onTriggered: win.askDeletePermanently(win.selected) }
        MenuItem { text: "Renommer…    F2"; visible: !win.inTrash; height: visible ? implicitHeight : 0; onTriggered: win.askRename(win.selected) }
        MenuItem { text: "Mettre à la corbeille…    Suppr"; visible: !win.inTrash; height: visible ? implicitHeight : 0; onTriggered: win.askTrash(win.selected) }
        MenuSeparator {}
        MenuItem { text: "Nouveau dossier…"; onTriggered: win.askNewFolder() }
    }

    GlassDialog {
        touchMode: win.adaptive
        id: opDialog
        x: content.x; y: 0
        width: content.width; height: parent.height
        topRightRadius: 16; bottomRightRadius: 16
        busy: win.connected && win.controller.operationBusy
        onAccepted: (value) => {
            if (win.pendingOp === "newFolder") win.controller.createFolder(value)
            else if (win.pendingOp === "rename") win.controller.renameEntry(win.pendingEntry.path, value)
            else if (win.pendingOp === "trash") win.controller.trashEntry(win.pendingEntry.path)
            else if (win.pendingOp === "delete") win.controller.deletePermanently(win.pendingEntry.path)
            else if (win.pendingOp === "empty") win.controller.emptyTrash()
            else if (win.pendingOp === "restoreKeepBoth") win.controller.restoreEntry(win.pendingEntry.path, true)
        }
        onRejected: { win.pendingOp = ""; win.view.forceActiveFocus() }
    }

    Connections {
        target: win.connected ? win.controller : null
        ignoreUnknownSignals: true
        function onOperationFinished(ok, message, code, path) {
            if (!ok && code === "conflict" && win.pendingOp === "restore") {
                win.pendingOp = "restoreKeepBoth"
                opDialog.openWith("Restaurer « " + win.pendingEntry.name + " »", message
                    + " Le restaurer sous un autre nom (« … (2) ») ?", "Garder les deux", false, false, "")
                return
            }
            if (ok || code === "unknown" || ["trash", "delete", "empty", "restore", "restoreKeepBoth"].indexOf(win.pendingOp) >= 0) {
                opDialog.visible = false
                win.pendingOp = ""
                win.showNotice(code === "unknown" ? "État inconnu — " + message : message, !ok)
                win.view.forceActiveFocus()
            } else {
                opDialog.error = message // keep the dialog open to fix the name
            }
        }
    }

    Timer { id: noticeTimer; interval: 4500; onTriggered: win.notice = "" }

    // Hint under the search field: the filter is local, Enter searches the server.
    Rectangle {
        visible: win.connected && toolbar.searchField.activeFocus && win.filterText.trim().length > 0
        z: 11
        anchors.right: parent.right; anchors.rightMargin: 12
        y: toolbar.height - 4
        width: hintRow.implicitWidth + 20; height: 34; radius: 10
        color: "#F5101B2E"; border.width: 1; border.color: "#40FFFFFF"
        Row {
            id: hintRow
            x: 10; anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Text { anchors.verticalCenter: parent.verticalCenter; text: "Rechercher « " + win.filterText.trim() + " » dans ce dossier et ses sous-dossiers :"; color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily }
            Repeater {
                model: [ { m: "name", t: "Nom  ↵" }, { m: "content", t: "Contenu  Ctrl+↵" } ]
                Rectangle {
                    required property var modelData
                    height: 22; radius: 6; width: chip.implicitWidth + 14
                    color: chipMouse.containsMouse ? Theme.selection : "#33FFFFFF"
                    Text { id: chip; anchors.centerIn: parent; text: modelData.t; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                    MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.serverSearch(modelData.m) }
                }
            }
        }
    }


    SearchResults {
        x: content.x; y: content.y
        width: content.width; height: content.height + statusBar.height
        z: 9
        bottomRightRadius: 16
        visible: (win.fixture && win.fixturePreview === "search") || (win.connected && win.controller.searchActive)
        controller: win.fixture ? fixtureData.sampleSearch : (win.connected ? win.controller : null)
        onOpenEntry: (entry) => {
            if (entry.directory) { win.controller.clearSearch(); win.filterText = ""; win.controller.navigate(entry.path) }
            else win.controller.openFile(entry.path)
        }
        onOpenLocation: (folder) => { win.controller.clearSearch(); win.filterText = ""; win.controller.navigate(folder) }
        onClosed: { if (win.connected) win.controller.clearSearch(); win.view.forceActiveFocus() }
    }

    // Transfers, operations, saves and searches are listed by the shared TaskPanel of the shell.


    FileViewer {
        adaptive: win.adaptive
        x: content.x; y: content.y
        width: content.width; height: content.height + statusBar.height
        z: 10
        id: fileViewer
        objectName: "fileViewer"
        visible: (win.fixture && ["text", "image", "editor"].indexOf(win.fixturePreview) >= 0) || (win.connected && win.controller.previewOpen)
        Component.onCompleted: if (win.fixture && win.fixturePreview === "editor") { startEditing(); dirty = true }
        controller: win.fixture ? fixtureData.samplePreview : (win.connected ? win.controller : null)
        bottomRightRadius: 16
        onClosed: { win.controller.closePreview(); win.view.forceActiveFocus() }
    }

    // No connection: the server manager is global, not part of Files.
    Column {
        visible: !win.fixture && !win.connected
        x: win.contentLeft; y: toolbar.height
        width: parent.width - x
        topPadding: 70
        spacing: 12
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Aucun serveur connecté"; color: Theme.ink; font.pixelSize: 16; font.family: Theme.fontFamily }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Les fichiers affichés sont ceux du serveur Linux choisi."; color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            height: 34; radius: 9; width: chooseLabel.implicitWidth + 30
            color: chooseMouse.containsMouse ? Theme.selection : "#992D6FE0"
            Text { id: chooseLabel; anchors.centerIn: parent; text: "Choisir un serveur…"; color: "white"; font.pixelSize: 13; font.family: Theme.fontFamily }
            MouseArea { id: chooseMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.requestServers() }
        }
    }

    // ── status bar ─────────────────────────────────────────────────────
    FilesStatusBar { id: statusBar; files: win }

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
    LinkBanner { remote: win.controller; touch: win.adaptive; inset: 44; onRetry: if (win.controller && !win.controller.busy) win.controller.refresh() }
}
