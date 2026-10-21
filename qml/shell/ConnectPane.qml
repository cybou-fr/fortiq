import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Connection surface shown inside the Files window while no server is
// connected. Same flow as R0.1: password kept in memory only, explicit
// host-key verification before trust.
ScrollView {
    id: pane
    property var controller: null
    readonly property bool pending: controller !== null && controller.pendingFingerprint.length > 0
    clip: true
    contentWidth: availableWidth
    // Light paper surface regardless of the host's dark-mode palette.
    palette.base: "#FFFFFF"
    palette.text: "#1C2333"
    palette.window: "#F2F4F9"
    palette.windowText: "#1C2333"
    palette.button: "#E3E7EF"
    palette.buttonText: "#1C2333"
    palette.placeholderText: "#8A93A6"

    ColumnLayout {
        width: pane.availableWidth
        spacing: 10

        Text {
            text: "Connexion au serveur Linux (SSH / SFTP)"
            color: Theme.paperInk
            font.pixelSize: 16; font.bold: true; font.family: Theme.fontFamily
        }
        Text {
            text: "Seuls l'adresse et l'empreinte vérifiée de l'hôte sont mémorisées. Le mot de passe reste en mémoire."
            color: "#5A6478"; font.pixelSize: 11; wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        GridLayout {
            columns: 2; rowSpacing: 6; columnSpacing: 10
            Layout.fillWidth: true
            TextField { id: hostInput; Layout.fillWidth: true; placeholderText: "Adresse du serveur"; text: pane.controller ? pane.controller.rememberedHost : ""; enabled: pane.controller && !pane.controller.busy }
            TextField {
                id: portInput
                Layout.preferredWidth: 80
                text: pane.controller ? String(pane.controller.rememberedPort) : "22"
                enabled: pane.controller && !pane.controller.busy
                validator: IntValidator { bottom: 1; top: 65535 }
            }
            TextField { id: userInput; Layout.fillWidth: true; placeholderText: "Utilisateur"; text: pane.controller ? pane.controller.rememberedUser : ""; enabled: pane.controller && !pane.controller.busy }
            TextField { id: passwordInput; Layout.preferredWidth: 160; echoMode: TextInput.Password; placeholderText: "Mot de passe"; enabled: pane.controller && !pane.controller.busy }
        }
        RowLayout {
            Button {
                text: "Se connecter"
                highlighted: true
                enabled: pane.controller && !pane.controller.busy && hostInput.text.trim().length > 0
                         && userInput.text.trim().length > 0 && passwordInput.text.length > 0
                onClicked: pane.controller.connectServer(hostInput.text, Number(portInput.text), userInput.text, passwordInput.text)
            }
            BusyIndicator { running: pane.controller && pane.controller.busy; visible: running; implicitWidth: 26; implicitHeight: 26 }
        }
        Text {
            text: pane.controller ? pane.controller.statusText : "Contrôleur SSH indisponible."
            color: "#5A6478"; font.pixelSize: 11; wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Rectangle {
            visible: pane.pending
            Layout.fillWidth: true
            implicitHeight: trust.implicitHeight + 20
            radius: 10; color: "#FFF5E0"; border.color: "#E0B25A"
            ColumnLayout {
                id: trust
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
                Text { text: "Première connexion : vérifiez l'empreinte de l'hôte"; font.bold: true; color: "#6B4A12"; font.pixelSize: 12 }
                Text { text: pane.pending ? pane.controller.pendingFingerprint : ""; font.family: "monospace"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; font.pixelSize: 11 }
                Text { text: "Comparez-la avec la console du serveur ou un autre canal de confiance."; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11; color: "#6B4A12" }
                Button { text: "Vérifiée — faire confiance à cet hôte"; enabled: pane.pending && !pane.controller.busy; onClicked: pane.controller.trustHost() }
            }
        }
    }

    Connections {
        target: pane.controller
        function onChanged() {
            if (pane.controller.connected && passwordInput.text.length > 0) passwordInput.text = ""
        }
    }
}
