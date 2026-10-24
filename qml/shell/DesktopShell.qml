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
    property var taskManager: null      // the application's TaskManager (transfers, operations, saves, searches)
    property var logsController: null
    property var terminalController: null
    property bool terminalOpen: false
    property var disksController: null
    property var browserController: null
    property bool webOpen: false
    property var networkController: null
    property bool networkOpen: false
    property var systemController: null
    property bool systemOpen: false
    property var dbController: null
    property bool dbOpen: false
    property var mailController: null
    property bool mailOpen: false
    property var licenseController: null    // product licence (LicenseController); null in builds without it
    property bool licenseOpen: false
    readonly property bool licenseLocked: !fixture && licenseController !== null && licenseController.required && !licenseController.entitled
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
    property bool filesOpen: false   // the reference scene opens it on completion; production starts without windows
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
    // The login screen (ServerManager) is shown while there is no session:
    // at start, after "Se déconnecter", or when an app asks for a server.
    property bool serversOpen: false
    property bool sessionMenuOpen: false
    // Ends the session like a Linux logout: unsaved MDok drafts are protected,
    // windows close, the SSH connection ends and the login screen returns.
    function logout() {
        if (!allowDocumentsClose()) return
        confirmSessionEnd("Se déconnecter", function() { endSession() })
    }
    // Every way of ending the session (logout, disconnect link, another server, quitting) asks first when the TaskManager
    // reports tasks that guard it; `proceed` runs at once when there are none.
    function confirmSessionEnd(label, proceed) { sessionGuard.ask(label, proceed) }
    function endSession() {
        sessionMenuOpen = false; launcherOpen = false; controlOpen = false
        filesOpen = false; logsOpen = false; terminalOpen = false; disksOpen = false; documentsOpen = false; webOpen = false; networkOpen = false; systemOpen = false; dbOpen = false; mailOpen = false
        if (controller) controller.disconnectServer()
        serversOpen = true
    }
    Connections {
        target: shell.controller
        function onChanged() { if (shell.controller.connected && shell.serversOpen && !shell.fixture) shell.serversOpen = false }
    }
    property var windowOrder: ["files", "browser", "logs", "terminal", "disks", "docs", "web", "network", "system", "db", "mail"]
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

    // ---- Moves between apps (audit backlog 8, R12) --------------------------------------------------------------
    // An action carries the server it was made for. It runs only while that server is the connected one: a stale or
    // late action never lands on another server, and the active server is never substituted for the one named.
    property string lastRoute: ""
    function notify(message) { routeToast.show(message) }
    function shQuote(value) { return "'" + String(value).replace(/'/g, "'\\''") + "'" }
    function route(a) {
        if (!controller || !controller.connected || !a.serverId || a.serverId !== controller.activeServerId) {
            notify("Action ignorée : elle ne concerne pas le serveur actuellement connecté.")
            return false
        }
        switch (a.type) {
        case "openDocument": documentsOpen = true; raiseApp("docs"); launcherOpen = false; documentsWindow.request("open", a.path); break
        case "revealInFiles": controller.navigate(a.path); filesOpen = true; raiseApp("files"); launcherOpen = false; break
        case "showLogs": logsOpen = true; raiseApp("logs"); launcherOpen = false; logsWindow.showUnit(a.unit); break
        case "showService": systemOpen = true; raiseApp("system"); launcherOpen = false; systemWindow.showService(a.unit); break
        case "terminalHere": terminalOpen = true; raiseApp("terminal"); launcherOpen = false; terminalWindow.proposeCommand("cd -- " + shQuote(a.path)); break
        default: notify("Action inconnue : " + a.type); return false
        }
        lastRoute = a.type + "@" + a.serverId
        return true
    }
    // The action of a window: stamped with the connected server at the moment of the click.
    function act(type, fields) {
        return route(Object.assign({ type: type, serverId: controller ? controller.activeServerId : "" }, fields))
    }
    function copyText(what, text) {
        shellClip.text = text; shellClip.selectAll(); shellClip.copy(); shellClip.deselect(); shellClip.text = ""
        notify("Copié : " + what)
    }
    TextEdit { id: shellClip; visible: false }

    function launch(app) {
        if (app === "files") { filesOpen = true; raiseApp("files") }
        else if (app === "launcher") launcherOpen = !launcherOpen
        else if (app === "browser" && !fixture) { webOpen = true; raiseApp("web"); launcherOpen = false }
        else if (app === "browser") { browserOpen = !browserOpen; if (browserOpen) raiseApp("browser") }
        else if (app === "servers") serversOpen = true
        else if (app === "logs") { raiseApp("logs"); logsOpen = true; launcherOpen = false }
        else if (app === "terminal") { terminalOpen = true; raiseApp("terminal"); launcherOpen = false }
        else if (app === "docs") { documentsOpen=true;raiseApp("docs");launcherOpen=false }
        else if (app === "disks") { disksOpen=true;raiseApp("disks");launcherOpen=false }
        else if (app === "network") { networkOpen = true; raiseApp("network"); launcherOpen = false }
        else if (app === "system") { systemOpen = true; raiseApp("system"); launcherOpen = false }
        else if (app === "db") { dbOpen = true; raiseApp("db"); launcherOpen = false }
        else if (app === "mail") { mailOpen = true; raiseApp("mail"); launcherOpen = false }
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
        onRequestDisconnect: shell.confirmSessionEnd("Se déconnecter", function() { shell.controller.disconnectServer() })
        onRequestDocuments:path=>shell.act("openDocument", { path: path })
        onRequestTerminal: path => shell.act("terminalHere", { path: path })
    }

    BrowserWindow {
        id: browserWindow
        z: shell.windowOrder.indexOf("browser") + 1
        visible: shell.fixture && shell.browserOpen && !shell.adaptive
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
        onRequestService: unit => shell.act("showService", { unit: unit })
        onRequestCopy: (what, text) => shell.copyText(what, text)
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

    WebWindow {
        id: webWindow
        visible: shell.webOpen && (!shell.adaptive || shell.foregroundApp === "web")
        adaptive: shell.adaptive
        x: shell.adaptive ? 8 : 320; y: shell.adaptive ? shell.appTop : 86
        width: shell.adaptive ? shell.width - 16 : 940; height: shell.adaptive ? shell.appHeight : 660
        z: shell.windowOrder.indexOf("web") + 1
        backdrop: wallpaper; browser: shell.browserController; remote: shell.controller
        onCloseRequested: { shell.webOpen = false; if (shell.adaptive) { shell.foregroundApp = "files"; shell.filesOpen = true } }
        onRequestServers: shell.serversOpen = true
    }

    MailWindow {
        id: mailWindow
        visible: shell.mailOpen && (!shell.adaptive || shell.foregroundApp === "mail")
        adaptive: shell.adaptive
        x: shell.adaptive ? 8 : 300; y: shell.adaptive ? shell.appTop : 80
        width: shell.adaptive ? shell.width - 16 : 1000; height: shell.adaptive ? shell.appHeight : 670
        z: shell.windowOrder.indexOf("mail") + 1
        backdrop: wallpaper; mailer: shell.mailController; remote: shell.controller
        onCloseRequested: { shell.mailOpen = false; if (shell.adaptive) { shell.foregroundApp = "files"; shell.filesOpen = true } }
        onRequestServers: shell.serversOpen = true
    }

    DbWindow {
        id: dbWindow
        visible: shell.dbOpen && (!shell.adaptive || shell.foregroundApp === "db")
        adaptive: shell.adaptive
        x: shell.adaptive ? 8 : 350; y: shell.adaptive ? shell.appTop : 104
        width: shell.adaptive ? shell.width - 16 : 960; height: shell.adaptive ? shell.appHeight : 650
        z: shell.windowOrder.indexOf("db") + 1
        backdrop: wallpaper; db: shell.dbController; remote: shell.controller
        onCloseRequested: { shell.dbOpen = false; if (shell.adaptive) { shell.foregroundApp = "files"; shell.filesOpen = true } }
        onRequestServers: shell.serversOpen = true
        onRequestCopy: (what, text) => shell.copyText(what, text)
    }

    SystemWindow {
        id: systemWindow
        visible: shell.systemOpen && (!shell.adaptive || shell.foregroundApp === "system")
        adaptive: shell.adaptive
        x: shell.adaptive ? 8 : 340; y: shell.adaptive ? shell.appTop : 98
        width: shell.adaptive ? shell.width - 16 : 940; height: shell.adaptive ? shell.appHeight : 650
        z: shell.windowOrder.indexOf("system") + 1
        backdrop: wallpaper; sys: shell.systemController; remote: shell.controller
        onCloseRequested: { shell.systemOpen = false; if (shell.adaptive) { shell.foregroundApp = "files"; shell.filesOpen = true } }
        onRequestServers: shell.serversOpen = true
        onRequestLogs: unit => shell.act("showLogs", { unit: unit })
        onRequestCopy: (what, text) => shell.copyText(what, text)
    }

    NetworkWindow {
        id: networkWindow
        visible: shell.networkOpen && (!shell.adaptive || shell.foregroundApp === "network")
        adaptive: shell.adaptive
        x: shell.adaptive ? 8 : 330; y: shell.adaptive ? shell.appTop : 92
        width: shell.adaptive ? shell.width - 16 : 920; height: shell.adaptive ? shell.appHeight : 640
        z: shell.windowOrder.indexOf("network") + 1
        backdrop: wallpaper; network: shell.networkController; remote: shell.controller
        onCloseRequested: { shell.networkOpen = false; if (shell.adaptive) { shell.foregroundApp = "files"; shell.filesOpen = true } }
        onRequestServers: shell.serversOpen = true
        onRequestCopy: (what, text) => shell.copyText(what, text)
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
        onOpenPath:path=>shell.act("revealInFiles", { path: path })
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
    function allowDocumentsClose(){
        if(documentsController && (documentsController.dirty || documentsController.busy)){
            documentsOpen=true;raiseApp("docs");return documentsWindow.allowAppClose()
        }
        return true
    }
    // Closing the window: unsaved MDok drafts first, then running tasks (asked once, then the app quits).
    function allowAppClose(){
        if (!allowDocumentsClose()) return false
        if (taskManager && taskManager.guardingCount() > 0) { sessionGuard.ask("Quitter", function() { Qt.quit() }); return false }
        return true
    }

    // Observe a press above app windows, then reject it so the same event
    // continues to the actual button, field or drag handle underneath.
    MouseArea {
        anchors.fill: parent
        z: 9
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onPressed: mouse => {
            const windows = [{key: "files", item: filesWindow}, {key: "browser", item: browserWindow}, {key: "logs", item: logsWindow}, {key: "terminal", item: terminalWindow}, {key:"disks",item:disksWindow}, {key:"docs",item:documentsWindow}, {key:"web",item:webWindow}, {key:"network",item:networkWindow}, {key:"system",item:systemWindow}, {key:"db",item:dbWindow}, {key:"mail",item:mailWindow}]
            windows.sort((a, b) => b.item.z - a.item.z)
            for (const entry of windows) {
                const p = entry.item.mapFromItem(shell, mouse.x, mouse.y)
                if (entry.item.visible && entry.item.contains(p)) { shell.raiseApp(entry.key); break }
            }
            // A press anywhere outside a floating panel closes it. The app
            // panel (launcher button) and the top-bar toggle accept their own
            // presses, so they never get here. The reference scene stays open.
            if (!shell.fixture && !shell.serversOpen) {
                if (shell.controlOpen && !shell.pressInside(controlPanel, mouse.x, mouse.y)) shell.controlOpen = false
                if (shell.sessionMenuOpen && !shell.pressInside(sessionMenu, mouse.x, mouse.y)) shell.sessionMenuOpen = false
                if (shell.launcherOpen && !shell.pressInside(launcherItem, mouse.x, mouse.y)
                    && !shell.pressInside(appPanel, mouse.x, mouse.y) && !shell.pressInside(compactPanel, mouse.x, mouse.y))
                    shell.launcherOpen = false
            }
            mouse.accepted = false
        }
    }
    function pressInside(item, x, y) {
        if (!item || !item.visible) return false
        return item.contains(item.mapFromItem(shell, x, y))
    }

    LauncherPanel {
        id: launcherItem
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
        id: controlPanel
        objectName: "controlCenter"
        z: 10
        visible: shell.controlOpen
        docked: !shell.fixture && !shell.adaptive
        x: shell.adaptive ? shell.width - width - 8 : (docked ? shell.width - width - 8 : 1215)
        y: docked ? 59 : 64
        width: Math.min(310, shell.width - 16)
        height: shell.adaptive ? Math.min(679, shell.height - 144) : (docked ? Math.min(contentHeight, shell.height - 59 - 8) : 679)
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
        objectName: "appPanel"
        fixture:shell.fixture
        z: 10
        visible: !shell.adaptive
        backdrop: wallpaper
        edge: shell.panelEdge
        x: edge === "left" ? 4 : (parent.width - width) / 2
        y: edge === "left" ? 59 : parent.height - height - 8
        width: edge === "left" ? 84 : (shell.fixture?560:Math.min(parent.width-16,appPanel.apps.length*60+84))
        height: edge === "left" ? parent.height - 59 - 8 : 70
        running: ({ files: shell.filesOpen, logs: shell.logsOpen, terminal: shell.terminalOpen, disks:shell.disksOpen, docs:shell.documentsOpen, browser: shell.fixture ? shell.browserOpen : shell.webOpen, network: shell.networkOpen, system: shell.systemOpen, db: shell.dbOpen, mail: shell.mailOpen, launcher: shell.launcherOpen })
        onLaunch: (app) => shell.launch(app)
        onMoveTo: (edge) => {
            shell.panelEdge = edge
            if (!shell.fixture) displaySettings.panelEdge = edge
        }
    }

    CompactAppPanel {
        id: compactPanel
        z: 10
        apps: appPanel.apps
        visible: shell.adaptive && !Qt.inputMethod.visible
        x: 8; y: parent.height - height - 4; width: parent.width - 16; height: 68
        currentApp: shell.foregroundApp
        onLaunch: app => shell.launch(app)
    }

    TopBar {
        id: topBar
        z: 10
        compact: shell.adaptive
        pillRight: !shell.fixture
        width: parent.width
        serverLabel: shell.fixture ? "Linux"
            : (shell.controller && shell.controller.connected ? shell.controller.serverLabel : "Aucun serveur")
        clockText: shell.barClock
        connected: shell.controller !== null && shell.controller.connected
        linkLost: shell.controller !== null && shell.controller.connected && shell.controller.linkLost === true
        onServerClicked: {
            if (shell.fixture) return
            if (shell.controller && shell.controller.connected) shell.sessionMenuOpen = !shell.sessionMenuOpen
            else shell.serversOpen = true
        }
        MouseArea {
            x: shell.adaptive ? parent.width - 44 : 1100; width: shell.adaptive ? 44 : Math.max(0, topBar.indicatorLeft - 1100); height: parent.height
            onClicked: shell.controlOpen = !shell.controlOpen
        }
    }

    // Session menu under the server name in the top bar.
    Glass {
        id: sessionMenu
        objectName: "sessionMenu"
        z: 11
        visible: shell.sessionMenuOpen && !shell.fixture
        x: shell.width - width - 12; y: 52
        width: Math.min(280, shell.width - 16); height: licenseController ? 140 : 96
        backdrop: wallpaper
        radius: 14
        tint: "#E6101A2E"
        Rectangle { x: 16; y: 20; width: 9; height: 9; radius: 4.5; color: "#4ADE80" }
        Text {
            x: 34; y: 14; width: parent.width - 50; elide: Text.ElideRight
            text: shell.controller ? "Connecté à " + shell.controller.serverLabel : ""
            color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily
        }
        Rectangle {
            objectName: "logoutButton"
            x: 12; y: 46; width: parent.width - 24; height: 36; radius: 9
            color: lm.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
            Text { anchors.centerIn: parent; text: "Se déconnecter"; color: "white"; font.pixelSize: 13; font.family: Theme.fontFamily }
            MouseArea { id: lm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: shell.logout() }
        }
        Rectangle {
            objectName: "licenseButton"
            visible: shell.licenseController !== null
            x: 12; y: 90; width: parent.width - 24; height: 36; radius: 9
            color: lcm.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
            Text { anchors.centerIn: parent; text: "Licence"; color: "white"; font.pixelSize: 13; font.family: Theme.fontFamily }
            MouseArea { id: lcm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { shell.sessionMenuOpen = false; shell.licenseOpen = true } }
        }
    }

    FixtureTasks { id: fixtureTasks }
    TaskPanel {
        id: taskPanel
        objectName: "taskPanel"
        z: 12
        visible: !shell.serversOpen && (shell.fixture ? shell.fixturePreview === "transfers"
                                                      : (shell.taskManager !== null && shell.taskManager.count > 0))
        manager: shell.fixture ? fixtureTasks : shell.taskManager
        controller: shell.controller
        width: Math.min(420, shell.width - 24)
        x: shell.width - width - 12
        y: shell.height - height - (shell.adaptive ? 84 : 12)
    }

    ServerManager {
        id: serverManager
        confirmEnd: shell.confirmSessionEnd
        adaptive: shell.adaptive
        anchors.fill: parent
        z: 100
        visible: shell.serversOpen
        backdrop: wallpaper
        wallpaperSource: shell.wallpaperUrl
        timeText: shell.timeText
        dateText: shell.dateText
        controller: shell.controller
    }
    states: State {
        name: "adaptive"; when: shell.adaptive
        PropertyChanges { target: filesWindow; x: 8; y: shell.appTop; width: shell.width - 16; height: shell.appHeight }
        PropertyChanges { target: logsWindow; x: 8; y: shell.appTop; width: shell.width - 16; height: shell.appHeight }
        PropertyChanges { target: terminalWindow; x: 8; y: shell.appTop; width: shell.width - 16; height: shell.appHeight }
    }
    LicenseScreen {
        id: licenseScreen
        anchors.fill: parent
        z: 150
        visible: !shell.fixture && shell.licenseController !== null && (shell.licenseOpen || shell.licenseLocked)
        license: shell.licenseController
        adaptive: shell.adaptive
        onCloseRequested: shell.licenseOpen = false
    }
    Toast { id: routeToast; adaptive: shell.adaptive }

    SessionGuard { id: sessionGuard; anchors.fill: parent; manager: shell.taskManager }
    focus: true
    function handleBack() {
        if (sessionGuard.visible) { sessionGuard.stay(); return true }
        if (licenseScreen.visible) { if (!licenseLocked) licenseOpen = false; return true }
        if (Qt.inputMethod.visible) { Qt.inputMethod.hide(); return true }
        if (serversOpen) { serverManager.back(); return true }
        if (launcherOpen || controlOpen) { launcherOpen = false; controlOpen = false; return true }
        if (foregroundApp === "files" && filesWindow.handleBack()) return true
        if(foregroundApp==="docs")return documentsWindow.handleBack()
        if (foregroundApp === "disks") {
            if(disksWindow.handleBack())return true
            foregroundApp="files";filesOpen=true;return true
        }
        if (foregroundApp === "mail") {
            if (mailWindow.handleBack()) return true
            foregroundApp = "files"; filesOpen = true; return true
        }
        if (foregroundApp === "db") {
            if (dbWindow.handleBack()) return true
            foregroundApp = "files"; filesOpen = true; return true
        }
        if (foregroundApp === "system") {
            if (systemWindow.handleBack()) return true
            foregroundApp = "files"; filesOpen = true; return true
        }
        if (foregroundApp === "network") {
            if (networkWindow.handleBack()) return true
            foregroundApp = "files"; filesOpen = true; return true
        }
        if (foregroundApp === "web") {
            if (webWindow.handleBack()) return true
            foregroundApp = "files"; filesOpen = true; return true
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
    Component.onCompleted: {
        if (fixture) filesOpen = true
        else serversOpen = true
    }
}
