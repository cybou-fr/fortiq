import QtQuick

// The "no server connected" state of an application window: what is missing, what this app needs it for, one clear action.
// Every application uses this instead of its own text and small button. `buttonName` keeps each app's own objectName.
Column {
    id: state
    property string hint: ""
    property string buttonName: ""
    property bool touch: false
    signal chooseServer()
    spacing: 12

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 52; height: 52; radius: 26
        color: "#1FFFFFFF"; border.width: 1; border.color: "#33FFFFFF"
        Rectangle { anchors.centerIn: parent; width: 14; height: 14; radius: 7; color: "#8FA0BC" }
    }
    Text {
        width: parent.width; horizontalAlignment: Text.AlignHCenter
        text: "Aucun serveur connecté"; textFormat: Text.PlainText
        color: Theme.ink; font.pixelSize: state.touch ? 19 : 18; font.family: Theme.fontFamily
    }
    Text {
        visible: state.hint.length > 0
        width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
        text: state.hint; textFormat: Text.PlainText
        color: Theme.inkMuted; font.pixelSize: state.touch ? 14 : 13; font.family: Theme.fontFamily
    }
    Item { width: 1; height: 4 }
    StateButton {
        objectName: state.buttonName
        anchors.horizontalCenter: parent.horizontalCenter
        touch: state.touch
        text: "Choisir un serveur…"
        onClicked: state.chooseServer()
    }
}
