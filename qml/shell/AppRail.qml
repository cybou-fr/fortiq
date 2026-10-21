import QtQuick

// RAIL (4,59)→(88,940). Entries in master order; `available: false`
// entries are shown but report "bientôt" instead of launching.
Glass {
    id: rail
    radius: 14
    tint: Theme.rail
    signal launch(string app)

    readonly property var apps: [
        { kind: "files", label: "Fichiers", app: "files", y: 14, available: true },
        { kind: "browser", label: "Navigateur", app: "browser", y: 98, available: false },
        { kind: "terminal", label: "Terminal", app: "terminal", y: 183, available: false },
        { kind: "editor", label: "Éditeur", app: "editor", y: 268, available: false },
        { kind: "docs", label: "Bureautique", app: "docs", y: 352, available: false },
        { kind: "graphics", label: "Graphisme", app: "graphics", y: 437, available: false },
        { kind: "media", label: "Multimédia", app: "media", y: 522, available: false },
        { kind: "settings", label: "Paramètres", app: "settings", y: 607, available: false }
    ]

    Repeater {
        model: rail.apps
        Item {
            required property var modelData
            x: 0; y: modelData.y
            width: rail.width; height: 80
            AppIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 4
                size: 44
                kind: modelData.kind
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 56
                text: modelData.label
                color: Theme.ink
                font.pixelSize: 12; font.family: Theme.fontFamily
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: rail.launch(modelData.app)
            }
        }
    }

    Item {
        y: parent.height - 80
        width: parent.width; height: 70
        AppIcon { anchors.horizontalCenter: parent.horizontalCenter; y: 6; size: 26; kind: "apps" }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 44
            text: "Toutes les apps"
            color: Theme.ink
            font.pixelSize: 11; font.family: Theme.fontFamily
        }
        MouseArea { anchors.fill: parent; onClicked: rail.launch("launcher") }
    }
}
