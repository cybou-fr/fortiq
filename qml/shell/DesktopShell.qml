import QtCore
import QtQuick

// The master composition at its native 1536×1024 coordinates
// (UI_SPEC_V1 §1). The host scales this canvas uniformly to the window.
// `fixture` is the visual-only reference scene: fixed clock, sample files,
// launcher and control center open. It is never production data.
Item {
    id: shell
    property var controller: null
    property var logsController: null
    property bool fixture: false
    property string fixturePreview: ""
    property url wallpaperSource: ""
    // Owner-supplied wallpapers bundled with the shell. Choice is RAM-only;
    // right-click the desktop to cycle. An explicit file overrides it.
    readonly property var wallpapers: ["aurora", "desert", "city"]
    property string wallpaperName: "aurora"
    property bool launcherOpen: fixture && fixturePreview !== "logs" && fixturePreview !== "incident" && fixturePreview !== "service" && fixturePreview !== "service"
    property bool controlOpen: fixture
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
        if (app === "files") filesOpen = true
        else if (app === "launcher") launcherOpen = !launcherOpen
        else if (app === "browser") browserOpen = !browserOpen
        else if (app === "logs") { logsOpen = true; launcherOpen = false }
        else if (app === "trash") {
            filesOpen = true
            if (controller && controller.connected && controller.trashDirectory.length > 0) controller.navigate(controller.trashDirectory)
            else serversOpen = true
        }
    }

    Wallpaper {
        id: wallpaper
        anchors.fill: parent
        source: shell.wallpaperSource.toString().length > 0 ? shell.wallpaperSource
            : (shell.wallpaperName.length > 0 ? Qt.resolvedUrl("../../assets/wallpapers/" + shell.wallpaperName + ".jpg") : "")
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
        x: 119; y: 70
        timeText: shell.timeText
        dateText: shell.dateText
        sampleWeather: shell.fixture
    }

    FilesWindow {
        visible: shell.filesOpen
        x: 471; y: 72; width: 717; height: 403
        backdrop: wallpaper
        controller: shell.controller
        fixture: shell.fixture
        fixturePreview: shell.fixturePreview
        onCloseRequested: shell.filesOpen = false
        onRequestServers: shell.serversOpen = true
    }

    BrowserWindow {
        visible: shell.browserOpen
        x: 554; y: 512; width: 650; height: 400
        backdrop: wallpaper
        fixture: shell.fixture
    }

    LogsWindow {
        visible: shell.logsOpen
        x: 300; y: 130
        width: 900; height: 640
        backdrop: wallpaper
        logs: shell.logsController
        remote: shell.controller
        fixture: shell.fixture
        fixtureIncident: shell.fixture && shell.fixturePreview === "incident"
        fixtureService: shell.fixture && shell.fixturePreview === "service"
        onCloseRequested: shell.logsOpen = false
        onRequestServers: shell.serversOpen = true
    }

    LauncherPanel {
        visible: shell.launcherOpen
        x: 98; y: 497; width: 438; height: 443
        backdrop: wallpaper
        fixture: shell.fixture
        onLaunch: (app) => shell.launch(app)
    }

    ControlCenter {
        visible: shell.controlOpen
        x: 1215; y: 64; width: 310; height: 679
        backdrop: wallpaper
        fixture: shell.fixture
    }

    WorkspaceSwitcher {
        x: 1215; y: 765; width: 310; height: 129
        backdrop: wallpaper
    }

    AppPanel {
        id: appPanel
        backdrop: wallpaper
        edge: shell.panelEdge
        x: edge === "left" ? 4 : (parent.width - width) / 2
        y: edge === "left" ? 59 : parent.height - height - 8
        width: edge === "left" ? 84 : 560
        height: edge === "left" ? parent.height - 59 - 8 : 70
        running: ({ files: shell.filesOpen, logs: shell.logsOpen, launcher: shell.launcherOpen })
        onLaunch: (app) => shell.launch(app)
        onMoveTo: (edge) => {
            shell.panelEdge = edge
            if (!shell.fixture) displaySettings.panelEdge = edge
        }
    }

    TopBar {
        width: parent.width
        serverLabel: shell.fixture ? "Linux"
            : (shell.controller && shell.controller.connected ? shell.controller.serverLabel : "Aucun serveur")
        clockText: shell.barClock
        connected: shell.controller !== null && shell.controller.connected
        onServerClicked: if (!shell.fixture) shell.serversOpen = true
        MouseArea {
            x: 1100; width: 380; height: parent.height
            onClicked: shell.controlOpen = !shell.controlOpen
        }
    }

    ServerManager {
        anchors.fill: parent
        z: 100
        visible: shell.serversOpen
        backdrop: wallpaper
        controller: shell.controller
        onClosed: shell.serversOpen = false
    }
    Component.onCompleted: if (!fixture && controller && !controller.connected) serversOpen = true
}
