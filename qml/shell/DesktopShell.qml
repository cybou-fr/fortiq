import QtCore
import QtQuick

// The master composition at its native 1536×1024 coordinates
// (UI_SPEC_V1 §1). The host scales this canvas uniformly to the window.
// `fixture` is the visual-only reference scene: fixed clock, sample files,
// launcher and control center open. It is never production data.
Item {
    id: shell
    objectName: "desktopShell"
    property bool adaptive: false
    readonly property bool narrow: adaptive && width < 700
    property string foregroundApp: fixture && logsOpen ? "logs" : "files"
    readonly property real appTop: adaptive ? 56 : 0
    readonly property real appHeight: Math.max(44, height - appTop - (Qt.inputMethod.visible ? 8 : 76))
    property var controller: null
    property var logsController: null
    property var terminalController: null
    property bool terminalOpen: false
    property var disksController: null
    property bool disksOpen: false
    property var documentsController:null
    property bool documentsOpen:false
    property bool fixture: false
    property string fixturePreview: ""
    property url wallpaperSource: ""
    // Owner-supplied wallpapers bundled with the shell. Choice is RAM-only;
    // right-click the desktop to cycle. An explicit file overrides it.
    readonly property var wallpapers: ["aurora", "desert", "city"]
    property string wallpaperName: "aurora"
    readonly property url wallpaperUrl: wallpaperSource.toString().length > 0 ? wallpaperSource
        : (wallpaperName.length > 0 ? Qt.resolvedUrl("../../assets/wallpapers/" + wallpaperName + ".jpg") : "")
    property bool launcherOpen: fixture && !adaptive && fixturePreview !== "logs" && fixturePreview !== "incident" && fixturePreview !== "service" && fixturePreview !== "service"
    property bool controlOpen: fixture && !adaptive
    property bool filesOpen: true
    property bool browserOpen: fixture && fixturePreview !== "logs" && fixturePreview !== "incident"
    property bool logsOpen: fixture && (fixturePreview === "logs" || fixturePreview === "incident" || fixturePreview === "service")
    property date now: new Date()
    // One app panel, left or bottom (owner decision 22.10.2026). The choice is
    // a display preference remembered on this device (owner decision); the
    // reference fixture neither reads nor writes it.
    Settings {
        id: displaySettings
        category: "display"
        property string panelEdge: "bottom"
    }
    property string panelEdge: fixture ? (fixturePreview === "panelleft" ? "left" : "bottom")
                                       : (displaySettings.panelEdge === "left" ? "left" : "bottom")
    property bool serversOpen: false
    property var windowOrder: ["files", "browser", "logs", "terminal", "disks", "docs"]
    function raiseApp(app) {
        windowOrder = windowOrder.filter(key => key !== app).concat([app])
        foregroundApp = app
    }

    width: Theme.canvasWidth
    height: Theme.canvasHeight

    readonly property var days: ["Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"]
    readonly property var months: ["janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre"]
    readonly property string timeText: fixture ? "13:45" : Qt.formatTime(now, "hh:mm")
    readonly property string dateText: fixture ? "Mercredi 18 octobre"
        : days[now.getDay()] + " " + now.getDate() + " " + months[now.getMonth()]
    readonly property var shortMonths: ["janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc."]
    readonly property string barClock: fixture ? "Mer. 18 oct.  13:45"
        : days[now.getDay()].slice(0, 3) + ". " + now.getDate() + " " + shortMonths[now.getMonth()] + "  " + timeText

    Timer { running: !shell.fixture; repeat: true; interval: 15000; onTriggered: shell.now = new Date() }

    function launch(app) {
        if (app === "files") { filesOpen = true; raiseApp("files") }
        else if (app === "launcher") launcherOpen = !launcherOpen
        else if (app === "browser") { browserOpen = !browserOpen; if (browserOpen) raiseApp("browser") }
        else if (app === "servers") serversOpen = true
        else if (app === "logs") { raiseApp("logs"); logsOpen = true; launcherOpen = false }
        else if (app === "terminal") { terminalOpen = true; raiseApp("terminal"); launcherOpen = false }
        else if (app === "docs") { documentsOpen=true;raiseApp("docs");launcherOpen=false }
        else if (app === "disks") { disksOpen=true;raiseApp("disks");launcherOpen=false }
        else if (app === "trash") {
            filesOpen = true; raiseApp("files")
            if (controller && controller.connected && controller.trashDirectory.length > 0) controller.navigate(controller.trashDirectory)
            else serversOpen = true
        }
    }

    Wallpaper {
        id: wallpaper
        anchors.fill: parent
        source: shell.wallpaperUrl
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: {
            const i = shell.wallpapers.indexOf(shell.wallpaperName)
            shell.wallpaperName = shell.wallpapers[(i + 1) % shell.wallpapers.length]
        }
    }

    ClockWidget {
        visible: !shell.adaptive
        x: 119; y: 70
        timeText: shell.timeText
        dateText: shell.dateText
        sampleWeather: shell.fixture
    }

    FilesWindow {
        id: filesWindow
        z: shell.windowOrder.indexOf("files") + 1
        visible: shell.filesOpen && (!shell.adaptive || shell.foregroundApp === "files")
        adaptive: shell.adaptive
        x: shell.adaptive ? 8 : 471; y: shell.adaptive ? shell.appTop : 72
        width: shell.adaptive ? shell.width - 16 : 717
        height: shell.adaptive ? shell.appHeight : 403
        backdrop: wallpaper
        controller: shell.controller
        fixture: shell.fixture
        fixturePreview: shell.fixturePreview
        onCloseRequested: { shell.filesOpen = false; if (shell.adaptive && shell.logsOpen) shell.foregroundApp = "logs" }
        onRequestServers: shell.serversOpen = true
        onRequestDocuments:path=>{shell.documentsOpen=true;shell.raiseApp("docs");documentsWindow.request("open",path)}
    }

    BrowserWindow {
        id: browserWindow
        z: shell.windowOrder.indexOf("browser") + 1
        visible: shell.browserOpen && !shell.adaptive
        x: 554; y: 512; width: 650; height: 400
        backdrop: wallpaper
        fixture: shell.fixture
    }

    LogsWindow {
        id: logsWindow
        z: shell.windowOrder.indexOf("logs") + 1
        visible: shell.logsOpen && (!shell.adaptive || shell.foregroundApp === "logs")
        adaptive: shell.adaptive
        x: shell.adaptive ? 8 : 300; y: shell.adaptive ? shell.appTop : 130
        width: shell.adaptive ? shell.width - 16 : 900
        height: shell.adaptive ? shell.appHeight : 640
        backdrop: wallpaper
        logs: shell.logsController
        remote: shell.controller
        fixture: shell.fixture
        fixtureIncident: shell.fixture && shell.fixturePreview === "incident"
        fixtureService: shell.fixture && shell.fixturePreview === "service"
        onCloseRequested: { shell.logsOpen = false; if (shell.adaptive) { shell.filesOpen = true; shell.foregroundApp = "files" } }
        onRequestServers: shell.serversOpen = true
    }

    TerminalWindow {
        id: terminalWindow
        visible: shell.terminalOpen && (!shell.adaptive || shell.foregroundApp === "terminal")
        adaptive: shell.adaptive
        x: shell.adaptive ? 8 : 340; y: shell.adaptive ? shell.appTop : 110
        width: shell.adaptive ? shell.width-16 : 920
        height: shell.adaptive ? shell.appHeight : 600
        z: shell.windowOrder.indexOf("terminal") + 1
        backdrop: wallpaper
        terminal: shell.terminalController
        remote: shell.controller
        onCloseRequested: { shell.terminalOpen=false; if(shell.adaptive){shell.foregroundApp="files";shell.filesOpen=true} }
        onRequestServers: shell.serversOpen=true
    }

    DisksWindow {
        id:disksWindow
        visible:shell.disksOpen && (!shell.adaptive || shell.foregroundApp==="disks")
        adaptive:shell.adaptive
        x:shell.adaptive?8:300;y:shell.adaptive?shell.appTop:100
        width:shell.adaptive?shell.width-16:920;height:shell.adaptive?shell.appHeight:640
        z:shell.windowOrder.indexOf("disks")+1
        backdrop:wallpaper;disks:shell.disksController;remote:shell.controller
        onCloseRequested:{shell.disksOpen=false;if(shell.adaptive){shell.foregroundApp="files";shell.filesOpen=true}}
        onRequestServers:shell.serversOpen=true
        onOpenPath:path=>{if(shell.controller && shell.controller.connected){shell.controller.navigate(path);shell.filesOpen=true;shell.raiseApp("files")}}
    }

    DocumentsWindow {
        id:documentsWindow
        visible:shell.documentsOpen && (!shell.adaptive || shell.foregroundApp==="docs")
        adaptive:shell.adaptive
        x:shell.adaptive?8:280;y:shell.adaptive?shell.appTop:90
        width:shell.adaptive?shell.width-16:1020;height:shell.adaptive?shell.appHeight:680
        z:shell.windowOrder.indexOf("docs")+1
        backdrop:wallpaper;documents:shell.documentsController;remote:shell.controller
        onCloseRequested:{shell.documentsOpen=false;if(shell.adaptive){shell.foregroundApp="files";shell.filesOpen=true}}
        onRequestServers:shell.serversOpen=true
    }
    function allowAppClose(){
        if(documentsController && (documentsController.dirty || documentsController.busy)){
            documentsOpen=true;raiseApp("docs");return documentsWindow.allowAppClose()
        }
        return true
    }

    // Observe a press above app windows, then reject it so the same event
    // continues to the actual button, field or drag handle underneath.
    MouseArea {
        anchors.fill: parent
        z: 9
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onPressed: mouse => {
            const windows = [{key: "files", item: filesWindow}, {key: "browser", item: browserWindow}, {key: "logs", item: logsWindow}, {key: "terminal", item: terminalWindow}, {key:"disks",item:disksWindow}, {key:"docs",item:documentsWindow}]
            windows.sort((a, b) => b.item.z - a.item.z)
            for (const entry of windows) {
                const p = entry.item.mapFromItem(shell, mouse.x, mouse.y)
                if (entry.item.visible && entry.item.contains(p)) { shell.raiseApp(entry.key); break }
            }
            mouse.accepted = false
        }
    }

    LauncherPanel {
        z: 10
        objectName: "launcherPanel"
        visible: shell.launcherOpen
        x: shell.adaptive ? 8 : (shell.fixture ? 98 : (shell.panelEdge === "left" ? appPanel.x + appPanel.width + 10 : appPanel.x))
        y: shell.adaptive ? Math.max(56, shell.height - height - 80) : (shell.fixture ? 497 : (shell.panelEdge === "left" ? Math.max(59, shell.height - height - 80) : appPanel.y - height - 10))
        width: shell.adaptive ? Math.min(438, shell.width - 16) : 438; height: shell.adaptive ? Math.min(443, shell.height - 140) : 443
        backdrop: wallpaper
        fixture: shell.fixture
        onLaunch: (app) => shell.launch(app)
    }

    ControlCenter {
        objectName: "controlCenter"
        z: 10
        visible: shell.controlOpen
        docked: !shell.fixture && !shell.adaptive
        x: shell.adaptive ? shell.width - width - 8 : (docked ? shell.width - width : 1215)
        y: docked ? 47 : 64
        width: Math.min(310, shell.width - 16)
        height: shell.adaptive ? Math.min(679, shell.height - 144) : (docked ? shell.height - 47 : 679)
        backdrop: wallpaper
        fixture: shell.fixture
        wallpapers: shell.wallpapers
        wallpaperName: shell.wallpaperName
        onWallpaperSelected: name => { shell.wallpaperSource = ""; shell.wallpaperName = name }
    }

    // Reference workspace cards: visual fixture only. Production wallpaper
    // choice is part of the control center panel.
    WorkspaceSwitcher {
        z: 10
        objectName: "workspaceSwitcher"
        visible: !shell.adaptive && shell.fixture
        x: 1215; y: 765; width: 310; height: 129
        backdrop: wallpaper
        current: 1
    }

    AppPanel {
        id: appPanel
        fixture:shell.fixture
        z: 10
        visible: !shell.adaptive
        backdrop: wallpaper
        edge: shell.panelEdge
        x: edge === "left" ? 4 : (parent.width - width) / 2
        y: edge === "left" ? 59 : parent.height - height - 8
        width: edge === "left" ? 84 : (shell.fixture?560:Math.min(parent.width-16,appPanel.apps.length*60+84))
        height: edge === "left" ? parent.height - 59 - 8 : 70
        running: ({ files: shell.filesOpen, logs: shell.logsOpen, terminal: shell.terminalOpen, disks:shell.disksOpen, docs:shell.documentsOpen, launcher: shell.launcherOpen })
        onLaunch: (app) => shell.launch(app)
        onMoveTo: (edge) => {
            shell.panelEdge = edge
            if (!shell.fixture) displaySettings.panelEdge = edge
        }
    }

    CompactAppPanel {
        z: 10
        apps: appPanel.apps
        visible: shell.adaptive && !Qt.inputMethod.visible
        x: 8; y: parent.height - height - 4; width: parent.width - 16; height: 68
        currentApp: shell.foregroundApp
        onLaunch: app => shell.launch(app)
    }

    TopBar {
        z: 10
        compact: shell.adaptive
        width: parent.width
        serverLabel: shell.fixture ? "Linux"
            : (shell.controller && shell.controller.connected ? shell.controller.serverLabel : "Aucun serveur")
        clockText: shell.barClock
        connected: shell.controller !== null && shell.controller.connected
        onServerClicked: if (!shell.fixture) shell.serversOpen = true
        MouseArea {
            x: shell.adaptive ? parent.width - 44 : 1100; width: shell.adaptive ? 44 : 380; height: parent.height
            onClicked: shell.controlOpen = !shell.controlOpen
        }
    }

    ServerManager {
        id: serverManager
        adaptive: shell.adaptive
        anchors.fill: parent
        z: 100
        visible: shell.serversOpen
        backdrop: wallpaper
        controller: shell.controller
        onClosed: shell.serversOpen = false
    }
    states: State {
        name: "adaptive"; when: shell.adaptive
        PropertyChanges { target: filesWindow; x: 8; y: shell.appTop; width: shell.width - 16; height: shell.appHeight }
        PropertyChanges { target: logsWindow; x: 8; y: shell.appTop; width: shell.width - 16; height: shell.appHeight }
        PropertyChanges { target: terminalWindow; x: 8; y: shell.appTop; width: shell.width - 16; height: shell.appHeight }
    }
    focus: true
    function handleBack() {
        if (Qt.inputMethod.visible) { Qt.inputMethod.hide(); return true }
        if (serversOpen) { serverManager.back(); return true }
        if (launcherOpen || controlOpen) { launcherOpen = false; controlOpen = false; return true }
        if (foregroundApp === "files" && filesWindow.handleBack()) return true
        if(foregroundApp==="docs")return documentsWindow.handleBack()
        if (foregroundApp === "disks") {
            if(disksWindow.handleBack())return true
            foregroundApp="files";filesOpen=true;return true
        }
        if (foregroundApp === "terminal") {
            if(terminalWindow.handleBack())return true
            foregroundApp="files";filesOpen=true;return true
        }
        if (foregroundApp === "logs") {
            if (logsWindow.handleBack()) return true
            foregroundApp = "files"; filesOpen = true; return true
        }
        return false
    }
    Keys.onReleased: event => {
        if ((shell.adaptive || Qt.platform.os === "android") && (event.key === Qt.Key_Back || event.key === Qt.Key_Escape)) event.accepted = shell.handleBack()
    }
    Component.onCompleted: if (!fixture && controller && !controller.connected) serversOpen = true
}
