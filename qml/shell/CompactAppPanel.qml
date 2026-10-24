import QtQuick
import QtQuick.Controls.Fusion

// One scrollable app panel on narrow screens; icon art is not scaled down.
Rectangle {
    id: panel
    property var apps: []
    property string currentApp: "files"
    signal launch(string app)
    color: "#E60E182A"; radius: 16
    border.width: 1; border.color: "#30FFFFFF"
    Flickable {
        anchors.fill: parent; anchors.margins: 4; clip: true
        contentWidth: apps.width; contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        Row {
            id: apps
            spacing: 4
            Repeater {
                model: panel.apps.filter(app => app.ready).concat(
                    [{app: "servers", label: "Serveurs", kind: "settings", ready: true},
                     {app: "trash", label: "Corbeille", kind: "trash", ready: true}],
                    panel.apps.filter(app => !app.ready))
                Rectangle {
                    required property var modelData
                    width: 72; height: 60; radius: 12
                    color: panel.currentApp === modelData.app ? "#403B82F6" : "transparent"
                    opacity: modelData.ready ? 1 : 0.4
                    AppIcon { anchors.horizontalCenter: parent.horizontalCenter; y: 3; size: 30; kind: parent.modelData.kind }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; y: 38; width: 68; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; text: parent.modelData.label + (parent.modelData.ready ? "" : " · bientôt"); color: Theme.ink; font.pixelSize: 11 }
                    MouseArea { anchors.fill: parent; enabled: parent.modelData.ready; onClicked: panel.launch(parent.modelData.app) }
                }
            }
        }
    }
}
