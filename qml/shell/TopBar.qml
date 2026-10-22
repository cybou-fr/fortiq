import QtQuick

// TOP (0,0)→(1536,47): brand mark + server label, centred search,
// local indicators and clock. Server-state is labelled separately from
// local-OS state; no fake Wi-Fi/Bluetooth toggles live here.
Rectangle {
    id: bar
    property string serverLabel: "Linux"
    property string clockText: ""
    property bool connected: false
    property bool compact: false
    signal serverClicked()
    color: Theme.topBar
    height: 47

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width; height: 1
        color: "#2A4C7A"
    }

    // New FORTIQ mark: the only sanctioned visual difference from the owner
    // reference sketch's original circular placeholder brand.
    Image {
        x: 16; y: 5
        width: 36; height: 36
        source: "qrc:/branding/fortiq_mark.png"
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: false
    }
    // Connection state + server name: opens the server manager.
    Rectangle {
        x: 58; anchors.verticalCenter: parent.verticalCenter
        width: compact ? Math.max(100, bar.width - 108) : serverText.implicitWidth + 34; height: compact ? 44 : 30; radius: 15
        color: serverMouse.containsMouse ? "#26FFFFFF" : "transparent"
        Rectangle {
            x: 10; anchors.verticalCenter: parent.verticalCenter
            width: 8; height: 8; radius: 4
            color: bar.connected ? "#4ADE80" : "#94A3B8"
        }
        MouseArea { id: serverMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: bar.serverClicked() }
    }
    Text {
        id: serverText
        x: 82; anchors.verticalCenter: parent.verticalCenter
        width: bar.compact ? Math.max(0, bar.width - 140) : implicitWidth
        elide: Text.ElideRight
        text: bar.serverLabel
        color: Theme.ink
        font.pixelSize: 15; font.weight: Font.DemiBold; font.family: Theme.fontFamily
    }

    Rectangle {
        visible: !bar.compact
        x: 511; y: 8
        width: 515; height: 31; radius: 15.5
        color: "#33A9C8F0"
        border.width: 1; border.color: "#4D9DC4F5"
        Text {
            x: 14; anchors.verticalCenter: parent.verticalCenter
            text: "⌕"
            color: Theme.ink; font.pixelSize: 18
        }
        Text {
            x: 42; anchors.verticalCenter: parent.verticalCenter
            text: "Rechercher des applications, fichiers, paramètres..."
            color: "#DCE6F5"; font.pixelSize: 12; font.family: Theme.fontFamily
        }
    }

    Row {
        visible: !bar.compact
        anchors.right: clock.left
        anchors.rightMargin: 22
        anchors.verticalCenter: parent.verticalCenter
        spacing: 22
        Repeater {
            model: ["⧉", "▤", "|", "🔔", "◠", "ᛒ", "🔊"]
            Text {
                required property string modelData
                text: modelData
                color: modelData === "|" ? "#6F86A8" : Theme.ink
                font.pixelSize: 15
            }
        }
    }
    Text {
        id: clock
        visible: !bar.compact
        anchors.right: avatar.left
        anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        text: bar.clockText
        color: Theme.ink
        font.pixelSize: 13; font.family: Theme.fontFamily
    }
    Rectangle {
        id: avatar
        x: bar.compact ? bar.width - 40 : 1491; y: 10
        width: 27; height: 27; radius: 13.5
        color: "#E9EEF5"
        border.width: 2; border.color: "white"
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 6; width: 10; height: 10; radius: 5; color: "#3B4250"
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 17; width: 17; height: 8; radius: 4; color: "#3B4250"
        }
    }
}
