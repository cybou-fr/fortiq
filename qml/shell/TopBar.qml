import QtQuick

// TOP (0,0)→(1536,47): brand mark + server label, centred search,
// local indicators and clock. Server-state is labelled separately from
// local-OS state; no fake Wi-Fi/Bluetooth toggles live here.
Rectangle {
    id: bar
    property string serverLabel: "Linux"
    property string clockText: ""
    property bool connected: false
    color: Theme.topBar
    height: 47

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width; height: 1
        color: "#2A4C7A"
    }

    Rectangle {
        x: 22; y: 11
        width: 25; height: 25; radius: 12.5
        color: "transparent"
        border.width: 2.5; border.color: "white"
        Rectangle {
            anchors.centerIn: parent
            width: 13; height: 13; radius: 6.5
            color: "transparent"; border.width: 2; border.color: "white"
        }
    }
    Text {
        x: 64; anchors.verticalCenter: parent.verticalCenter
        text: bar.serverLabel
        color: Theme.ink
        font.pixelSize: 15; font.weight: Font.DemiBold; font.family: Theme.fontFamily
    }

    Rectangle {
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
        anchors.right: avatar.left
        anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        text: bar.clockText
        color: Theme.ink
        font.pixelSize: 13; font.family: Theme.fontFamily
    }
    Rectangle {
        id: avatar
        x: 1491; y: 10
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
