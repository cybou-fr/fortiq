import QtQuick
import QtQuick.Controls

// Système (lecture seule): processus, services systemd et vue d'ensemble du serveur.
// Rien n'est démarré, arrêté ni tué (docs/SYSTEM.md).
Item {
    id: win
    objectName: "systemWindow"
    property bool adaptive: false
    property Item backdrop
    property var sys: null
    property var remote: null
    property string section: "processes"
    property bool navigationOpen: false
    property bool autoRefresh: false
    property bool showKernel: false
    property string filterText: ""
    property string sortKey: "cpu"
    property bool sortDescending: true
    property string serviceFilter: "all"
    property int selectedPid: -1
    readonly property bool connected: remote !== null && remote.connected
    readonly property bool drawer: adaptive && width < 900
    readonly property real contentLeft: drawer ? 0 : 196
    readonly property var overview: sys ? sys.overview : ({})
    readonly property int failedCount: overview.failedServices === undefined ? 0 : overview.failedServices
    signal closeRequested()
    signal requestServers()

    function sync() { if (sys) sys.setWindowActive(visible) }
    onVisibleChanged: sync()
    onSysChanged: sync()
    Component.onCompleted: sync()
    function handleBack() { if (navigationOpen) { navigationOpen = false; return true } if (selectedPid >= 0) { selectedPid = -1; return true } return false }

    // Filter and sort in RAM so the list reacts per keystroke.
    readonly property var shownProcesses: {
        if (!sys) return []
        const needle = filterText.trim().toLowerCase()
        const rows = sys.processes.filter(p => (showKernel || !p.kernel)
            && (needle.length === 0 || p.name.toLowerCase().indexOf(needle) >= 0 || p.command.toLowerCase().indexOf(needle) >= 0
                || p.user.toLowerCase().indexOf(needle) >= 0 || String(p.pid) === needle))
        const key = sortKey, dir = sortDescending ? -1 : 1
        rows.sort((a, b) => {
            let r = 0
            if (key === "cpu") r = a.cpu - b.cpu
            else if (key === "mem") r = a.rssKb - b.rssKb
            else if (key === "pid") r = a.pid - b.pid
            else r = a.name.localeCompare(b.name, undefined, { sensitivity: "base" })
            if (r === 0) r = a.pid - b.pid
            return r * dir
        })
        return rows
    }
    readonly property var shownServices: {
        if (!sys) return []
        const needle = filterText.trim().toLowerCase()
        const rows = sys.services.filter(s => (serviceFilter === "all" || (serviceFilter === "active" && s.active === "active")
                || (serviceFilter === "failed" && s.failed) || (serviceFilter === "inactive" && s.active === "inactive" && s.load === "loaded"))
            && (needle.length === 0 || s.unit.toLowerCase().indexOf(needle) >= 0 || s.description.toLowerCase().indexOf(needle) >= 0))
        rows.sort((a, b) => (a.failed !== b.failed) ? (a.failed ? -1 : 1) : a.unit.localeCompare(b.unit))
        return rows
    }
    readonly property var selectedProcess: {
        for (const p of shownProcesses) if (p.pid === selectedPid) return p
        return null
    }
    function stateText(s) {
        const c = s.charAt(0)
        return c === "R" ? "en cours d'exécution" : c === "S" ? "en attente (sommeil)" : c === "D" ? "attente d'entrée/sortie (non interruptible)"
             : c === "Z" ? "zombie (terminé, non récupéré par son parent)" : c === "T" ? "arrêté" : c === "I" ? "inactif (thread noyau)" : s
    }
    function stateColor(s) { const c = s.charAt(0); return c === "R" ? "#4ADE80" : c === "Z" ? "#FCA5A5" : c === "D" ? "#FCD34D" : Theme.inkMuted }
    function pct(v) { return v.toFixed(1) + " %" }

    Timer {
        interval: 5000; repeat: true
        running: win.autoRefresh && win.visible && win.connected && win.sys !== null && win.section !== "services"
        onTriggered: if (!win.sys.busy) win.sys.reload()
    }

    Glass { anchors.fill: parent; backdrop: win.backdrop; tint: "#B3121C30"; blurAmount: 0.9; radius: 16 }
    MouseArea { visible: win.drawer && win.navigationOpen; anchors.fill: parent; z: 30; onClicked: win.navigationOpen = false }

    Rectangle {
        objectName: "systemSidebar"
        visible: !win.drawer || win.navigationOpen
        width: win.drawer ? Math.min(260, win.width - 32) : 196; height: parent.height; z: win.drawer ? 40 : 0
        topLeftRadius: 16; bottomLeftRadius: 16; color: win.drawer ? "#F0121C30" : "#40050B18"
        WindowDragArea { anchors.fill: parent; windowItem: win; movable: !win.adaptive }
        Row {
            visible: !win.adaptive; x: 14; y: 17; spacing: 9
            Repeater {
                model: ["#FF5F57", "#FEBC2E", "#28C840"]
                Rectangle {
                    required property string modelData
                    width: 12; height: 12; radius: 6; color: modelData
                    MouseArea { anchors.fill: parent; onClicked: if (parent.modelData === "#FF5F57") win.closeRequested() }
                }
            }
        }
        Text { x: 18; y: 48; text: "SYSTÈME"; color: Theme.inkMuted; font.pixelSize: 10; font.letterSpacing: 1 }
        Column {
            x: 8; y: 72; width: parent.width - 16; spacing: 6
            Repeater {
                model: [ { key: "processes", title: "Processus" }, { key: "services", title: "Services" }, { key: "overview", title: "Vue d'ensemble" } ]
                Rectangle {
                    required property var modelData
                    objectName: "systemSection_" + modelData.key
                    width: parent.width; height: win.adaptive ? 44 : 34; radius: 8
                    color: win.section === modelData.key ? "#D02D73DA" : "transparent"
                    Text { x: 12; anchors.verticalCenter: parent.verticalCenter; text: modelData.title; color: Theme.ink; font.pixelSize: 12 }
                    Text {
                        visible: modelData.key === "services" && win.failedCount > 0
                        anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                        text: win.failedCount; color: "#FCA5A5"; font.pixelSize: 11; font.bold: true
                    }
                    MouseArea { anchors.fill: parent; onClicked: { win.section = modelData.key; win.navigationOpen = false; win.filterText = "" } }
                }
            }
        }
        Text {
            x: 18; y: 230; width: parent.width - 36; wrapMode: Text.WordWrap
            text: "Serveur Linux sélectionné\nLecture seule : aucun processus ni service n'est modifié"
            color: Theme.inkMuted; font.pixelSize: 11
        }
    }

    // ── header ─────────────────────────────────────────────────────────
    Item {
        id: header
        x: win.contentLeft; width: parent.width - x; height: 48
        WindowDragArea { anchors.fill: parent; windowItem: win; movable: !win.adaptive }
        ToolButtonGlass { visible: win.drawer; x: 4; y: 2; touch: true; glyph: "☰"; onClicked: win.navigationOpen = !win.navigationOpen }
        Text {
            x: win.drawer ? 54 : 14; width: Math.max(0, parent.width - x - 140); anchors.verticalCenter: parent.verticalCenter
            text: "Système · " + (win.connected ? (win.overview.hostname ? win.overview.hostname : win.remote.serverLabel) : "Aucun serveur")
            elide: Text.ElideMiddle; textFormat: Text.PlainText; color: Theme.ink; font.pixelSize: 14
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 4
            Rectangle {
                objectName: "systemAuto"
                visible: win.section !== "services"
                height: win.adaptive ? 44 : 28; radius: 8; width: autoLabel.implicitWidth + 20
                color: win.autoRefresh ? "#592F8CFF" : (autoMouse.containsMouse ? "#26FFFFFF" : "transparent")
                border.width: win.autoRefresh ? 1 : 0; border.color: "#802F8CFF"
                Text { id: autoLabel; anchors.centerIn: parent; text: "Auto 5 s"; color: Theme.ink; font.pixelSize: 11 }
                MouseArea { id: autoMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.autoRefresh = !win.autoRefresh }
            }
            ToolButtonGlass {
                touch: win.adaptive; glyph: "↻"; tip: "Actualiser"
                enabledState: win.connected && win.sys !== null && !win.sys.busy; onClicked: win.sys.reload()
            }
        }
    }

    // ── processes ──────────────────────────────────────────────────────
    Item {
        id: procPage
        visible: win.connected && win.sys !== null && win.section === "processes" && win.sys.state !== "disconnected"
        x: win.contentLeft + 12; y: 52; width: parent.width - x - 12; height: parent.height - y - 10

        Row {
            id: procTools
            width: parent.width; spacing: 6
            Rectangle {
                width: Math.max(140, parent.width - sortRow.width - kernelBox.width - 12); height: win.adaptive ? 44 : 30; radius: 15
                color: "#1AFFFFFF"; border.width: 1; border.color: filterInput.activeFocus ? Theme.accent : "#1FFFFFFF"
                TextInput {
                    id: filterInput
                    objectName: "systemFilter"
                    anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; verticalAlignment: TextInput.AlignVCenter
                    color: Theme.ink; font.pixelSize: 12; clip: true; selectByMouse: true
                    text: win.filterText; onTextEdited: win.filterText = text
                    Text { visible: parent.text.length === 0; anchors.verticalCenter: parent.verticalCenter; text: "Filtrer : nom, commande, utilisateur, PID"; color: "#7F93B5"; font.pixelSize: 12 }
                }
            }
            Row {
                id: sortRow
                spacing: 2
                Repeater {
                    model: [ { k: "cpu", t: "CPU" }, { k: "mem", t: "Mém." }, { k: "pid", t: "PID" }, { k: "name", t: "Nom" } ]
                    Rectangle {
                        required property var modelData
                        objectName: "systemSort_" + modelData.k
                        height: win.adaptive ? 44 : 28; radius: 8; width: sortLabel.implicitWidth + 18
                        color: win.sortKey === modelData.k ? "#592F8CFF" : (sortMouse.containsMouse ? "#26FFFFFF" : "transparent")
                        border.width: win.sortKey === modelData.k ? 1 : 0; border.color: "#802F8CFF"
                        Text { id: sortLabel; anchors.centerIn: parent; text: modelData.t + (win.sortKey === modelData.k ? (win.sortDescending ? " ▼" : " ▲") : ""); color: Theme.ink; font.pixelSize: 11 }
                        MouseArea {
                            id: sortMouse; anchors.fill: parent; hoverEnabled: true
                            onClicked: { if (win.sortKey === modelData.k) win.sortDescending = !win.sortDescending; else { win.sortKey = modelData.k; win.sortDescending = modelData.k !== "name" && modelData.k !== "pid" } }
                        }
                    }
                }
            }
            Rectangle {
                id: kernelBox
                objectName: "systemKernel"
                height: win.adaptive ? 44 : 28; radius: 8; width: kernelLabel.implicitWidth + 18
                color: win.showKernel ? "#592F8CFF" : (kernelMouse.containsMouse ? "#26FFFFFF" : "transparent")
                border.width: win.showKernel ? 1 : 0; border.color: "#802F8CFF"
                Text { id: kernelLabel; anchors.centerIn: parent; text: "Threads noyau"; color: Theme.ink; font.pixelSize: 11 }
                MouseArea { id: kernelMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.showKernel = !win.showKernel }
            }
        }
        Text {
            id: procSummary
            y: procTools.height + 6; width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText; font.pixelSize: 11
            color: win.sys && win.sys.state === "error" ? "#FCA5A5" : Theme.inkMuted
            text: !win.sys ? "" : win.sys.state === "error" ? win.sys.status
                : win.overview.processes + " processus · " + win.overview.running + " en cours · " + win.overview.zombies + " zombie(s) · "
                  + win.shownProcesses.length + " affichés" + (win.sys.updated ? " · " + win.sys.updated : "")
        }
        ListView {
            id: procList
            objectName: "systemProcessList"
            y: procSummary.y + procSummary.height + 6; width: parent.width
            height: Math.max(0, parent.height - y - (win.selectedProcess ? detail.height + 8 : 0))
            clip: true; spacing: 2; model: win.shownProcesses
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width - 10; height: win.adaptive ? 56 : 44; radius: 8
                color: win.selectedPid === modelData.pid ? "#40307CF0" : (rowMouse.containsMouse ? "#14FFFFFF" : "transparent")
                Rectangle { x: 8; y: 10; width: 8; height: 8; radius: 4; color: win.stateColor(modelData.state) }
                Text {
                    x: 24; y: 4; width: parent.width - 250; elide: Text.ElideRight; textFormat: Text.PlainText
                    text: modelData.name; color: modelData.kernel ? Theme.inkMuted : Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold
                }
                Text {
                    x: 24; y: 23; width: parent.width - 250; elide: Text.ElideMiddle; textFormat: Text.PlainText
                    text: modelData.user + "  ·  pid " + modelData.pid + "  ·  " + modelData.command; color: Theme.inkMuted; font.pixelSize: 10
                }
                Text { anchors.right: parent.right; anchors.rightMargin: 150; y: 6; width: 80; horizontalAlignment: Text.AlignRight; text: win.pct(modelData.cpu); color: modelData.cpu >= 50 ? "#FCD34D" : Theme.ink; font.pixelSize: 12; font.family: "Consolas" }
                Text { anchors.right: parent.right; anchors.rightMargin: 70; y: 6; width: 76; horizontalAlignment: Text.AlignRight; text: win.pct(modelData.mem); color: Theme.ink; font.pixelSize: 12; font.family: "Consolas" }
                Text { anchors.right: parent.right; anchors.rightMargin: 10; y: 6; width: 70; horizontalAlignment: Text.AlignRight; text: modelData.rss; color: Theme.inkMuted; font.pixelSize: 11; font.family: "Consolas" }
                Text { anchors.right: parent.right; anchors.rightMargin: 10; y: 24; width: 200; horizontalAlignment: Text.AlignRight; text: modelData.threads + " thread(s) · " + modelData.elapsed; color: Theme.inkMuted; font.pixelSize: 10 }
                MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.selectedPid = win.selectedPid === modelData.pid ? -1 : modelData.pid }
            }
        }
        Rectangle {
            id: detail
            objectName: "systemDetail"
            visible: win.selectedProcess !== null
            anchors.bottom: parent.bottom; width: parent.width; height: visible ? detailText.implicitHeight + 22 : 0
            radius: 10; color: "#40182740"; border.width: 1; border.color: "#25DCEBFF"
            Text {
                id: detailText
                x: 12; y: 10; width: parent.width - 24; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: Theme.ink; font.pixelSize: 11; font.family: "Consolas"
                text: win.selectedProcess ? win.selectedProcess.command + "\n\nPID " + win.selectedProcess.pid + "  ·  parent " + win.selectedProcess.ppid
                        + "  ·  utilisateur " + win.selectedProcess.user + "  ·  état " + win.selectedProcess.state + " (" + win.stateText(win.selectedProcess.state) + ")\n"
                        + "démarré il y a " + win.selectedProcess.elapsed + "  ·  " + win.selectedProcess.threads + " thread(s)  ·  mémoire résidente " + win.selectedProcess.rss
                        + "  ·  lecture seule : aucun signal n'est envoyé" : ""
            }
        }
    }

    // ── services ───────────────────────────────────────────────────────
    Item {
        id: svcPage
        visible: win.connected && win.sys !== null && win.section === "services" && win.sys.state !== "disconnected"
        x: win.contentLeft + 12; y: 52; width: parent.width - x - 12; height: parent.height - y - 10
        Row {
            id: svcTools
            width: parent.width; spacing: 6
            Rectangle {
                width: Math.max(140, parent.width - chips.width - 6); height: win.adaptive ? 44 : 30; radius: 15
                color: "#1AFFFFFF"; border.width: 1; border.color: svcFilter.activeFocus ? Theme.accent : "#1FFFFFFF"
                TextInput {
                    id: svcFilter
                    anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; verticalAlignment: TextInput.AlignVCenter
                    color: Theme.ink; font.pixelSize: 12; clip: true; selectByMouse: true
                    text: win.filterText; onTextEdited: win.filterText = text
                    Text { visible: parent.text.length === 0; anchors.verticalCenter: parent.verticalCenter; text: "Filtrer : nom ou description"; color: "#7F93B5"; font.pixelSize: 12 }
                }
            }
            Row {
                id: chips
                spacing: 2
                Repeater {
                    model: [ { k: "all", t: "Tous" }, { k: "active", t: "Actifs" }, { k: "failed", t: "En échec" }, { k: "inactive", t: "Inactifs" } ]
                    Rectangle {
                        required property var modelData
                        objectName: "systemServiceFilter_" + modelData.k
                        height: win.adaptive ? 44 : 28; radius: 8; width: chipLabel.implicitWidth + 18
                        color: win.serviceFilter === modelData.k ? "#592F8CFF" : (chipMouse.containsMouse ? "#26FFFFFF" : "transparent")
                        border.width: win.serviceFilter === modelData.k ? 1 : 0; border.color: "#802F8CFF"
                        Text { id: chipLabel; anchors.centerIn: parent; text: modelData.t; color: Theme.ink; font.pixelSize: 11 }
                        MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.serviceFilter = modelData.k }
                    }
                }
            }
        }
        Text {
            id: svcSummary
            y: svcTools.height + 6; width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText; font.pixelSize: 11; color: Theme.inkMuted
            text: !win.sys ? "" : !win.sys.hasSystemd ? "Ce serveur n'utilise pas systemd."
                : win.overview.services + " services · " + win.overview.activeServices + " actifs · " + win.overview.failedServices + " en échec · " + win.shownServices.length + " affichés"
        }
        ListView {
            objectName: "systemServiceList"
            y: svcSummary.y + svcSummary.height + 6; width: parent.width; height: Math.max(0, parent.height - y)
            clip: true; spacing: 2; model: win.shownServices
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width - 10; height: win.adaptive ? 56 : 44; radius: 8
                color: modelData.failed ? "#26EF4444" : (svcMouse.containsMouse ? "#14FFFFFF" : "transparent")
                Rectangle { x: 8; y: 10; width: 8; height: 8; radius: 4; color: modelData.failed ? "#EF4444" : modelData.active === "active" ? "#4ADE80" : modelData.active === "activating" ? "#FCD34D" : "#64748B" }
                Text { x: 24; y: 4; width: parent.width - 200; elide: Text.ElideRight; textFormat: Text.PlainText; text: modelData.unit; color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold }
                Text { x: 24; y: 23; width: parent.width - 200; elide: Text.ElideRight; textFormat: Text.PlainText; text: modelData.description; color: Theme.inkMuted; font.pixelSize: 10 }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 10; y: 6; width: 180; horizontalAlignment: Text.AlignRight; textFormat: Text.PlainText
                    text: modelData.active + " (" + modelData.sub + ")"; color: modelData.failed ? "#FCA5A5" : Theme.ink; font.pixelSize: 12
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 10; y: 24; width: 180; horizontalAlignment: Text.AlignRight; textFormat: Text.PlainText
                    visible: modelData.load !== "loaded"; text: modelData.load; color: "#FCD34D"; font.pixelSize: 10
                }
                MouseArea { id: svcMouse; anchors.fill: parent; hoverEnabled: true }
            }
        }
    }

    // ── overview ───────────────────────────────────────────────────────
    Flickable {
        visible: win.connected && win.sys !== null && win.section === "overview" && win.sys.state === "ready"
        x: win.contentLeft + 16; y: 58; width: parent.width - x - 16; height: parent.height - y - 12
        contentWidth: width; contentHeight: overviewColumn.height; clip: true; boundsBehavior: Flickable.StopAtBounds
        Column {
            id: overviewColumn
            width: parent.width; spacing: 12
            Text { text: "Vue d'ensemble"; color: Theme.ink; font.pixelSize: 18 }
            Text { width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: Theme.inkMuted; font.pixelSize: 12
                   text: (win.overview.os ? win.overview.os + " · " : "") + "noyau " + win.overview.kernel + " · en ligne depuis " + win.overview.uptime }
            Rectangle {
                width: parent.width; height: 74; radius: 10; color: "#30182740"; border.width: 1; border.color: "#25DCEBFF"
                Text { x: 12; y: 8; text: "CHARGE (moyenne sur 1 / 5 / 15 min)"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2 }
                Text {
                    x: 12; y: 26; font.pixelSize: 15; font.family: "Consolas"; color: win.overview.loadRatio > 1 ? "#FCD34D" : Theme.ink
                    text: Number(win.overview.load1).toFixed(2) + "   " + Number(win.overview.load5).toFixed(2) + "   " + Number(win.overview.load15).toFixed(2) + "      sur " + win.overview.cores + " cœur(s)"
                }
                Text { x: 12; y: 52; font.pixelSize: 11; color: Theme.inkMuted
                       text: win.overview.loadRatio > 1 ? "La charge dépasse le nombre de cœurs : le serveur est saturé." : "Charge par cœur : " + Number(win.overview.loadRatio).toFixed(2) }
            }
            Rectangle {
                width: parent.width; height: 90; radius: 10; color: "#30182740"; border.width: 1; border.color: "#25DCEBFF"
                Text { x: 12; y: 8; text: "MÉMOIRE"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2 }
                Text { x: 12; y: 26; font.pixelSize: 13; color: Theme.ink; text: win.overview.memUsed + " utilisés sur " + win.overview.memTotal + "   ·   disponible " + win.overview.memAvailable }
                Rectangle { x: 12; y: 52; width: parent.width - 24; height: 8; radius: 4; color: "#334F6788"
                    Rectangle { width: parent.width * Math.min(1, win.overview.memFraction); height: parent.height; radius: 4; color: win.overview.memFraction >= 0.9 ? "#E5A65F" : "#559CF2" } }
                Text { x: 12; y: 66; font.pixelSize: 10; color: Theme.inkMuted; text: "Swap : " + win.overview.swapUsed + " / " + win.overview.swapTotal }
            }
            Rectangle {
                width: parent.width; height: 74; radius: 10; color: "#30182740"; border.width: 1; border.color: "#25DCEBFF"
                Text { x: 12; y: 8; text: "ACTIVITÉ"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2 }
                Text { x: 12; y: 26; font.pixelSize: 13; color: Theme.ink
                       text: win.overview.processes + " processus (" + win.overview.running + " en cours, " + win.overview.zombies + " zombie(s))" }
                Text { x: 12; y: 48; font.pixelSize: 12; textFormat: Text.PlainText; color: win.overview.failedServices > 0 ? "#FCA5A5" : Theme.inkMuted
                       text: !win.sys || !win.sys.hasSystemd ? "Pas de systemd" : "systemd : " + win.overview.systemdState + " · " + win.overview.activeServices + " services actifs, " + win.overview.failedServices + " en échec" }
            }
        }
    }

    // ── no connection / explicit errors ────────────────────────────────
    Column {
        visible: !win.connected; x: win.contentLeft + 16; y: 150; width: parent.width - x - 16; spacing: 18
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: "Aucun serveur connecté"; color: Theme.ink; font.pixelSize: 16 }
        Button { objectName: "systemConnectButton"; anchors.horizontalCenter: parent.horizontalCenter; text: "Choisir un serveur…"; onClicked: win.requestServers() }
    }
    Text {
        visible: win.connected && win.sys !== null && (win.sys.state === "error" || win.sys.state === "loading") && win.sys.processes.length === 0
        x: win.contentLeft + 20; y: 110; width: parent.width - x - 20; wrapMode: Text.WordWrap; textFormat: Text.PlainText
        text: win.sys ? win.sys.status : ""; color: win.sys && win.sys.state === "error" ? "#FCA5A5" : Theme.inkMuted; font.pixelSize: 13
    }
    Text {
        x: win.contentLeft + 12; anchors.bottom: parent.bottom; anchors.bottomMargin: 6; width: parent.width - x - 12; elide: Text.ElideRight; textFormat: Text.PlainText
        visible: win.sys !== null && win.sys.notes.length > 0
        text: win.sys ? win.sys.notes.join("  ·  ") : ""; color: Theme.inkMuted; font.pixelSize: 10
    }
}
