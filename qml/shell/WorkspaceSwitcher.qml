import QtQuick

// WORKSPACES (1215,765)→(1526,894). Workspaces live in RAM only; the
// layout is not persisted until remote saved workspaces exist.
Glass {
    id: ws
    property int current: 1
    property var names: ["1. Travail", "2. Perso", "3. Création"]
    radius: 16
    tint: Theme.control

    Row {
        x: 13; y: 21; spacing: 13
        Repeater {
            model: ws.names
            Item {
                required property string modelData
                required property int index
                width: 77; height: 90
                Rectangle {
                    width: 77; height: 66; radius: 6
                    color: "transparent"
                    border.width: index === ws.current ? 2 : 0
                    border.color: Theme.accent
                    Rectangle {
                        anchors.fill: parent; anchors.margins: 3; radius: 4
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#3D5F9C" }
                            GradientStop { position: 0.55; color: "#6D6E8E" }
                            GradientStop { position: 1; color: "#141C30" }
                        }
                    }
                    MouseArea { anchors.fill: parent; onClicked: ws.current = index }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter; y: 76
                    text: modelData; color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily
                }
            }
        }
    }
    Text { x: 279; y: 36; text: "+"; color: "white"; font.pixelSize: 28 }
}
