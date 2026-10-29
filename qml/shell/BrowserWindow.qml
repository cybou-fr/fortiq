import QtQuick
import QtQuick.Effects

// BROWSER (554,512)→(1204,912). Visual shell only: no web engine and no
// network access. A banner states this outside the reference fixture.
Item {
    id: win
    property Item backdrop
    property bool fixture: false

    RectangularShadow {
        anchors.fill: parent
        offset.y: 12; radius: 14; blur: 40
        color: "#66000814"
    }

    Glass {
        id: chrome
        anchors.fill: parent
        backdrop: win.backdrop
        radius: 14
        shadow: false
        tint: Theme.appWindow
        blurAmount: 0.9
    }
    onXChanged: { chrome.syncOrigin(); page.syncOrigin() }
    onYChanged: { chrome.syncOrigin(); page.syncOrigin() }
    WindowDragArea {
        width: parent.width; height: 36
        windowItem: win
    }

    // Scenic page: the same wallpaper slice, unblurred, inside the frame.
    ShaderEffectSource {
        id: page
        x: 1; y: 72
        width: parent.width - 2; height: parent.height - 73
        sourceItem: win.backdrop
        live: true
        function syncOrigin() {
            const p = page.mapToItem(win.backdrop, 0, 0)
            sourceRect = Qt.rect(p.x, p.y, width, height)
        }
        Component.onCompleted: syncOrigin()
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: pageMask
        }
    }
    Rectangle {
        id: pageMask
        width: page.width; height: page.height
        bottomLeftRadius: 13; bottomRightRadius: 13
        visible: false; layer.enabled: true
    }

    Row {
        x: 14; y: 14; spacing: 8
        Repeater {
            model: ["#FF5F57", "#FEBC2E", "#28C840"]
            Rectangle { required property string modelData; width: 11; height: 11; radius: 5.5; color: modelData }
        }
    }
    Rectangle {
        x: 83; y: 6; width: 158; height: 28; radius: 8; color: "#26FFFFFF"
        Text { x: 34; anchors.verticalCenter: parent.verticalCenter; text: "Nouvel onglet"; color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily }
        Text { x: 136; anchors.verticalCenter: parent.verticalCenter; text: "×"; color: Theme.ink; font.pixelSize: 15 }
    }
    Text { x: 248; y: 8; text: "+"; color: Theme.ink; font.pixelSize: 18 }
    Row {
        anchors.right: parent.right; anchors.rightMargin: 22; y: 8; spacing: 22
        Repeater { model: ["–", "▢", "×"]; Text { required property string modelData; text: modelData; color: Theme.ink; font.pixelSize: 14 } }
    }

    Row {
        x: 16; y: 41; spacing: 16
        Repeater { model: ["←", "→", "⟳"]; Text { required property string modelData; text: modelData; color: Theme.ink; font.pixelSize: 15 } }
    }
    Rectangle {
        x: 108; y: 39; width: 390; height: 26; radius: 13; color: "#1AFFFFFF"
        Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: "⌕"; color: Theme.ink; font.pixelSize: 14 }
        Text { x: 34; anchors.verticalCenter: parent.verticalCenter; text: "Rechercher ou entrer une adresse web"; color: "#D7E1F0"; font.pixelSize: 11; font.family: Theme.fontFamily }
    }
    Row {
        x: 507; y: 42; spacing: 20
        Repeater { model: ["⛉", "☆", "⚙", "⋯"]; Text { required property string modelData; text: modelData; color: Theme.ink; font.pixelSize: 14 } }
    }

    Rectangle {
        x: 155; y: 170; width: 338; height: 38; radius: 19; color: "#F2FFFFFF"
        Text { x: 14; anchors.verticalCenter: parent.verticalCenter; text: "⌕"; color: "#2A3140"; font.pixelSize: 16 }
        Text { x: 44; anchors.verticalCenter: parent.verticalCenter; text: "Rechercher sur le Web..."; color: "#5A6478"; font.pixelSize: 11; font.family: Theme.fontFamily }
    }

    Row {
        x: 124; y: 258; spacing: 18
        Repeater {
            model: win.fixture
                ? [ { k: "media", t: "YouTube" }, { k: "chat", t: "Gmail" }, { k: "game", t: "GitHub" }, { k: "music", t: "ChatGPT" }, { k: "editor", t: "Linux News" }, { k: "add", t: "Ajouter" } ]
                : [ { k: "add", t: "Ajouter" } ]
            Item {
                required property var modelData
                width: 50; height: 72
                Rectangle {
                    width: 50; height: 50; radius: 12; color: "#E6FFFFFF"
                    AppIcon { anchors.centerIn: parent; size: 32; kind: modelData.k }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter; y: 58
                    text: modelData.t; color: "white"; font.pixelSize: 10; font.family: Theme.fontFamily
                }
            }
        }
    }

    Rectangle {
        visible: !win.fixture
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 18
        width: notice.implicitWidth + 24; height: 26; radius: 13; color: "#CC0B1A33"
        Text { id: notice; anchors.centerIn: parent; text: "Navigateur non disponible : moteur web via SSH non encore validé"; color: Theme.ink; font.pixelSize: 11 }
    }
}
