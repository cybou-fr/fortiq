import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

// Global server manager (FORTIQ-wide, not per app): saved servers, connect /
// disconnect, add / edit / remove, first-use host-key verification. Only
// bootstrap data is saved on this device; passwords and passphrases stay in
// memory for the session and are never written.
Item {
    id: mgr
    objectName: "serverManager"
    property bool adaptive: false
    property Item focusedInput: null
    function back() {
        if (editingId.length > 0) editingId = ""
        else { secret = ""; connectingId = ""; closed() }
    }
    property Item backdrop
    property var controller: null
    signal closed()

    readonly property bool connected: controller !== null && controller.connected
    readonly property bool busy: controller !== null && controller.busy
    readonly property var servers: controller ? controller.servers : []
    property string editingId: ""      // "" = list, "new" = add form, else id being edited
    property string connectingId: ""   // row whose secret field is open
    property string confirmRemoveId: ""
    property string secret: ""          // RAM only, cleared after use

    function startEdit(p) {
        editingId = p ? p.id : "new"
        nameField.text = p ? p.name : ""
        hostField.text = p ? p.host : ""
        portField.text = p ? String(p.port) : "22"
        userField.text = p ? p.user : ""
        keyField.text = p ? p.keyPath : ""
        formError.text = ""
        hostField.forceActiveFocus()
    }
    function saveEdit() {
        const id = controller.saveServer({ id: editingId === "new" ? "" : editingId, name: nameField.text, host: hostField.text,
                                           port: Number(portField.text), user: userField.text, keyPath: keyField.text })
        if (id.length === 0) { formError.text = "Adresse, port (1–65535) et utilisateur sont obligatoires, sans espaces."; return }
        editingId = ""
    }
    function connect(p) {
        controller.connectTo(p.id, secret)
        secret = ""
    }

    // Close once connected from here (the manager opened for a purpose).
    Connections {
        target: mgr.controller
        function onChanged() {
            if (mgr.visible && mgr.connected && mgr.connectingId.length > 0 && mgr.controller.pendingFingerprint.length === 0) {
                mgr.connectingId = ""
                mgr.closed()
            }
        }
    }

    Rectangle { anchors.fill: parent; color: "#80060C18"; MouseArea { anchors.fill: parent; onClicked: mgr.closed() } }

    Item {
        id: card
        width: Math.min(640, parent.width - 16); height: Math.min(parent.height - (mgr.adaptive ? 16 : 120), column.implicitHeight + 40)
        anchors.centerIn: parent
        Glass { anchors.fill: parent; backdrop: mgr.backdrop; radius: 18; tint: "#D9101A2E"; blurAmount: 0.9 }
        MouseArea { anchors.fill: parent } // keep clicks inside the card

        Flickable {
            id: formFlick
            function reveal(item) {
                if (!item) return
                const top = item.mapToItem(contentItem, 0, 0).y
                if (top < contentY) contentY = Math.max(0, top - 12)
                else if (top + item.height > contentY + height) contentY = Math.max(0, Math.min(contentHeight - height, top + item.height - height + 12))
            }
            onHeightChanged: Qt.callLater(() => reveal(mgr.focusedInput))
            anchors.fill: parent; anchors.margins: 20
            contentHeight: column.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: column
                width: parent.width
                spacing: 12

                Item {
                    width: parent.width; height: 30
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "Serveurs"; color: Theme.ink; font.pixelSize: 18; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
                    ToolButtonGlass { touch: mgr.adaptive; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; glyph: "×"; tip: "Fermer (Échap)"; onClicked: mgr.closed() }
                }
                Text {
                    width: parent.width; wrapMode: Text.WordWrap
                    text: "Une connexion SSH sert toutes les applications : Fichiers, Journaux… Seuls l'adresse, l'utilisateur, le chemin de la clé et l'empreinte vérifiée de l'hôte sont mémorisés sur cet appareil ; les mots de passe ne le sont jamais."
                    color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
                }

                // Current connection
                Rectangle {
                    visible: mgr.connected
                    width: parent.width; height: 44; radius: 10
                    color: "#1A16A34A"; border.width: 1; border.color: "#5516A34A"
                    Rectangle { x: 14; anchors.verticalCenter: parent.verticalCenter; width: 9; height: 9; radius: 4.5; color: "#4ADE80" }
                    Text {
                        x: 32; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 180; elide: Text.ElideRight
                        text: "Connecté à " + (mgr.controller ? mgr.controller.serverLabel : "")
                        color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily
                    }
                    Rectangle {
                        anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                        height: 28; radius: 7; width: dl.implicitWidth + 20
                        color: dm.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
                        Text { id: dl; anchors.centerIn: parent; text: "Se déconnecter"; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                        MouseArea { id: dm; anchors.fill: parent; hoverEnabled: true; onClicked: mgr.controller.disconnectServer() }
                    }
                }

                // Host key verification (first use) — never bypassed.
                Rectangle {
                    visible: mgr.controller && mgr.controller.pendingFingerprint.length > 0
                    width: parent.width; height: trust.implicitHeight + 24; radius: 10
                    color: "#26FACC15"; border.width: 1; border.color: "#80FACC15"
                    Column {
                        id: trust
                        x: 14; y: 12; width: parent.width - 28; spacing: 8
                        Text { text: "Première connexion : vérifiez l'empreinte de l'hôte"; color: "#FDE68A"; font.pixelSize: 13; font.bold: true; font.family: Theme.fontFamily }
                        TextEdit {
                            width: parent.width; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                            text: mgr.controller ? mgr.controller.pendingFingerprint : ""
                            color: "white"; font.family: "Consolas"; font.pixelSize: 13
                        }
                        Text {
                            width: parent.width; wrapMode: Text.WordWrap
                            text: "Comparez-la avec la console du serveur (ssh-keygen -lf /etc/ssh/ssh_host_*_key.pub -E sha256) ou un autre canal de confiance. N'acceptez pas une empreinte inattendue."
                            color: "#FEF3C7"; font.pixelSize: 11; font.family: Theme.fontFamily
                        }
                        Rectangle {
                            height: 30; radius: 7; width: tl.implicitWidth + 24
                            color: tm.containsMouse ? "#CA8A04" : "#A16207"
                            opacity: mgr.busy ? 0.5 : 1
                            Text { id: tl; anchors.centerIn: parent; text: "Vérifiée — faire confiance à cet hôte"; color: "white"; font.pixelSize: 12; font.family: Theme.fontFamily }
                            MouseArea { id: tm; anchors.fill: parent; hoverEnabled: true; enabled: !mgr.busy; onClicked: mgr.controller.trustHost() }
                        }
                    }
                }

                // Status / errors of the last attempt
                Text {
                    visible: mgr.controller && !mgr.connected && mgr.controller.statusText.length > 0 && mgr.controller.statusText !== "No Linux server connected."
                    width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                    text: mgr.controller ? (mgr.busy ? "Connexion… " : "") + mgr.controller.statusText : ""
                    color: mgr.busy ? Theme.inkMuted : "#FCA5A5"; font.pixelSize: 11; font.family: Theme.fontFamily
                }

                // Saved servers
                Text {
                    visible: mgr.servers.length === 0 && mgr.editingId === ""
                    text: "Aucun serveur enregistré. Ajoutez votre serveur Linux pour commencer."
                    color: Theme.ink; font.pixelSize: 13; font.family: Theme.fontFamily
                }
                Repeater {
                    model: mgr.editingId === "" ? mgr.servers : []
                    Rectangle {
                        id: row
                        required property var modelData
                        readonly property bool open: mgr.connectingId === modelData.id
                        width: column.width
                        height: open ? 104 : 56
                        radius: 10
                        color: modelData.active ? "#262F8CFF" : (rm.containsMouse ? "#14FFFFFF" : "#0AFFFFFF")
                        border.width: 1; border.color: modelData.active ? "#802F8CFF" : "#1AFFFFFF"
                        MouseArea { id: rm; anchors.fill: parent; hoverEnabled: true }
                        Rectangle { x: 14; y: 16; width: 9; height: 9; radius: 4.5; color: row.modelData.active ? "#4ADE80" : "#64748B" }
                        Column {
                            x: 32; y: 9; width: parent.width - 32 - actions.width - 16
                            Text { width: parent.width; elide: Text.ElideRight; text: row.modelData.name; color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
                            Text {
                                width: parent.width; elide: Text.ElideMiddle
                                text: row.modelData.user + "@" + row.modelData.host + (row.modelData.port !== 22 ? ":" + row.modelData.port : "")
                                      + "  ·  " + (row.modelData.keyPath ? "clé " + row.modelData.keyPath.split(/[\\/]/).pop() : "mot de passe")
                                      + "  ·  " + (row.modelData.pinned ? "empreinte vérifiée" : "empreinte à vérifier")
                                color: Theme.inkMuted; font.pixelSize: 10; font.family: Theme.fontFamily
                            }
                        }
                        Row {
                            id: actions
                            anchors.right: parent.right; anchors.rightMargin: 10; y: 13
                            spacing: 6
                            Rectangle {
                                visible: !row.modelData.active
                                height: 28; radius: 7; width: cl.implicitWidth + 20
                                color: cm.containsMouse ? Theme.selection : "#992D6FE0"
                                opacity: mgr.busy ? 0.5 : 1
                                Text { id: cl; anchors.centerIn: parent; text: "Se connecter"; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                                MouseArea {
                                    id: cm; anchors.fill: parent; hoverEnabled: true; enabled: !mgr.busy
                                    onClicked: {
                                        mgr.connectingId = row.modelData.id
                                        mgr.secret = ""
                                        secretField.text = ""
                                        // A key without passphrase connects at once.
                                        if (row.modelData.keyPath) mgr.connect(row.modelData)
                                        else secretField.forceActiveFocus()
                                    }
                                }
                            }
                            ToolButtonGlass { touch: mgr.adaptive; glyph: "✎"; tip: "Modifier"; enabledState: !row.modelData.active; onClicked: mgr.startEdit(row.modelData) }
                            ToolButtonGlass { touch: mgr.adaptive;
                                glyph: mgr.confirmRemoveId === row.modelData.id ? "✓" : "🗑"
                                tip: mgr.confirmRemoveId === row.modelData.id ? "Confirmer la suppression de ce serveur de la liste" : "Retirer de la liste"
                                onClicked: {
                                    if (mgr.confirmRemoveId === row.modelData.id) { mgr.controller.removeServer(row.modelData.id); mgr.confirmRemoveId = "" }
                                    else mgr.confirmRemoveId = row.modelData.id
                                }
                            }
                        }
                        Row {
                            visible: row.open
                            x: 32; y: 60; spacing: 8
                            Rectangle {
                                width: 300; height: mgr.adaptive ? 44 : 32; radius: 8; color: "#1AFFFFFF"
                                border.width: 1; border.color: secretField.activeFocus ? Theme.accent : "#33FFFFFF"
                                TextInput {
                                    id: secretField
                                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                                    verticalAlignment: TextInput.AlignVCenter
                                    echoMode: TextInput.Password
                                    color: Theme.ink; font.pixelSize: 12
                                    onTextEdited: mgr.secret = text
                                    onAccepted: if (row.open) { mgr.connect(row.modelData); text = "" }
                                    Text {
                                        visible: parent.text.length === 0
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: row.modelData.keyPath ? "Phrase de passe de la clé (si demandée)" : "Mot de passe SSH (non enregistré)"
                                        color: "#7F93B5"; font.pixelSize: 12; font.family: Theme.fontFamily
                                    }
                                }
                            }
                            Rectangle {
                                height: mgr.adaptive ? 44 : 32; radius: 8; width: gl.implicitWidth + 24
                                color: gm.containsMouse ? Theme.selection : "#992D6FE0"
                                opacity: mgr.busy ? 0.5 : 1
                                Text { id: gl; anchors.centerIn: parent; text: mgr.busy ? "Connexion…" : "Connexion"; color: "white"; font.pixelSize: 12; font.family: Theme.fontFamily }
                                MouseArea { id: gm; anchors.fill: parent; hoverEnabled: true; enabled: !mgr.busy; onClicked: { mgr.connect(row.modelData); secretField.text = "" } }
                            }
                        }
                    }
                }

                Rectangle {
                    visible: mgr.editingId === ""
                    height: mgr.adaptive ? 44 : 32; radius: 8; width: al.implicitWidth + 26
                    color: am.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
                    Text { id: al; anchors.centerIn: parent; text: "+  Ajouter un serveur"; color: "white"; font.pixelSize: 12; font.family: Theme.fontFamily }
                    MouseArea { id: am; anchors.fill: parent; hoverEnabled: true; onClicked: mgr.startEdit(null) }
                }

                // Add / edit form
                Column {
                    visible: mgr.editingId !== ""
                    width: parent.width
                    spacing: 8
                    Text { text: mgr.editingId === "new" ? "Nouveau serveur" : "Modifier le serveur"; color: Theme.ink; font.pixelSize: 14; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
                    component Field: Rectangle {
                        property alias text: input.text
                        property alias input: input
                        property string label: ""
                        property string hint: ""
                        height: 46; radius: 8; color: "#14FFFFFF"
                        border.width: 1; border.color: input.activeFocus ? Theme.accent : "#2BFFFFFF"
                        Text { x: 10; y: 5; width: parent.width - 20; elide: Text.ElideRight; text: parent.label; color: "#8FA2C2"; font.pixelSize: 9; font.family: Theme.fontFamily }
                        TextInput {
                            id: input
                            x: 10; y: 20; width: parent.width - 20
                            color: Theme.ink; font.pixelSize: 13; font.family: Theme.fontFamily; clip: true
                            selectByMouse: true
                            onActiveFocusChanged: if (activeFocus) { mgr.focusedInput = parent; Qt.callLater(() => formFlick.reveal(parent)) }
                            onAccepted: mgr.saveEdit()
                            Text { visible: parent.text.length === 0; text: parent.parent.hint; color: "#5F7395"; font.pixelSize: 13; font.family: Theme.fontFamily }
                        }
                    }
                    Row {
                        spacing: 8
                        Field { id: hostField; width: column.width - 108; label: "ADRESSE"; hint: "serveur.exemple.fr ou 192.0.2.10" }
                        Field { id: portField; width: 100; label: "PORT"; hint: "22"; input.validator: IntValidator { bottom: 1; top: 65535 } }
                    }
                    Row {
                        spacing: 8
                        Field { id: userField; width: (column.width - 8) / 2; label: "UTILISATEUR"; hint: "debian" }
                        Field { id: nameField; width: (column.width - 8) / 2; label: "NOM AFFICHÉ (OPTION)"; hint: "Mon VPS" }
                    }
                    Row {
                        spacing: 8
                        Field { id: keyField; width: column.width - 108; label: "CLÉ PRIVÉE OPENSSH (VIDE = MOT DE PASSE)"; hint: Qt.platform.os === "android" ? "Choisir via Parcourir…" : "C:/Users/vous/.ssh/id_ed25519" }
                        Rectangle {
                            width: 100; height: 46; radius: 8; color: bm.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
                            Text { anchors.centerIn: parent; text: "Parcourir…"; color: "white"; font.pixelSize: 12; font.family: Theme.fontFamily }
                            MouseArea { id: bm; anchors.fill: parent; hoverEnabled: true; onClicked: keyDialog.open() }
                        }
                    }
                    Text { id: formError; visible: text.length > 0; width: parent.width; wrapMode: Text.WordWrap; color: "#FCA5A5"; font.pixelSize: 11; font.family: Theme.fontFamily }
                    Row {
                        spacing: 8
                        Rectangle {
                            height: mgr.adaptive ? 44 : 32; radius: 8; width: sl.implicitWidth + 26
                            color: sm.containsMouse ? Theme.selection : "#992D6FE0"
                            Text { id: sl; anchors.centerIn: parent; text: "Enregistrer"; color: "white"; font.pixelSize: 12; font.family: Theme.fontFamily }
                            MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; onClicked: mgr.saveEdit() }
                        }
                        Rectangle {
                            height: mgr.adaptive ? 44 : 32; radius: 8; width: xl.implicitWidth + 26
                            color: xm.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
                            Text { id: xl; anchors.centerIn: parent; text: "Annuler"; color: "white"; font.pixelSize: 12; font.family: Theme.fontFamily }
                            MouseArea { id: xm; anchors.fill: parent; hoverEnabled: true; onClicked: mgr.editingId = "" }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: keyDialog
        title: "Choisir la clé privée OpenSSH"
        fileMode: FileDialog.OpenFile
        onAccepted: {
            // Keep SAF document identifiers opaque, including encoded slashes.
            keyField.text = selectedFile.toString()
        }
    }

    focus: visible
    Keys.onEscapePressed: if (editingId !== "") editingId = ""; else closed()
}
