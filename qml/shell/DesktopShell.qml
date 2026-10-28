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
    // Browser engine (Chromium, docs/BROWSER.md): the default Browser when this build has it; the text reader (curl on the server) is
    // the fallback the user can pick ("Lecteur simple") and the only Browser of builds without Qt WebEngine.
    property var engineBrowser: null
    property bool webText: false
    readonly property bool engineOn: engineBrowser !== null && engineBrowser.available && !webText
    property var engineWindowItem: null
    onWebOpenChanged: if (webOpen) ensureEngineWindow()
    onEngineOnChanged: if (engineOn && webOpen) ensureEngineWindow()
    function ensureEngineWindow() {
        if (!engineOn || engineWindowItem) return
        const comp = Qt.createComponent("Fortiq.Engine", "EngineWindow")
        if (comp.status !== Component.Ready) { console.warn("Browser engine window unavailable: " + comp.errorString()); webText = true; return }
        const w = comp.createObject(shell, { backdrop: wallpaper, engine: shell.engineBrowser, legacy: shell.browserController, remote: shell.controller })
        w.x = Qt.binding(() => shell.adaptive ? 8 : 320)
        w.y = Qt.binding(() => shell.adaptive ? shell.appTop : 86)
        w.width = Qt.binding(() => shell.adaptive ? shell.width - 16 : 940)
        w.height = Qt.binding(() => shell.adaptive ? shell.appHeight : 660)
        w.z = Qt.binding(() => shell.windowOrder.indexOf("web") + 1)
        w.adaptive = Qt.binding(() => shell.adaptive)
        w.visible = Qt.binding(() => shell.webOpen && shell.engineOn && (!shell.adaptive || shell.foregroundApp === "web"))
        w.closeRequested.connect(() => { shell.webOpen = false; shell.focusAfterClose("web"); if (shell.adaptive && shell.foregroundApp === "files") shell.filesOpen = true })
        w.requestServers.connect(() => { shell.serversOpen = true })
        w.requestTextMode.connect(() => { shell.webText = true })
        engineWindowItem = w
    }
    property var networkController: null
    property bool networkOpen: false
    property var systemController: null
    property bool systemOpen: false
    property var dbController: null
    property bool dbOpen: false
    property var mailController: null
    property bool mailOpen: false
    property bool settingsOpen: false
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
        property string wallpaper: "unset"      // "unset" | "none" | a bundled wallpaper name
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
        confirmSessionEnd("Se déconnecter", function() { endSession() })
    }
    // Every way of ending the session (logout, disconnect link, another server, quitting) asks first when the TaskManager
    // reports tasks that guard it; `proceed` runs at once when there are none.
    function confirmSessionEnd(label, proceed) {
        const managerWasOpen = serversOpen
        const guarded = function() { if(managerWasOpen)shell.serversOpen=true;sessionGuard.ask(label, proceed) }
        if(documentsController && documentsController.hasBusyTabs()){
            // Never disconnect while an MDok tab has an in-flight SSH write.
            serversOpen=false;documentsOpen=true;raiseApp("docs")
            pushNotification("warning", "MDok", "Un document est en cours de traitement. Réessayez après la fin de l'opération.")
            return
        }
        if(documentsController && documentsController.hasDirtyTabs()){
            serversOpen=false;documentsOpen=true;raiseApp("docs");documentsWindow.requestAllDocumentsReady(function(){
                // Re-check: a different tab may have started writing during confirmation.
                if(documentsController.hasBusyTabs() || documentsController.hasDirtyTabs())return
                guarded()
            })
        } else guarded()
    }
    function endSession() {
        sessionMenuOpen = false; launcherOpen = false; controlOpen = false
        filesOpen = false; logsOpen = false; terminalOpen = false; disksOpen = false; documentsOpen = false; webOpen = false; networkOpen = false; systemOpen = false; dbOpen = false; mailOpen = false
        // Settings and the fixture browser are windows too. Leaving either
        // open can expose a stale panel above the next server login.
        settingsOpen = false; browserOpen = false
        foregroundApp = "files"
        notificationsOpen = false
        if (controller) controller.disconnectServer()
        serversOpen = true
    }
    // Notifications (RAM only, newest first): what happened while the user was elsewhere. Nothing is stored on disk or sent anywhere.
    property var notifications: []
    property bool notificationsOpen: false
    readonly property int unreadCount: notifications.filter(n => n.unread).length
    property bool lastLinkLost: false
    function pushNotification(kind, title, text) {
        const entry = { id: Date.now() + Math.random(), time: Date.now(), kind: kind, title: title, text: text, unread: !notificationsOpen }
        notifications = [entry].concat(notifications).slice(0, 50)
    }
    function toggleNotifications() {
        notificationsOpen = !notificationsOpen
        if (notificationsOpen) { sessionMenuOpen = false; controlOpen = false; launcherOpen = false; notifications = notifications.map(n => Object.assign({}, n, { unread: false })) }
    }
    function clearNotifications() { notifications = [] }
    function notificationTime(ms) { return Qt.formatTime(new Date(ms), "HH:mm") }
    function taskFinished(id, state) {
        if (!taskManager || state === "cancelled") return
        const t = taskManager.task(id)
        if (!t || !t.title) return
        if (t.transient && state === "succeeded") return      // quiet reads leave no trace
        const where = t.source ? " · " + t.source : ""
        if (state === "succeeded") pushNotification("ok", "Terminé : " + t.title, (t.message || "") + where)
        else if (state === "failed") pushNotification("error", "Échec : " + t.title, (t.message || "") + where)
        else pushNotification("warn", "Résultat incertain : " + t.title, (t.message || "") + where)
    }
    function trackLink(source) {
        const c = source === undefined ? controller : source
        if (!c) return
        const lost = c.connected && c.linkLost === true
        if (lost === lastLinkLost) return
        lastLinkLost = lost
        if (lost) pushNotification("error", "Connexion perdue", c.serverLabel + " — les données affichées datent d’avant la coupure.")
        else if (c.connected) pushNotification("ok", "Connexion rétablie", c.serverLabel)
    }
    Connections {
        target: shell.controller
        ignoreUnknownSignals: true
        function onLinkChanged() { shell.trackLink() }
    }
    Connections {
        target: shell.taskManager
        ignoreUnknownSignals: true
        function onFinished(id, state) { shell.taskFinished(id, state) }
    }
    Connections {
        target: shell.controller
        function onChanged() { if (shell.controller.connected && shell.serversOpen && !shell.fixture) shell.serversOpen = false }
    }
    property var windowOrder: ["files", "browser", "logs", "terminal", "disks", "docs", "web", "network", "system", "db", "mail"]
    // Wallpaper choice is a display preference of this device (the reference scene neither reads nor writes it).
    function setWallpaper(name) {
        wallpaperName = name
        if (!fixture) displaySettings.wallpaper = name.length > 0 ? name : "none"
    }
    function restoreWallpaper() {
        if (fixture || wallpaperSource.toString().length > 0) return
        const saved = displaySettings.wallpaper
        if (saved === "none") wallpaperName = ""
        else if (wallpapers.indexOf(saved) >= 0) wallpaperName = saved
    }

    // Global search of the top bar: applications and settings, accent-insensitive.
    readonly property var searchEntries: [
        { t: "Fichiers", k: "files", d: "Application" }, { t: "Journaux", k: "logs", d: "Application" }, { t: "Terminal", k: "terminal", d: "Application" },
        { t: "Navigateur", k: "browser", d: "Application" }, { t: "Réseau", k: "network", d: "Application" }, { t: "Système", k: "system", d: "Application" },
        { t: "Bases de données", k: "db", d: "Application" }, { t: "Courrier", k: "mail", d: "Application" }, { t: "Disques", k: "disks", d: "Application" },
        { t: "MDok (documents)", k: "docs", d: "Application" }, { t: "Corbeille", k: "trash", d: "Fichiers" },
        { t: "Paramètres", k: "settings", d: "Application" }, { t: "Fond d’écran", k: "settings", d: "Paramètre", section: "appearance" },
        { t: "Position de la barre des applications", k: "settings", d: "Paramètre", section: "appearance" },
        { t: "Serveurs", k: "servers", d: "Paramètre" }, { t: "À propos de FORTIQ", k: "settings", d: "Paramètre", section: "about" }
    ]
    function normalizeText(s) { return s.normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase() }
    function searchResults(query) {
        const q = normalizeText(query.trim())
        if (q.length === 0) return []
        return searchEntries.filter(e => normalizeText(e.t).indexOf(q) >= 0)
    }
    function openSearchResult(entry) {
        if (entry.section) settingsWindow.section = entry.section
        launch(entry.k)
    }

    function raiseApp(app) {
        windowOrder = windowOrder.filter(key => key !== app).concat([app])
        foregroundApp = app
    }
    function focusAfterClose(closed) {
        if (foregroundApp !== closed) return
        const opened = {
            files: filesOpen, browser: browserOpen, logs: logsOpen,
            terminal: terminalOpen, disks: disksOpen, docs: documentsOpen,
            web: webOpen, network: networkOpen, system: systemOpen,
            db: dbOpen, mail: mailOpen, settings: settingsOpen
        }
        for (let i = windowOrder.length - 1; i >= 0; --i) {
            const key = windowOrder[i]
            if (key !== closed && opened[key]) {
                foregroundApp = key
                return
            }
        }
        // Keep a valid foreground identity when the desktop has no windows.
        foregroundApp = "files"
    }
    // On a close, focus may still refer to an editor or button belonging to
    // the hidden window. Return keyboard focus to the remaining window's
    // focus scope rather than routing keystrokes to an invisible control.
    function restoreWindowFocus() {
        // Never steal input from a server/login screen, a session guard,
        // or a global overlay activated during the deferred handoff.
        if (serversOpen || sessionGuard.visible || launcherOpen
            || controlOpen || sessionMenuOpen || notificationsOpen
            || (!adaptive && topBar.searchText.length > 0)) return
        const items = {
            files: filesWindow, browser: browserWindow, logs: logsWindow,
            terminal: terminalWindow, disks: disksWindow, docs: documentsWindow,
            web: engineOn && engineWindowItem ? engineWindowItem : webWindow,
            network: networkWindow, system: systemWindow, db: dbWindow,
            mail: mailWindow, settings: settingsWindow
        }
        const item = items[foregroundApp]
        if (item && item.visible) item.forceActiveFocus()
        else shell.forceActiveFocus()
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
        case "pickMailAttachment":
            if(a.generation!==controller.connectionGeneration){notify("La session du brouillon a changé.");return false}
            filesOpen=true;raiseApp("files");launcherOpen=false;break
        case "attachToMail":
            mailOpen=true;raiseApp("mail");launcherOpen=false
            if(!mailWindow.attachFile(a.path))return false
            break
        case "revealFile":
            filesOpen = true; raiseApp("files"); launcherOpen = false
            filesWindow.revealFile(a.path, a.serverId); break
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
        else if (app === "settings" && !fixture) { settingsOpen = true; raiseApp("settings"); launcherOpen = false }
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
        onCloseRequested: { shell.filesOpen = false; shell.focusAfterClose("files"); if (shell.adaptive && shell.foregroundApp === "files") shell.filesOpen = true }
        onRequestServers: shell.serversOpen = true
        onRequestDisconnect: shell.confirmSessionEnd("Se déconnecter", function() { shell.controller.disconnectServer() })
        onRequestDocuments:path=>shell.act("openDocument", { path: path })
        onRequestMail:path=>shell.act("attachToMail", { path:path })
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
        onCloseRequested: { shell.logsOpen = false; shell.focusAfterClose("logs"); if (shell.adaptive && shell.foregroundApp === "files") shell.filesOpen = true }
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
        onCloseRequested: { shell.terminalOpen = false; shell.focusAfterClose("terminal"); if (shell.adaptive && shell.foregroundApp === "files") shell.filesOpen = true }
        onRequestServers: shell.serversOpen=true
    }

    WebWindow {
        id: webWindow
        visible: shell.webOpen && !shell.engineOn && (!shell.adaptive || shell.foregroundApp === "web")
        engineAvailable: shell.engineBrowser !== null && shell.engineBrowser.available
        onRequestEngineMode: shell.webText = false
        adaptive: shell.adaptive
        x: shell.adaptive ? 8 : 320; y: shell.adaptive ? shell.appTop : 86
        width: shell.adaptive ? shell.width - 16 : 940; height: shell.adaptive ? shell.appHeight : 660
        z: shell.windowOrder.indexOf("web") + 1
        backdrop: wallpaper; browser: shell.browserController; remote: shell.controller
        onCloseRequested: { shell.webOpen = false; shell.focusAfterClose("web"); if (shell.adaptive && shell.foregroundApp === "files") shell.filesOpen = true }
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
        onCloseRequested: { shell.mailOpen = false; shell.focusAfterClose("mail"); if (shell.adaptive && shell.foregroundApp === "files") shell.filesOpen = true }
        onRequestServers: shell.serversOpen = true
        onRequestAttachmentFiles:(serverId,generation)=>shell.route({type:"pickMailAttachment",serverId:serverId,generation:generation})
    }

    DbWindow {
        id: dbWindow
        visible: shell.dbOpen && (!shell.adaptive || shell.foregroundApp === "db")
        adaptive: shell.adaptive
        x: shell.adaptive ? 8 : 350; y: shell.adaptive ? shell.appTop : 104
        width: shell.adaptive ? shell.width - 16 : 960; height: shell.adaptive ? shell.appHeight : 650
        z: shell.windowOrder.indexOf("db") + 1
        backdrop: wallpaper; db: shell.dbController; remote: shell.controller
        onCloseRequested: { shell.dbOpen = false; shell.focusAfterClose("db"); if (shell.adaptive && shell.foregroundApp === "files") shell.filesOpen = true }
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
        onCloseRequested: { shell.systemOpen = false; shell.focusAfterClose("system"); if (shell.adaptive && shell.foregroundApp === "files") shell.filesOpen = true }
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
        onCloseRequested: { shell.networkOpen = false; shell.focusAfterClose("network"); if (shell.adaptive && shell.foregroundApp === "files") shell.filesOpen = true }
        onRequestServers: shell.serversOpen = true
        onRequestCopy: (what, text) => shell.copyText(what, text)
    }

    SettingsWindow {
        id: settingsWindow
        visible: shell.settingsOpen && (!shell.adaptive || shell.foregroundApp === "settings")
        adaptive: shell.adaptive
        x: shell.adaptive ? 8 : 420; y: shell.adaptive ? shell.appTop : 110
        width: shell.adaptive ? shell.width - 16 : 720; height: shell.adaptive ? shell.appHeight : 520
        z: shell.windowOrder.indexOf("settings") + 1
        backdrop: wallpaper
        wallpapers: shell.wallpapers; wallpaperName: shell.wallpaperName; panelEdge: shell.panelEdge
        appVersion: Qt.application.version
        onCloseRequested: { shell.settingsOpen = false; shell.focusAfterClose("settings"); if (shell.adaptive && shell.foregroundApp === "files") shell.filesOpen = true }
        onChooseWallpaper: name => shell.setWallpaper(name)
        onChooseEdge: edge => { shell.panelEdge = edge; if (!shell.fixture) displaySettings.panelEdge = edge }
        onRequestServers: shell.serversOpen = true
    }

    DisksWindow {
        id:disksWindow
        visible:shell.disksOpen && (!shell.adaptive || shell.foregroundApp==="disks")
        adaptive:shell.adaptive
        x:shell.adaptive?8:300;y:shell.adaptive?shell.appTop:100
        width:shell.adaptive?shell.width-16:920;height:shell.adaptive?shell.appHeight:640
        z:shell.windowOrder.indexOf("disks")+1
        backdrop:wallpaper;disks:shell.disksController;remote:shell.controller
        onCloseRequested: { shell.disksOpen = false; shell.focusAfterClose("disks"); if (shell.adaptive && shell.foregroundApp === "files") shell.filesOpen = true }
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
        onCloseRequested: { shell.documentsOpen = false; shell.focusAfterClose("docs"); if (shell.adaptive && shell.foregroundApp === "files") shell.filesOpen = true }
        onRequestServers:shell.serversOpen=true
        onRequestFiles:(path, serverId)=>shell.route({ type:"revealFile", path:path, serverId:serverId })
    }
    // Closing the window: unsaved MDok drafts first, then running tasks (asked once, then the app quits).
    function allowAppClose(){
        if(mailWindow.hasUnsentDraft){
            serversOpen=false;mailOpen=true;raiseApp("mail")
            mailWindow.requestQuit(function(){if(shell.allowAppClose())Qt.quit()});return false
        }
        if ((documentsController && documentsController.hasDirtyTabs())
                || (taskManager && taskManager.guardingCount() > 0)) {
            confirmSessionEnd("Quitter", function() { Qt.quit() }); return false
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
            const windows = [{key: "files", item: filesWindow}, {key: "browser", item: browserWindow}, {key: "logs", item: logsWindow}, {key: "terminal", item: terminalWindow}, {key:"disks",item:disksWindow}, {key:"docs",item:documentsWindow}, {key:"web",item:(shell.engineOn && shell.engineWindowItem) ? shell.engineWindowItem : webWindow}, {key:"network",item:networkWindow}, {key:"system",item:systemWindow}, {key:"db",item:dbWindow}, {key:"mail",item:mailWindow}, {key:"settings",item:settingsWindow}]
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
                if (shell.notificationsOpen && !shell.pressInside(notificationPanel, mouse.x, mouse.y)) shell.notificationsOpen = false
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
        running: ({ files: shell.filesOpen, logs: shell.logsOpen, terminal: shell.terminalOpen, disks:shell.disksOpen, docs:shell.documentsOpen, browser: shell.fixture ? shell.browserOpen : shell.webOpen, network: shell.networkOpen, system: shell.systemOpen, db: shell.dbOpen, mail: shell.mailOpen, settings: shell.settingsOpen, launcher: shell.launcherOpen })
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
        searchEnabled: !shell.fixture
        unreadCount: shell.unreadCount
        onBellClicked: shell.toggleNotifications()
        onSearchAccepted: { const r = shell.searchResults(searchText); if (r.length > 0) { shell.openSearchResult(r[0]); searchText = "" } }
        onSearchCleared: searchText = ""
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

    // Notification panel under the bell.
    Glass {
        id: notificationPanel
        objectName: "notificationPanel"
        z: 11
        visible: shell.notificationsOpen && !shell.fixture
        width: Math.max(0, Math.min(380, shell.width - 16)); height: Math.max(0, Math.min(shell.height - 60, 64 + Math.max(1, Math.min(shell.notifications.length, 6)) * 66))
        x: Math.max(8, Math.min(shell.width - width - 8, 1030)); y: 52
        backdrop: wallpaper; radius: 14; tint: "#E6101A2E"
        Text { x: 16; y: 14; text: "Notifications"; color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
        Rectangle {
            objectName: "notificationsClear"
            visible: shell.notifications.length > 0
            anchors.right: parent.right; anchors.rightMargin: 12; y: 8; width: 96; height: 26; radius: 13
            color: nc.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
            Text { anchors.centerIn: parent; text: "Tout effacer"; color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily }
            MouseArea { id: nc; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: shell.clearNotifications() }
        }
        Text {
            visible: shell.notifications.length === 0
            objectName: "notificationsEmpty"
            x: 16; y: 52; width: parent.width - 32; text: "Aucune notification."
            color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily
        }
        ListView {
            objectName: "notificationList"
            x: 8; y: 44; width: parent.width - 16; height: parent.height - 52
            clip: true; spacing: 4
            model: shell.notifications
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width; height: 62; radius: 9; color: "#14FFFFFF"
                Rectangle { x: 10; y: 12; width: 8; height: 8; radius: 4
                    color: parent.modelData.kind === "error" ? "#F87171" : (parent.modelData.kind === "warn" ? "#FBBF24" : "#4ADE80") }
                Text { x: 28; y: 7; width: parent.width - 80; elide: Text.ElideRight; text: parent.modelData.title; color: Theme.ink; font.pixelSize: 12; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
                Text { anchors.right: parent.right; anchors.rightMargin: 10; y: 8; text: shell.notificationTime(parent.modelData.time); color: Theme.inkMuted; font.pixelSize: 10; font.family: Theme.fontFamily }
                Text { x: 28; y: 26; width: parent.width - 40; height: 32; wrapMode: Text.WordWrap; elide: Text.ElideRight; maximumLineCount: 2
                    text: parent.modelData.text; color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily }
            }
        }
    }

    // Results of the top-bar search, listed under it.
    Glass {
        id: searchPopup
        objectName: "searchResults"
        z: 11
        readonly property var results: shell.searchResults(topBar.searchText)
        visible: !shell.fixture && !shell.adaptive && results.length > 0
        x: 511; y: 44; width: 515; height: Math.min(results.length, 6) * 34 + 12
        backdrop: wallpaper; radius: 14; tint: "#E6101A2E"
        Column {
            x: 6; y: 6; width: parent.width - 12
            Repeater {
                model: searchPopup.results.slice(0, 6)
                Rectangle {
                    required property var modelData
                    required property int index
                    objectName: "searchResult_" + index
                    width: parent.width; height: 34; radius: 9
                    color: index === 0 ? "#33FFFFFF" : (rm.containsMouse ? "#26FFFFFF" : "transparent")
                    Text { x: 12; anchors.verticalCenter: parent.verticalCenter; text: parent.modelData.t; color: Theme.ink; font.pixelSize: 13; font.family: Theme.fontFamily }
                    Text { anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: parent.modelData.d; color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily }
                    MouseArea { id: rm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { shell.openSearchResult(parent.modelData); topBar.searchText = "" } }
                }
            }
        }
    }

    // Session menu under the server name in the top bar.
    Glass {
        id: sessionMenu
        objectName: "sessionMenu"
        z: 11
        visible: shell.sessionMenuOpen && !shell.fixture
        x: Math.max(8, shell.width - width - 12); y: 52
        width: Math.min(280, shell.width - 16); height: 96
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
        width: Math.max(0, Math.min(420, shell.width - 24))
        x: Math.max(12, shell.width - width - 12)
        // Keep the task panel accessible above the bottom dock and within
        // the available viewport even when the host is resized very short.
        readonly property bool coversDock: !shell.adaptive && appPanel.edge !== "left" && appPanel.x + appPanel.width > x - 8
        y: Math.max(8, Math.min(shell.height - height - 8,
            coversDock ? appPanel.y - height - 8 : shell.height - height - (shell.adaptive ? 84 : 12)))
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
    Toast { id: routeToast; adaptive: shell.adaptive }

    SessionGuard { id: sessionGuard; anchors.fill: parent; manager: shell.taskManager }
    focus: true
    onForegroundAppChanged: Qt.callLater(shell.restoreWindowFocus)
    function handleBack() {
        if (sessionGuard.visible) { sessionGuard.stay(); return true }
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
        if (foregroundApp === "settings") { settingsOpen = false; foregroundApp = "files"; filesOpen = true; return true }
        if (foregroundApp === "network") {
            if (networkWindow.handleBack()) return true
            foregroundApp = "files"; filesOpen = true; return true
        }
        if (foregroundApp === "web") {
            if ((shell.engineOn && shell.engineWindowItem) ? shell.engineWindowItem.handleBack() : webWindow.handleBack()) return true
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
        restoreWallpaper()
        if (fixture) filesOpen = true
        else serversOpen = true
    }
}
