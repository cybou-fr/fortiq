import QtQuick
import QtQuick.Controls.Fusion

// Confirmation before the session ends (logout, disconnect, another server, quitting) while tasks still run.
// The list is what the TaskManager says guards the session: transfers, saves, sends, an open terminal.
// Reads and views are not listed: ending them loses nothing. Nothing here changes a task: continuing only ends the
// session, and the tasks then settle on their own as "cancelled" or, when a change may already have happened, "unknown".
Item {
    id: guard
    objectName: "sessionGuard"
    property var manager: null
    property var pending: null
    property string actionLabel: ""
    property var tasks: []
    visible: false
    z: 300

    function ask(label, proceed) {
        if (!manager || manager.guardingCount() === 0) { proceed(); return }
        actionLabel = label
        pending = proceed
        tasks = manager.guardingTasks()
        visible = true
    }
    function stay() { visible = false; pending = null }
    function proceedAnyway() {
        const go = pending
        visible = false
        pending = null
        if (go) go()
    }
    function describe(t) {
        if (t.kind === "terminal") return "Terminal ouvert : la session shell sera fermée"
        if (t.kind === "docSave" || t.kind === "docSaveCopy") return "Enregistrement en cours : le résultat deviendrait inconnu"
        if (t.kind === "mailSend") return "Envoi en cours : on ne saurait pas si le message est parti"
        if (t.kind === "upload") return "Envoi de fichier : transfert interrompu"
        if (t.kind === "download") return "Téléchargement : transfert interrompu"
        return "Opération en cours sur le serveur : résultat possiblement inconnu"
    }

    Connections {
        target: guard.manager
        ignoreUnknownSignals: true
        function onTasksChanged() { if (guard.visible && guard.manager) guard.tasks = guard.manager.guardingTasks() }
    }

    Rectangle { anchors.fill: parent; color: "#99000000" }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onClicked: {} }   // modal: nothing behind is reachable
    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 540); height: Math.min(parent.height - 32, column.implicitHeight + 32)
        radius: 14; color: "#F5121C30"; border.width: 1; border.color: "#50DCEBFF"
        Flickable {
            anchors.fill: parent; anchors.margins: 16; clip: true; contentWidth: width; contentHeight: column.implicitHeight; boundsBehavior: Flickable.StopAtBounds
            Column {
                id: column
                width: parent.width; spacing: 10
                Text {
                    objectName: "sessionGuardTitle"
                    width: parent.width; wrapMode: Text.Wrap; color: Theme.ink; font.pixelSize: 16; font.bold: true
                    text: guard.tasks.length > 0 ? "Des tâches sont encore en cours" : "Les tâches sont terminées"
                }
                Text {
                    width: parent.width; wrapMode: Text.Wrap; color: Theme.inkMuted; font.pixelSize: 11
                    text: "Si vous continuez, la connexion est fermée. Ce qui est interrompu n'est pas rejoué : une écriture dont le résultat n'est pas connu est signalée « inconnu » dans la liste des tâches."
                }
                Repeater {
                    model: guard.tasks
                    Rectangle {
                        required property var modelData
                        objectName: "sessionGuardTask"
                        width: column.width; height: taskText.implicitHeight + 14; radius: 8; color: "#30182740"
                        Column {
                            id: taskText
                            x: 10; y: 7; width: parent.width - 20; spacing: 2
                            Text { width: parent.width; elide: Text.ElideMiddle; textFormat: Text.PlainText; text: modelData.title; color: Theme.ink; font.pixelSize: 12; font.bold: true }
                            Text { width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: guard.describe(modelData); color: "#FCD34D"; font.pixelSize: 10 }
                        }
                    }
                }
                Row {
                    spacing: 8
                    Button { objectName: "sessionGuardStay"; text: "Attendre"; onClicked: guard.stay() }
                    Button { objectName: "sessionGuardGo"; text: guard.actionLabel + " quand même"; onClicked: guard.proceedAnyway() }
                }
            }
        }
    }
}
