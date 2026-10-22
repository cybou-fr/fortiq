import QtQuick

// LAUNCHER (97,498)→(536,941). Pinned grid + recommendations. Outside the
// fixture, entries are FORTIQ apps (unavailable ones say so) and the
// recommendations list stays empty until real server evidence exists.
Glass {
    id: launcher
    property bool fixture: false
    signal launch(string app)
    radius: 16
    tint: Theme.launcher

    readonly property var samplePinned: [
        { k: "browser", t: "Firefox" }, { k: "files", t: "Fichiers" }, { k: "terminal", t: "Terminal" },
        { k: "editor", t: "VS Code" }, { k: "office", t: "LibreOffice" }, { k: "paint", t: "GIMP" },
        { k: "vector", t: "Inkscape" }, { k: "cad", t: "Blender" }, { k: "cone", t: "VLC" },
        { k: "music", t: "Spotify" }, { k: "chat", t: "Teams" }, { k: "game", t: "Steam" }
    ]
    readonly property var appPinned: [
        { k: "files", t: "Fichiers" }, { k: "logs", t: "Journaux" }, { k: "terminal", t: "Terminal" },
        { k: "disks", t: "Disques" }, { k: "editor", t: "Éditeur" }, { k: "docs", t: "MDok" }, { k: "settings", t: "Paramètres" }
    ]
    readonly property var sampleRecent: [
        { k: "media", t: "Présentation CYBOU.pptx", w: "Il y a 12 min" }, { k: "sheet", t: "Budget 2026.xlsx", w: "Il y a 28 min" },
        { k: "docs", t: "Notes rapides.txt", w: "Il y a 1 h" }, { k: "graphics", t: "Montage vidéo.mp4", w: "Il y a 2 h" },
        { k: "files", t: "Capture d'écran.png", w: "Il y a 3 h" }, { k: "docs", t: "Plan de projet.docx", w: "Il y a 5 h" }
    ]

    Rectangle {
        x: 18; y: 17; width: parent.width - 36; height: 31; radius: 15.5
        color: "#1FFFFFFF"; border.width: 1; border.color: "#40FFFFFF"
        SymbolGlyph { x: 12; anchors.verticalCenter: parent.verticalCenter; glyph: "⌕"; color: Theme.ink; size: 16 }
        Text { x: 36; anchors.verticalCenter: parent.verticalCenter; text: "Rechercher des applications..."; color: "#C5D0E0"; font.pixelSize: 11; font.family: Theme.fontFamily }
    }

    Text { x: 25; y: 63; text: "Épinglé"; color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
    Rectangle {
        x: parent.width - 79; y: 62; width: 54; height: 18; radius: 9; color: "#26FFFFFF"
        Text { anchors.centerIn: parent; text: "Toutes  ›"; color: Theme.ink; font.pixelSize: 10; font.family: Theme.fontFamily }
    }

    Grid {
        x: 21; y: 92
        columns: 6; columnSpacing: 0; rowSpacing: 4
        Repeater {
            model: launcher.fixture ? launcher.samplePinned : launcher.appPinned
            Item {
                required property var modelData
                width: 69; height: 74
                AppIcon { anchors.horizontalCenter: parent.horizontalCenter; size: 38; kind: modelData.k }
                MouseArea {
                    anchors.fill: parent
                    enabled: !launcher.fixture && (["files","logs","terminal","disks","docs"].indexOf(modelData.k)>=0)
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: launcher.launch(modelData.k)
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter; y: 47
                    text: modelData.t; color: Theme.ink
                    font.pixelSize: 10; font.family: Theme.fontFamily
                }
            }
        }
    }

    Text { x: 25; y: 252; text: "Recommandé"; color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
    Text {
        visible: !launcher.fixture
        x: 25; y: 280; width: parent.width - 50; wrapMode: Text.WordWrap
        text: "Les fichiers récents du serveur apparaîtront ici lorsqu'ils seront réellement disponibles."
        color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
    }
    Grid {
        visible: launcher.fixture
        x: 25; y: 283
        columns: 2; columnSpacing: 0; rowSpacing: 15
        Repeater {
            model: launcher.sampleRecent
            Item {
                required property var modelData
                width: 218; height: 34
                AppIcon { y: 1; size: 30; kind: modelData.k }
                Text { x: 48; y: 0; text: modelData.t; color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily }
                Text { x: 48; y: 17; text: modelData.w; color: Theme.inkMuted; font.pixelSize: 10; font.family: Theme.fontFamily }
            }
        }
    }
}
