import QtQuick
import QtQuick.Controls

Item {
    id: win
    objectName: "terminalWindow"
    property bool adaptive: false
    property Item backdrop
    property var terminal: null
    property var remote: null
    property string pasteText: ""
    signal closeRequested()
    signal requestServers()
    component Action: Rectangle {
        property string text: ""
        signal clicked()
        width: caption.implicitWidth + 22
        height: win.adaptive ? 40 : 30
        radius: 8
        color: enabled ? (actionMouse.containsMouse ? "#663E80C4" : "#44395880") : "#221E304A"
        border.width: 1; border.color: "#33DCEBFF"
        opacity: enabled ? 1 : 0.45
        Text { id: caption; anchors.centerIn: parent; text: parent.text; color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily }
        MouseArea { id: actionMouse; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
    }
    function handleBack() {
        if (pasteDialog.visible) { pasteDialog.close(); return true }
        return false
    }
    Glass { anchors.fill: parent; backdrop: win.backdrop; tint: "#B3121C30"; blurAmount: 0.9; radius: 16 }
    Item {
        width: parent.width; height: 48
        WindowDragArea { anchors.fill: parent; windowItem: win; movable: !win.adaptive }
        Row {
            x: 14; y: 17; spacing: 9
            Repeater {
                model: ["#FF5F57", "#FEBC2E", "#28C840"]
                Rectangle {
                    required property string modelData
                    width: 12; height: 12; radius: 6; color: modelData
                    MouseArea { anchors.fill: parent; onClicked: if(parent.modelData==="#FF5F57")win.closeRequested() }
                }
            }
        }
        Text {
            x: 90; anchors.verticalCenter: parent.verticalCenter; width: Math.max(0,parent.width-340); elide: Text.ElideMiddle
            text: "Terminal · " + (win.remote && win.remote.connected ? win.remote.serverLabel : "Aucun serveur")
            color: Theme.ink; font.pixelSize: 14; font.family: Theme.fontFamily
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 4
            Action { text: "Copier"; onClicked: surface.copyScreen() }
            Action { text: "Coller…"; enabled: surface.inputEnabled; onClicked: surface.pasteRequested() }
            Action { text: win.terminal && win.terminal.running ? "Arrêter" : "Ouvrir"; enabled: !win.terminal || win.terminal.running || !win.terminal.busy
                onClicked: {
                    if(!win.remote || !win.remote.connected){win.requestServers();return}
                    if(win.terminal.running)win.terminal.stop();else win.terminal.start()
                }
            }
        }
    }
    TerminalView {
        id: surface
        objectName: "terminalSurface"
        x: 12; y: 52; width: parent.width-24; height: Math.max(32,parent.height-y-64)
        inputEnabled: win.terminal !== null && win.terminal.running
        onBytes: data => { if(win.terminal)win.terminal.send(data) }
        onGridChanged: (columns, rows) => { if(win.terminal)win.terminal.resize(columns, rows) }
        onPasteRequested: { win.pasteText=clipboardText(); pasteDialog.open() }
        Component.onCompleted: if(win.terminal)win.terminal.resize(columns,rows)
    }
    Connections {
        target: win.terminal
        function onOutput(data) { surface.feed(data) }
        function onResetScreen() { surface.reset() }
        function onChanged() { if(win.terminal.running)surface.forceActiveFocus() }
    }
    Row {
        x: 12; anchors.bottom: parent.bottom; anchors.bottomMargin: 24; spacing: 6
        Action { text: "Ctrl+C"; enabled: surface.inputEnabled; onClicked: surface.control(3) }
        Action { text: "Ctrl+D"; enabled: surface.inputEnabled; onClicked: surface.control(4) }
        Text { width: Math.max(30,win.width-220); elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter
            text: win.terminal ? win.terminal.status : "Connectez un serveur Linux, puis ouvrez le shell."
            color: Theme.inkMuted; font.pixelSize: 11
        }
    }
    Text { x:12; anchors.bottom:parent.bottom; anchors.bottomMargin:6; text:"Fermer la fenêtre masque le shell. Arrêter ferme la session. Aucun rejeu automatique."; color:Theme.inkMuted; font.pixelSize:9; width:parent.width-24; elide:Text.ElideRight }
    Dialog {
        id: pasteDialog
        anchors.centerIn: parent; width: Math.min(440, win.width-24); modal: true
        title: "Envoyer le presse-papiers au shell ?"
        contentHeight: 84
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Text { text: win.pasteText.length > 16000 ? "Texte trop volumineux (limite 16 Ko)." : "Le texte sera envoyé au serveur. Les retours à la ligne peuvent exécuter des commandes."; wrapMode: Text.WordWrap }
        onAccepted: { surface.paste(win.pasteText); win.pasteText=""; surface.forceActiveFocus() }
        onRejected: win.pasteText=""
    }
}
