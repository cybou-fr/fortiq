import QtQuick

// DOCK (384,948)→(1077,1017) + TRASH (1081,948)→(1153,1019).
Item {
    id: dock
    property Item backdrop
    signal launch(string app)
    width: 769; height: 71

    Glass {
        id: main
        backdrop: dock.backdrop
        width: 693; height: 69
        radius: 18
        tint: Theme.dock
        Row {
            x: 22; anchors.verticalCenter: parent.verticalCenter; spacing: 18
            Repeater {
                model: ["apps", "browser", "files", "terminal", "editor", "office", "cad", "paint", "music", "settings", "settings"]
                Item {
                    required property string modelData
                    required property int index
                    width: index === 0 ? 46 : 42; height: 46
                    AppIcon { anchors.centerIn: parent; size: modelData === "apps" ? 30 : 42; kind: modelData }
                    Rectangle {
                        visible: index === 0
                        x: 55; y: 6; width: 1; height: 34; color: "#40FFFFFF"
                    }
                    MouseArea { anchors.fill: parent; onClicked: dock.launch(modelData === "apps" ? "launcher" : modelData) }
                }
            }
        }
    }
    Glass {
        backdrop: dock.backdrop
        x: 697; width: 72; height: 71
        radius: 18
        tint: Theme.dock
        AppIcon { anchors.centerIn: parent; size: 36; kind: "trash" }
    }
}
