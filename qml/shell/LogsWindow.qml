import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Effects

// Logs app, glass sibling of Files: sources sidebar (system / user journal,
// services, /var/log files), range and severity filters, live follow,
// counts, timeline and deterministic repeat groups. No AI. Log text is
// untrusted: always rendered as plain text, never as markup or links.
Item {
    id: win
    objectName: "logsWindow"
    property bool adaptive: false
    property bool navigationOpen: false
    readonly property bool drawer: adaptive && width < 900
    readonly property real contentLeft: drawer ? 0 : 210
    function handleBack() {
        if (navigationOpen) { navigationOpen = false; return true }
        if (logs && logs.inspectorOpen) { logs.closeInspector(); return true }
        return false
    }
    property Item backdrop
    property var logs: null          // LogsController (null in the preview build)
    property var remote: null        // RemoteController, for connection state
    property bool fixture: false
    property bool fixtureIncident: false // fixture only: show the inspector sample
    property bool fixtureService: false  // fixture only: show the service sample
    readonly property bool unitSelected: lc && (lc.source === "unit" || lc.source === "userunit")
    signal closeRequested()
    signal requestServers()
    signal requestService(string unit)
    signal requestCopy(string what, string text)
    // Opened from another app: the journal of one unit over the last day.
    property string requestedUnit: ""
    function showUnit(unit) { requestedUnit = unit; choose("unit", unit); if (!fixture && logs) logs.setRange("24h") }

    property string view: fixtureService ? "service" : "timeline" // "timeline" | "patterns" | "service"
    property string textFilter: ""
    property string patternKey: ""
    property string patternLabel: ""
    property int expanded: -1
    property string unitFilter: ""

    readonly property bool connected: fixture || (remote !== null && remote.connected)
    readonly property var lc: fixture ? sample : logs

    readonly property var shownEvents: {
        if (!lc) return []
        const needle = textFilter.trim().toLowerCase()
        return lc.events.filter(e => (patternKey.length === 0 || e.tkey === patternKey)
            && (needle.length === 0 || String(e.message).toLowerCase().indexOf(needle) >= 0
                || String(e.unit).toLowerCase().indexOf(needle) >= 0))
    }

    function priorityColor(p) {
        if (p >= 0 && p <= 2) return "#F87171"
        if (p === 3) return "#FB923C"
        if (p === 4) return "#FACC15"
        if (p === 5 || p === 6) return "#60A5FA"
        if (p === 7) return "#94A3B8"
        return "#64748B"
    }
    function sourceTitle() {
        if (!lc) return "Journaux"
        if (lc.source === "journal") return "Journal système"
        if (lc.source === "user") return "Mon journal utilisateur"
        if (lc.source === "unit") return "Service " + lc.target
        if (lc.source === "userunit") return "Service utilisateur " + lc.target
        return lc.target
    }
    function choose(source, target) {
        navigationOpen = false
        patternKey = ""; patternLabel = ""; expanded = -1
        if (!fixture && logs) logs.select(source, target)
        // A service opens on its status; other sources on the timeline.
        if (source === "unit" || source === "userunit") { view = "service"; if (!fixture && logs) logs.loadService() }
        else if (view === "service") view = "timeline"
    }

    // The shell can signal connection, completion and visibility in the same
    // event-loop turn. Coalesce initial loads so one open does not enqueue
    // multiple SSH reads or invalidate its own request generations.
    property bool initialLoadScheduled: false
    function scheduleInitialLoad() {
        if (initialLoadScheduled || !visible || !connected || fixture || !logs
            || logs.busy) return
        initialLoadScheduled = true
        Qt.callLater(function() {
            initialLoadScheduled = false
            if (!visible || !connected || fixture || !logs || logs.busy) return
            logs.loadSources()
            logs.reload()
        })
    }
    onConnectedChanged: scheduleInitialLoad()
    onLogsChanged: scheduleInitialLoad()
    Component.onCompleted: scheduleInitialLoad()
    onVisibleChanged: if (visible && logs && logs.events.length === 0) scheduleInitialLoad()
    onXChanged: body.syncOrigin()
    onYChanged: body.syncOrigin()

    QtObject {
        id: sample
        readonly property bool inspectorOpen: win.fixtureIncident
        readonly property bool inspectorBusy: false
        readonly property int inspectorWindow: 300
        readonly property int inspectorAnchor: 3
        readonly property string inspectorError: ""
        readonly property var inspectorEvents: [
            { timeText: "18/10 13:40:58", priority: 6, unit: "nginx.service", message: "Reloading nginx configuration" },
            { timeText: "18/10 13:41:03", priority: 3, unit: "php-fpm.service", message: "WARNING: [pool www] server reached pm.max_children setting (5)" },
            { timeText: "18/10 13:41:04", priority: 3, unit: "nginx.service", message: "upstream timed out (110: Connection timed out) while reading response header from upstream, client: 10.0.0.12" },
            { timeText: "18/10 13:42:17", priority: 3, unit: "nginx.service", message: "upstream timed out (110: Connection timed out) while reading response header from upstream, client: 10.0.0.31" },
            { timeText: "18/10 13:42:19", priority: 6, unit: "systemd", message: "php-fpm.service: Main process exited, code=killed, status=9/KILL" },
            { timeText: "18/10 13:42:20", priority: 6, unit: "systemd", message: "php-fpm.service: Scheduled restart job, restart counter is at 1." },
            { timeText: "18/10 13:42:21", priority: 6, unit: "systemd", message: "Started php-fpm.service - The PHP FastCGI Process Manager." }
        ]
        readonly property var synopsis: ({
            observed: [
                { text: "Fenêtre 13:40:58 – 13:42:21 (±5 min) : 7 événement(s), 3 erreur(s), 0 avertissement(s).", refs: [1, 2, 3] },
                { text: "Événement choisi (13:42:17, nginx.service) : upstream timed out (110: Connection timed out) while reading response header…", refs: [3] },
                { text: "Première erreur de la fenêtre à 13:41:03 (php-fpm.service) : WARNING: [pool www] server reached pm.max_children setting (5)", refs: [1] },
                { text: "php-fpm.service : 1 erreur(s), 0 avertissement(s), de 13:41:03 à 13:41:03.", refs: [1] },
                { text: "nginx.service : 2 erreur(s), 0 avertissement(s), de 13:41:04 à 13:42:17.", refs: [2, 3] },
                { text: "php-fpm.service : processus principal terminé, statut 9/KILL, à 13:42:19.", refs: [4] },
                { text: "php-fpm.service : redémarrage programmé n°1 à 13:42:20.", refs: [5] },
                { text: "Répété ×2 : nginx.service · upstream timed out (#: Connection timed out) while reading response header from upstream, client: ‹ip›", refs: [2, 3] }
            ],
            inferred: [
                { text: "Hypothèse : les erreurs de php-fpm.service (13:41:03) précèdent celles de nginx.service (13:41:04) de 1 s. Proximité dans le temps seulement, pas une cause démontrée.", refs: [1, 2] },
                { text: "Hypothèse : le redémarrage de php-fpm.service à 13:42:20 suit 1 erreur(s) de cette unité dans les 77 s précédentes — réaction probable du gestionnaire de services.", refs: [1, 5] }
            ],
            unknown: [
                { text: "La cause n'est pas déterminée : les événements sont reliés seulement par le temps, l'unité et le texte.", refs: [] },
                { text: "Les messages de niveau debug et les fichiers de /var/log ne sont pas inclus.", refs: [] }
            ],
            next: [
                { text: "Ouvrir le journal de php-fpm.service sur 24 h", refs: [], action: "unit", arg: "php-fpm.service" },
                { text: "Sur le serveur, en lecture seule : systemctl status php-fpm.service", refs: [] },
                { text: "Élargir la fenêtre à ±15 min", refs: [], action: "widen", arg: "900" }
            ]
        })
        readonly property bool serviceBusy: false
        readonly property string serviceError: ""
        readonly property var serviceInfo: ({ id: "php-fpm.service", description: "The PHP 8.3 FastCGI Process Manager", active: "active", sub: "running",
            sinceText: "18/10 13:42:21", enabled: "enabled", result: "success", restarts: 3, restartPolicy: "on-failure", pid: "4182",
            memory: "48,2 Mo", cpu: "312.4 s", tasks: "7", exitStatus: "0", fragment: "/usr/lib/systemd/system/php8.3-fpm.service", historyLimited: false })
        readonly property var serviceDays: [ { label: "12/10", starts: 1, failures: 0, restarts: 0 }, { label: "13/10", starts: 0, failures: 0, restarts: 0 },
            { label: "14/10", starts: 2, failures: 1, restarts: 1 }, { label: "15/10", starts: 0, failures: 0, restarts: 0 },
            { label: "16/10", starts: 1, failures: 0, restarts: 0 }, { label: "17/10", starts: 0, failures: 0, restarts: 0 },
            { label: "18/10", starts: 3, failures: 1, restarts: 2 } ]
        readonly property var serviceHistory: [
            { timeText: "18/10 13:42:21", kind: "started", label: "Démarré", detail: "" },
            { timeText: "18/10 13:42:20", kind: "restart", label: "Redémarrage programmé", detail: "1" },
            { timeText: "18/10 13:42:19", kind: "exited", label: "Processus terminé", detail: "9/KILL" },
            { timeText: "18/10 09:12:04", kind: "started", label: "Démarré", detail: "" },
            { timeText: "18/10 09:12:03", kind: "restart", label: "Redémarrage programmé", detail: "1" },
            { timeText: "18/10 09:12:02", kind: "failed", label: "Échec", detail: "exit-code" },
            { timeText: "14/10 22:30:11", kind: "failed", label: "Échec", detail: "timeout" }
        ]
        function setInspectorWindow(s) {}
        function inspectorReport() { return "" }
        readonly property string source: "unit"
        readonly property string target: win.fixtureService ? "php8.3-fpm.service" : "nginx.service"
        readonly property string range: "1h"
        readonly property int maxPriority: 6
        readonly property bool busy: false
        readonly property bool following: true
        readonly property string state: "limited"
        readonly property string stateText: "Accès limité : seuls les messages de votre utilisateur sont visibles. Les membres des groupes adm ou systemd-journal voient tout le journal."
        readonly property string coverage: "6 événements"
        readonly property string lastUpdate: "13:45:12"
        readonly property var units: [ { name: "backup.service", active: "failed", sub: "failed" }, { name: "cron.service", active: "active", sub: "running" },
                                       { name: "nginx.service", active: "active", sub: "running" }, { name: "ssh.service", active: "active", sub: "running" } ]
        readonly property var files: [ { path: "/var/log/dpkg.log", name: "dpkg.log", readable: true }, { path: "/var/log/fail2ban.log", name: "fail2ban.log", readable: false },
                                       { path: "/var/log/nginx/error.log", name: "nginx/error.log", readable: true } ]
        readonly property var counts: [ { label: "Critique", count: 0, level: 2 }, { label: "Erreurs", count: 3, level: 3 }, { label: "Avertissements", count: 1, level: 4 },
                                         { label: "Info", count: 2, level: 6 }, { label: "Debug / ?", count: 0, level: 7 } ]
        readonly property var events: [
            { timeText: "18/10 13:41:02", priority: 6, priorityName: "info", unit: "nginx.service", pid: "812", message: "Reloading nginx configuration", tkey: "a", boot: "0532ee1e", cursor: "s=…" },
            { timeText: "18/10 13:41:03", priority: 3, priorityName: "erreur", unit: "nginx.service", pid: "812", message: "upstream timed out (110: Connection timed out) while reading response header from upstream, client: 10.0.0.12", tkey: "b", boot: "0532ee1e", cursor: "s=…" },
            { timeText: "18/10 13:42:17", priority: 3, priorityName: "erreur", unit: "nginx.service", pid: "812", message: "upstream timed out (110: Connection timed out) while reading response header from upstream, client: 10.0.0.31", tkey: "b", boot: "0532ee1e", cursor: "s=…" },
            { timeText: "18/10 13:43:55", priority: 4, priorityName: "avertissement", unit: "nginx.service", pid: "812", message: "an upstream response is buffered to a temporary file /var/cache/nginx/proxy_temp/4/00/0000000004", tkey: "c", boot: "0532ee1e", cursor: "s=…" },
            { timeText: "18/10 13:44:40", priority: 3, priorityName: "erreur", unit: "nginx.service", pid: "812", message: "upstream timed out (110: Connection timed out) while reading response header from upstream, client: 10.0.0.7", tkey: "b", boot: "0532ee1e", cursor: "s=…" },
            { timeText: "18/10 13:45:01", priority: 6, priorityName: "info", unit: "nginx.service", pid: "812", message: "worker process 913 exited with code 0", tkey: "d", boot: "0532ee1e", cursor: "s=…" }
        ]
        readonly property var patterns: [
            { key: "b", template: "upstream timed out (#: Connection timed out) while reading response header from upstream, client: ‹ip›", sample: "", unit: "nginx.service", count: 3, priority: 3, priorityName: "erreur", first: "18/10 13:41:03", last: "18/10 13:44:40" },
            { key: "a", template: "Reloading nginx configuration", unit: "nginx.service", count: 1, priority: 6, priorityName: "info", first: "18/10 13:41:02", last: "18/10 13:41:02" },
            { key: "c", template: "an upstream response is buffered to a temporary file /var/cache/nginx/proxy_temp/#/#/#", unit: "nginx.service", count: 1, priority: 4, priorityName: "avertissement", first: "18/10 13:43:55", last: "18/10 13:43:55" }
        ]
    }

    component Segment: Rectangle {
        property string label
        property bool on
        signal picked()
        height: win.adaptive ? 44 : 24; radius: 6; width: segText.implicitWidth + 14
        color: on ? Theme.selection : (segMouse.containsMouse ? "#26FFFFFF" : "transparent")
        Text { id: segText; anchors.centerIn: parent; text: parent.label; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
        MouseArea { id: segMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.picked() }
    }

    RectangularShadow { anchors.fill: parent; offset.y: 14; radius: 16; blur: 44; color: "#73000814" }
    Glass { id: body; anchors.fill: parent; backdrop: win.backdrop; radius: 16; shadow: false; tint: "#B3121C30"; blurAmount: 0.9 }

    // ── sidebar ────────────────────────────────────────────────────────
    Rectangle {
        id: sidebar
        visible: !win.drawer || win.navigationOpen
        z: win.drawer ? 40 : 0
        width: 210; height: parent.height
        topLeftRadius: 16; bottomLeftRadius: 16
        color: "#40050B18"
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#1FFFFFFF" }

        WindowDragArea {
            anchors.fill: parent
            windowItem: win
            movable: !win.adaptive
        }

        Row {
            x: 14; y: 17; spacing: 9
            Repeater {
                model: ["#FF5F57", "#FEBC2E", "#28C840"]
                Rectangle {
                    required property string modelData
                    width: 12; height: 12; radius: 6; color: modelData
                    MouseArea { anchors.fill: parent; onClicked: if (parent.modelData === "#FF5F57") win.closeRequested() }
                }
            }
        }
        Text { x: 90; y: 13; text: "Journaux"; color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold; font.family: Theme.fontFamily }

        Column {
            id: sources
            x: 8; y: 44; width: parent.width - 16
            spacing: 1
            Text { x: 10; text: "SOURCES"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2; font.family: Theme.fontFamily; bottomPadding: 4 }
            Repeater {
                model: [ { s: "journal", g: "≣", t: "Journal système" }, { s: "user", g: "☺", t: "Mon journal" } ]
                Rectangle {
                    required property var modelData
                    readonly property bool active: win.lc && win.lc.source === modelData.s
                    width: parent.width; height: win.adaptive ? 44 : 28; radius: 7
                    color: active ? Theme.selection : (srcMouse.containsMouse ? "#1AFFFFFF" : "transparent")
                    SymbolGlyph { x: 10; anchors.verticalCenter: parent.verticalCenter; glyph: modelData.g; color: "white"; size: 13 }
                    Text { x: 32; anchors.verticalCenter: parent.verticalCenter; text: modelData.t; color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily }
                    MouseArea { id: srcMouse; anchors.fill: parent; hoverEnabled: true; enabled: win.connected; onClicked: win.choose(modelData.s, "") }
                }
            }
        }

        Text { id: servicesTitle; x: 18; y: sources.y + sources.height + 12; text: "SERVICES"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2; font.family: Theme.fontFamily }
        Rectangle {
            id: unitSearch
            x: 10; y: servicesTitle.y + 16; width: parent.width - 20; height: 24; radius: 7
            color: "#1AFFFFFF"
            TextInput {
                id: unitInput
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily; clip: true
                onTextEdited: win.unitFilter = text
                Text { visible: parent.text.length === 0; anchors.verticalCenter: parent.verticalCenter; text: "Filtrer les services"; color: "#7F93B5"; font.pixelSize: 11; font.family: Theme.fontFamily }
            }
        }
        ListView {
            id: unitList
            interactive: count > 0
            x: 8; y: unitSearch.y + 28; width: parent.width - 16
            height: (parent.height - y - 30) * 0.55
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: win.lc ? win.lc.units.filter(u => win.unitFilter.length === 0 || u.name.toLowerCase().indexOf(win.unitFilter.toLowerCase()) >= 0) : []
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property var modelData
                readonly property bool active: win.lc && (win.lc.source === "unit" || win.lc.source === "userunit") && win.lc.target === modelData.name
                width: unitList.width; height: win.adaptive ? 44 : 24; radius: 6
                color: active ? Theme.selection : (uMouse.containsMouse ? "#1AFFFFFF" : "transparent")
                Rectangle {
                    x: 10; anchors.verticalCenter: parent.verticalCenter; width: 7; height: 7; radius: 3.5
                    color: modelData.sub === "failed" ? "#F87171" : (modelData.sub === "running" ? "#4ADE80" : "#64748B")
                }
                Text {
                    x: 24; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 30 - (modelData.user ? 40 : 0); elide: Text.ElideRight
                    text: modelData.name.replace(/\.service$/, ""); color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily
                }
                Text {
                    visible: modelData.user === true
                    anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter
                    text: "utilisateur"; color: "#7F93B5"; font.pixelSize: 8; font.family: Theme.fontFamily
                }
                MouseArea { id: uMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.choose(modelData.user ? "userunit" : "unit", modelData.name) }
            }
        }

        Text { id: filesTitle; x: 18; y: unitList.y + unitList.height + 8; text: "FICHIERS /var/log"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2; font.family: Theme.fontFamily }
        ListView {
            id: fileList
            interactive: count > 0
            x: 8; y: filesTitle.y + 16; width: parent.width - 16
            height: parent.height - y - 10
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: win.lc ? win.lc.files : []
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property var modelData
                readonly property bool active: win.lc && win.lc.source === "file" && win.lc.target === modelData.path
                width: fileList.width; height: win.adaptive ? 44 : 24; radius: 6
                color: active ? Theme.selection : (fMouse.containsMouse ? "#1AFFFFFF" : "transparent")
                opacity: modelData.readable ? 1 : 0.5
                SymbolGlyph { x: 10; anchors.verticalCenter: parent.verticalCenter; glyph: modelData.readable ? "▤" : "🔒"; color: "white"; size: 10 }
                Text {
                    x: 26; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 32; elide: Text.ElideMiddle
                    text: modelData.name; color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily
                }
                MouseArea { id: fMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.choose("file", modelData.path) }
                ToolTipLite { text: modelData.readable ? modelData.path : "Non lisible par cet utilisateur"; shown: fMouse.containsMouse }
            }
        }
    }

    Rectangle {
        anchors.fill: parent; visible: win.drawer && win.navigationOpen; z: 35
        color: "#80000000"
        MouseArea { anchors.fill: parent; onClicked: win.navigationOpen = false }
    }
    ToolButtonGlass {
        visible: win.drawer; x: 4; y: 2; z: 2; touch: true; glyph: "☰"
        onClicked: win.navigationOpen = !win.navigationOpen
    }
    // ── toolbar ────────────────────────────────────────────────────────
    Item {
        id: toolbar
        x: win.contentLeft; width: parent.width - x; height: 48
        WindowDragArea {
            anchors.fill: parent
            windowItem: win
            movable: !win.adaptive
        }
        Text {
            x: win.drawer ? 54 : 14; anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - tools.width - x - 12); elide: Text.ElideMiddle
            text: win.sourceTitle()
            color: Theme.ink; font.pixelSize: 14; font.weight: Font.DemiBold; font.family: Theme.fontFamily
        }
        Row {
            id: tools
            anchors.right: parent.right; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Rectangle {
                height: 26; radius: 13; width: liveText.implicitWidth + 30
                color: win.lc && win.lc.following ? "#3316A34A" : (liveMouse.containsMouse ? "#26FFFFFF" : "#14FFFFFF")
                border.width: 1; border.color: win.lc && win.lc.following ? "#8016A34A" : "#26FFFFFF"
                Rectangle {
                    x: 10; anchors.verticalCenter: parent.verticalCenter; width: 8; height: 8; radius: 4
                    color: win.lc && win.lc.following ? "#4ADE80" : "#64748B"
                    SequentialAnimation on opacity {
                        running: win.lc && win.lc.following; loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: 700 } NumberAnimation { to: 1; duration: 700 }
                    }
                }
                Text { id: liveText; x: 23; anchors.verticalCenter: parent.verticalCenter; text: "En direct"; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                MouseArea { id: liveMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; enabled: !win.fixture && win.connected; onClicked: win.logs.setFollowing(!win.logs.following) }
            }
            ToolButtonGlass { glyph: "↻"; tip: "Recharger"; enabledState: !win.fixture && win.connected && win.lc && !win.lc.busy; onClicked: win.logs.reload() }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1AFFFFFF" }
    }

    // ── counts, state, view switch, text filter ───────────────────────
    Flickable {
        id: strip
        x: win.contentLeft; y: toolbar.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: win.adaptive ? Math.max(width, 680) : width
        contentHeight: height
        width: win.width - x; height: (win.adaptive ? 160 : 72) + (banner.visible ? banner.height + 6 : 0)
        Item {
            width: strip.contentWidth; height: strip.height
        Row {
            id: rangeRow
            x: 12; y: 8
            visible: win.lc && win.lc.source !== "file"
            Repeater {
                model: [ { k: "15min", t: "15 min" }, { k: "1h", t: "1 h" }, { k: "24h", t: "24 h" }, { k: "7d", t: "7 j" }, { k: "boot", t: "Démarrage" } ]
                Segment { required property var modelData; label: modelData.t; on: win.lc && win.lc.range === modelData.k; onPicked: if (!win.fixture) win.logs.setRange(modelData.k) }
            }
        }
        Row {
            id: prioRow
            anchors.left: rangeRow.visible ? rangeRow.right : parent.left; anchors.leftMargin: 14; y: 8
            visible: win.lc && win.lc.source !== "file"
            Repeater {
                model: [ { p: 3, t: "Erreurs" }, { p: 4, t: "Avert." }, { p: 6, t: "Info" }, { p: 7, t: "Tout" } ]
                Segment { required property var modelData; label: modelData.t; on: win.lc && win.lc.maxPriority === modelData.p; onPicked: if (!win.fixture) win.logs.setMaxPriority(modelData.p) }
            }
        }
        Row {
            x: 12; y: win.adaptive ? 60 : 40; spacing: 6
            Repeater {
                model: win.lc ? win.lc.counts : []
                Rectangle {
                    required property var modelData
                    height: 24; radius: 12; width: cText.implicitWidth + 26
                    color: "#14FFFFFF"; border.width: 1; border.color: "#1FFFFFFF"
                    opacity: modelData.count > 0 ? 1 : 0.45
                    Rectangle { x: 9; anchors.verticalCenter: parent.verticalCenter; width: 7; height: 7; radius: 3.5; color: win.priorityColor(modelData.level) }
                    Text { id: cText; x: 20; anchors.verticalCenter: parent.verticalCenter; text: modelData.count + " " + modelData.label; color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily }
                }
            }
        }
        Row {
            anchors.right: win.adaptive ? undefined : parent.right; anchors.rightMargin: 12; x: win.adaptive ? 12 : 0; y: win.adaptive ? 104 : 8
            spacing: 6
            Rectangle {
                width: 110; height: 24; radius: 12; color: "#1AFFFFFF"
                border.width: 1; border.color: filterInput.activeFocus ? Theme.accent : "#1FFFFFFF"
                TextInput {
                    id: filterInput
                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily; clip: true
                    onTextEdited: win.textFilter = text
                    Text { visible: parent.text.length === 0; anchors.verticalCenter: parent.verticalCenter; text: "Filtrer le texte"; color: "#7F93B5"; font.pixelSize: 11; font.family: Theme.fontFamily }
                }
            }
            Row {
                Repeater {
                    model: win.unitSelected
                        ? [ { k: "service", t: "Service" }, { k: "timeline", t: "Chronologie" }, { k: "patterns", t: "Répétitions" } ]
                        : [ { k: "timeline", t: "Chronologie" }, { k: "patterns", t: "Répétitions" } ]
                    Rectangle {
                        required property var modelData
                        height: win.adaptive ? 44 : 24; radius: 6; width: vt.implicitWidth + 14
                        color: win.view === modelData.k ? Theme.selection : (vm.containsMouse ? "#26FFFFFF" : "transparent")
                        Text { id: vt; anchors.centerIn: parent; text: modelData.t; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                        MouseArea {
                            id: vm; anchors.fill: parent; hoverEnabled: true
                            onClicked: {
                                win.view = modelData.k
                                if (modelData.k === "service" && !win.fixture && win.logs && Object.keys(win.logs.serviceInfo).length === 0) win.logs.loadService()
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            id: banner
            visible: win.lc && win.lc.stateText.length > 0
            x: 12; y: win.adaptive ? 160 : 72; width: win.adaptive ? strip.width - 24 : parent.width - 24
            height: bannerText.implicitHeight + 12; radius: 8
            color: win.lc && (win.lc.state === "limited" || win.lc.state === "rotated") ? "#26FACC15" : "#33F87171"
            border.width: 1; border.color: win.lc && (win.lc.state === "limited" || win.lc.state === "rotated") ? "#66FACC15" : "#66F87171"
            Text {
                id: bannerText
                x: 10; y: 6; width: parent.width - 20; wrapMode: Text.WordWrap
                text: win.lc ? win.lc.stateText : ""
                textFormat: Text.PlainText
                color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily
            }
        }
        }
    }

    // ── content ────────────────────────────────────────────────────────
    Item {
        id: content
        x: win.contentLeft; y: strip.y + strip.height
        width: parent.width - x; height: parent.height - y - statusBar.height
        clip: true

        Rectangle {
            visible: win.patternKey.length > 0 && win.view === "timeline"
            x: 12; y: 2; width: parent.width - 24; height: win.adaptive ? 44 : 24; radius: 6; z: 2
            color: "#262F8CFF"
            Text {
                x: 10; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 120; elide: Text.ElideRight
                text: "Répétition : " + win.patternLabel; textFormat: Text.PlainText
                color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                text: "Tout afficher ×"; color: "#9CC6FF"; font.pixelSize: 11; font.family: Theme.fontFamily
                MouseArea { anchors.fill: parent; onClicked: { win.patternKey = ""; win.patternLabel = "" } }
            }
        }

        ListView {
            id: timeline
            visible: win.view === "timeline"
            anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
            anchors.topMargin: win.patternKey.length > 0 ? 30 : 2
            model: win.shownEvents
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            cacheBuffer: 800
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            // Newest at the bottom; stay pinned there while following.
            property bool pinned: true
            onMovementEnded: pinned = atYEnd
            onCountChanged: if (pinned) Qt.callLater(positionViewAtEnd)
            delegate: Item {
                id: ev
                required property var modelData
                required property int index
                readonly property bool open: win.expanded === index
                width: timeline.width
                height: open ? Math.max(30, full.implicitHeight + meta.implicitHeight + 18 + 30) : 26
                Rectangle {
                    anchors.fill: parent; radius: 6
                    color: ev.open ? "#262F8CFF" : (evMouse.containsMouse ? "#12FFFFFF" : (ev.index % 2 ? "#08FFFFFF" : "transparent"))
                }
                Rectangle { x: 4; y: 5; width: 3; height: 16; radius: 1.5; color: win.priorityColor(ev.modelData.priority) }
                Text { x: 14; y: 5; width: 98; text: ev.modelData.timeText || "—"; color: "#9FB2D0"; font.pixelSize: 11; font.family: Theme.monoFamily }
                Text {
                    x: 116; y: 5; width: 120; elide: Text.ElideRight
                    text: ev.modelData.unit; textFormat: Text.PlainText
                    color: "#B9C6DA"; font.pixelSize: 11; font.family: Theme.fontFamily
                }
                Text {
                    visible: !ev.open
                    x: 242; y: 5; width: parent.width - x - 8; elide: Text.ElideRight
                    text: ev.modelData.message; textFormat: Text.PlainText
                    color: ev.modelData.priority >= 0 && ev.modelData.priority <= 3 ? "#FECACA" : Theme.ink
                    font.pixelSize: 11; font.family: Theme.monoFamily
                }
                TextEdit {
                    id: full
                    visible: ev.open
                    x: 242; y: 5; width: parent.width - x - 8
                    readOnly: true; selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere; textFormat: TextEdit.PlainText
                    text: ev.open ? ev.modelData.message : ""
                    color: Theme.ink; selectionColor: Theme.accent; font.pixelSize: 11; font.family: Theme.monoFamily
                }
                Text {
                    visible: ev.open
                    anchors.right: parent.right; anchors.rightMargin: 10
                    y: meta.y
                    text: "Inspecter autour (±5 min) →"
                    color: "#9CC6FF"; font.pixelSize: 10; font.underline: inspMouse.containsMouse; font.family: Theme.fontFamily
                    z: 3
                    MouseArea {
                        id: inspMouse
                        anchors.fill: parent; anchors.margins: -4; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        enabled: !win.fixture && ev.modelData.time > 0
                        onClicked: win.logs.inspect(ev.modelData, 300)
                    }
                }
                Row {
                    visible: ev.open
                    x: 242; y: meta.y + meta.implicitHeight + 6; spacing: 8
                    CopyChip { objectName: "logsCopyEvent"; touch: win.adaptive
                        onClicked: win.requestCopy("événement du journal", (ev.modelData.timeText || "") + "  " + ev.modelData.unit + "  " + ev.modelData.message) }
                    CopyChip { objectName: "logsShowService"; label: "Service"; touch: win.adaptive; visible: !win.fixture && ev.modelData.unit.length > 0 && /\.(service|socket|timer|mount)$/.test(ev.modelData.unit)
                        onClicked: win.requestService(ev.modelData.unit) }
                }
                Text {
                    id: meta
                    visible: ev.open
                    x: 242; y: full.y + full.height + 6; width: parent.width - x - 8; elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: "priorité " + ev.modelData.priorityName + (ev.modelData.pid ? "  ·  pid " + ev.modelData.pid : "")
                          + (ev.modelData.boot ? "  ·  démarrage " + ev.modelData.boot : "")
                    color: "#7F93B5"; font.pixelSize: 10; font.family: Theme.fontFamily
                }
                MouseArea {
                    id: evMouse
                    anchors.fill: parent; hoverEnabled: true
                    enabled: !ev.open
                    onClicked: win.expanded = ev.index
                }
                MouseArea { visible: ev.open; width: 236; height: parent.height; onClicked: win.expanded = -1 }
            }
        }

        ListView {
            id: patternList
            visible: win.view === "patterns"
            anchors.fill: parent; anchors.margins: 6
            model: win.lc ? win.lc.patterns : []
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                id: pat
                required property var modelData
                width: patternList.width; height: 46; radius: 8
                color: pMouse.containsMouse ? "#1AFFFFFF" : "#0DFFFFFF"
                Rectangle {
                    x: 10; anchors.verticalCenter: parent.verticalCenter
                    width: 46; height: 26; radius: 13
                    color: Qt.rgba(0, 0, 0, 0.25); border.width: 1; border.color: win.priorityColor(pat.modelData.priority)
                    Text { anchors.centerIn: parent; text: "×" + pat.modelData.count; color: "white"; font.pixelSize: 12; font.bold: true; font.family: Theme.fontFamily }
                }
                Text {
                    x: 66; y: 6; width: parent.width - x - 12; elide: Text.ElideRight
                    text: pat.modelData.template; textFormat: Text.PlainText
                    color: Theme.ink; font.pixelSize: 11; font.family: Theme.monoFamily
                }
                Text {
                    x: 66; y: 25; width: parent.width - x - 12; elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: pat.modelData.unit + "  ·  " + pat.modelData.priorityName + "  ·  " + pat.modelData.first
                          + (pat.modelData.count > 1 ? " → " + pat.modelData.last : "")
                    color: "#7F93B5"; font.pixelSize: 10; font.family: Theme.fontFamily
                }
                MouseArea {
                    id: pMouse
                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { win.patternKey = pat.modelData.key; win.patternLabel = pat.modelData.template; win.view = "timeline"; win.expanded = -1 }
                }
            }
        }

        ServicePanel {
            anchors.fill: parent
            visible: win.view === "service" && win.unitSelected
            lc: win.lc
            onShowJournal: win.view = "timeline"
        }

        Column {
            visible: win.view !== "service" && win.lc && !win.lc.busy && (win.view === "timeline" ? win.shownEvents.length === 0 : win.lc.patterns.length === 0)
            anchors.centerIn: parent
            spacing: 6
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: !win.connected ? "Aucun serveur connecté : choisissez-en un en haut à gauche."
                    : (win.lc && win.lc.events.length > 0 ? "Aucun événement ne correspond au filtre." : "Aucun événement dans cette période.")
                color: Theme.ink; font.pixelSize: 13; font.family: Theme.fontFamily
            }
        }
        BusyIndicator { anchors.centerIn: parent; running: win.lc && win.lc.busy && win.lc.events.length === 0; visible: running }
    }

    IncidentInspector {
        x: win.contentLeft; y: toolbar.height
        width: parent.width - x; height: parent.height - y
        bottomRightRadius: 16
        z: 20
        visible: win.lc !== null && win.lc.inspectorOpen === true
        lc: win.lc
        priorityColor: win.priorityColor
        onOpenUnit: (unit) => { win.logs.closeInspector(); win.choose("unit", unit); win.logs.setRange("24h") }
        onClosed: if (!win.fixture) win.logs.closeInspector()
    }

    // ── status bar ─────────────────────────────────────────────────────
    Item {
        id: statusBar
        x: win.contentLeft; y: parent.height - height
        width: parent.width - x; height: 26
        Rectangle { width: parent.width; height: 1; color: "#14FFFFFF" }
        Text {
            x: 14; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 220; elide: Text.ElideRight
            text: win.lc ? win.lc.coverage + (win.shownEvents.length !== win.lc.events.length ? "  ·  " + win.shownEvents.length + " affichés" : "") : ""
            color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 22; anchors.verticalCenter: parent.verticalCenter
            text: !win.lc ? "" : win.lc.state === "disconnected" ? "source déconnectée" + (win.lc.dataLabel ? " · données de " + win.lc.dataLabel : "")
                : (win.lc.busy ? "lecture…" : (win.lc.following ? "en direct · " : "") + "màj " + win.lc.lastUpdate) + "  ·  lecture seule"
            color: win.lc && win.lc.state === "disconnected" ? "#FCA5A5" : "#7F93B5"
            font.pixelSize: 10; font.family: Theme.fontFamily
        }
    }

    MouseArea {
        width: 16; height: 16
        anchors.right: parent.right; anchors.bottom: parent.bottom
        cursorShape: Qt.SizeFDiagCursor
        property point press
        onPressed: (m) => press = Qt.point(m.x, m.y)
        onPositionChanged: (m) => {
            win.width = Math.max(640, win.width + m.x - press.x)
            win.height = Math.max(380, win.height + m.y - press.y)
        }
    }
    LinkBanner { remote: win.remote; touch: win.adaptive; onRetry: if (win.logs) win.logs.reload() }
}
