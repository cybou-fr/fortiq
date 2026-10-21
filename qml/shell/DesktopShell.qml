import QtQuick

// The master composition at its native 1536×1024 coordinates
// (UI_SPEC_V1 §1). The host scales this canvas uniformly to the window.
// `fixture` is the visual-only reference scene: fixed clock, sample files,
// launcher and control center open. It is never production data.
Item {
    id: shell
    property var controller: null
    property bool fixture: false
    property url wallpaperSource: ""
    property bool launcherOpen: fixture
    property bool controlOpen: fixture
    property bool filesOpen: true
    property bool browserOpen: fixture
    property date now: new Date()

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
    }

    Wallpaper { id: wallpaper; anchors.fill: parent; source: shell.wallpaperSource }

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
        onCloseRequested: shell.filesOpen = false
    }

    BrowserWindow {
        visible: shell.browserOpen
        x: 554; y: 512; width: 650; height: 400
        backdrop: wallpaper
        fixture: shell.fixture
    }

    LauncherPanel {
        visible: shell.launcherOpen
        x: 98; y: 497; width: 438; height: 443
        backdrop: wallpaper
        fixture: shell.fixture
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

    AppRail {
        x: 4; y: 63; width: 84; height: 875
        backdrop: wallpaper
        onLaunch: (app) => shell.launch(app)
    }

    Dock {
        x: 384; y: 948
        backdrop: wallpaper
        onLaunch: (app) => shell.launch(app)
    }

    TopBar {
        width: parent.width
        serverLabel: shell.fixture ? "Linux"
            : (shell.controller && shell.controller.connected ? shell.controller.serverLabel : "Aucun serveur")
        clockText: shell.barClock
        connected: shell.controller !== null && shell.controller.connected
        MouseArea {
            x: 1100; width: 380; height: parent.height
            onClicked: shell.controlOpen = !shell.controlOpen
        }
    }
}
