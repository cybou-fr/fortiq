import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

// Login screen (owner decision 23.10.2026): connecting to a server works like
// logging in to a Linux desktop. It covers the whole shell while there is no
// session; the saved servers are shown as account tiles, choosing one asks for
// the SSH password (or the key passphrase), and "Se déconnecter" in the
// session menu returns here. Only bootstrap data is saved on this device
// (address, user, key path, verified host-key pin); passwords and passphrases
// stay in memory for the attempt and are never written. First-use host-key
// verification is never bypassed.
Item {
    id: mgr
    objectName: "serverManager"
    property bool adaptive: false
    property Item focusedInput: null
    property Item backdrop
    property var controller: null
    property url wallpaperSource: ""
    property string timeText: ""
    property string dateText: ""

    readonly property bool connected: !!controller && controller.connected
    readonly property bool busy: !!controller && controller.busy
    readonly property var servers: controller ? controller.servers : []
    property string editingId: ""      // "" = login, "new" = add form, else the id being edited
    property string selectedId: ""     // account tile whose prompt is open
    property string confirmRemoveId: ""
    property string secret: ""          // RAM only, cleared after use
    readonly property var selected: {
        for (const p of servers) if (p.id === selectedId) return p
        return null
    }

    // Returns true when it consumed Back: the login screen itself cannot be left.
    function back() {
        if (editingId.length > 0 && servers.length > 0) editingId = ""
        else if (selectedId.length > 0 && servers.length > 1) selectedId = ""
        return true
    }
    function pickDefault() {
        if (selected) return
        selectedId = ""
        for (const p of servers) if (p.active) { selectedId = p.id; return }
        if (servers.length > 0) selectedId = servers[0].id
    }
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
        selectedId = id
    }
    function connect(p) {
        controller.connectTo(p.id, secret)
        secret = ""
        secretField.text = ""
    }
    function initial(p) { return (p.name && p.name.length > 0 ? p.name : p.user).charAt(0).toUpperCase() }
    function tint(p) {
        const palette = ["#3B82F6", "#8B5CF6", "#14B8A6", "#F59E0B", "#EC4899", "#22C55E"]
        let h = 0
        for (const c of p.id + p.host) h = (h * 31 + c.charCodeAt(0)) & 0xffff
        return palette[h % palette.length]
    }

    onServersChanged: pickDefault()
    onVisibleChanged: if (visible) { pickDefault(); secretField.text = ""; secret = "" }
    Component.onCompleted: { pickDefault(); if (servers.length === 0 && controller) startEdit(null) }
    Connections {
        target: mgr.controller
        function onChanged() {
            if (mgr.connected) { secretField.text = ""; mgr.secret = "" }
        }
    }

    // Opaque: the login screen replaces the desktop instead of floating over it.
    Wallpaper { anchors.fill: parent; source: mgr.wallpaperSource }
    Rectangle { anchors.fill: parent; color: "#B3081020" }
    MouseArea { anchors.fill: parent; onClicked: mgr.forceActiveFocus() }

    Flickable {
        id: flick
        function reveal(item) {
            if (!item) return
            const top = item.mapToItem(contentItem, 0, 0).y
            if (top < contentY) contentY = Math.max(0, top - 12)
            else if (top + item.height > contentY + height) contentY = Math.max(0, Math.min(contentHeight - height, top + item.height - height + 12))
        }
        onHeightChanged: Qt.callLater(() => reveal(mgr.focusedInput))
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, page.implicitHeight + 48)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: page
            width: Math.min(flick.width - 32, 720)
            x: (flick.width - width) / 2
            y: Math.max(24, (flick.height - implicitHeight) / 2 - (mgr.adaptive ? 0 : 20))
            spacing: 22

            // Clock, like a greeter's lock screen.
            Column {
                width: parent.width; spacing: 2
                visible: mgr.editingId === "" && !(mgr.adaptive && Qt.inputMethod.visible)
                Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: mgr.timeText; color: "white"; font.pixelSize: mgr.adaptive ? 56 : 80; font.weight: Font.Light; font.family: Theme.fontFamily }
                Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: mgr.dateText; color: Theme.ink; font.pixelSize: 18; font.family: Theme.fontFamily }
            }

            // Account tiles.
            Flow {
                visible: mgr.editingId === ""
                width: parent.width; spacing: 14
                // centre the rows
                property real rowWidth: Math.min(width, (mgr.servers.length + 1) * 164 - 14)
                x: 0
                leftPadding: Math.max(0, (width - rowWidth) / 2)
                Repeater {
                    model: mgr.servers
                    Item {
                        id: tile
                        objectName: "loginTile_" + modelData.id
                        required property var modelData
                        readonly property bool current: mgr.selectedId === modelData.id
                        width: 150; height: 156
                        Rectangle {
                            anchors.fill: parent; radius: 16
                            color: tile.current ? "#2E2F8CFF" : (tm.containsMouse ? "#18FFFFFF" : "transparent")
                            border.width: tile.current ? 1 : 0; border.color: "#802F8CFF"
                        }
                        Rectangle {
                            x: (parent.width - 84) / 2; y: 14; width: 84; height: 84; radius: 42
                            color: mgr.tint(tile.modelData)
                            border.width: 3; border.color: tile.modelData.active ? "#4ADE80" : "#55FFFFFF"
                            Text { anchors.centerIn: parent; text: mgr.initial(tile.modelData); color: "white"; font.pixelSize: 38; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
                        }
                        Text {
                            x: 6; y: 106; width: parent.width - 12; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                            text: tile.modelData.name; color: "white"; font.pixelSize: 14; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                        }
                        Text {
                            x: 6; y: 128; width: parent.width - 12; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
                            text: tile.modelData.user + "@" + tile.modelData.host + (tile.modelData.port !== 22 ? ":" + tile.modelData.port : "")
                            color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
                        }
                        MouseArea {
                            id: tm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                mgr.selectedId = tile.modelData.id; mgr.confirmRemoveId = ""; secretField.text = ""; mgr.secret = ""
                                // A key account connects at once (no passphrase); if the key
                                // needs one, the error appears and the prompt takes it.
                                if (tile.modelData.keyPath && !mgr.busy) mgr.connect(tile.modelData)
                                else secretField.forceActiveFocus()
                            }
                        }
                    }
                }
                Item {
                    width: 150; height: 156
                    Rectangle { anchors.fill: parent; radius: 16; color: addMouse.containsMouse ? "#18FFFFFF" : "transparent" }
                    Rectangle {
                        x: (parent.width - 84) / 2; y: 14; width: 84; height: 84; radius: 42
                        color: "#1FFFFFFF"; border.width: 2; border.color: "#55FFFFFF"
                        Text { anchors.centerIn: parent; text: "+"; color: "white"; font.pixelSize: 40; font.weight: Font.Light }
                    }
                    Text {
                        x: 6; y: 106; width: parent.width - 12; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                        text: "Ajouter un serveur"; color: "white"; font.pixelSize: 14; font.family: Theme.fontFamily
                    }
                    MouseArea { id: addMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: mgr.startEdit(null) }
                }
            }

            // Password prompt for the chosen account.
            Column {
                visible: mgr.editingId === "" && mgr.selected !== null
                width: parent.width; spacing: 10
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    Rectangle {
                        width: Math.min(320, page.width - 60); height: mgr.adaptive ? 48 : 40; radius: 20
                        color: "#590A1224"; border.width: 1; border.color: secretField.activeFocus ? Theme.accent : "#66FFFFFF"
                        TextInput {
                            id: secretField
                            objectName: "loginSecret"
                            anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18
                            verticalAlignment: TextInput.AlignVCenter
                            echoMode: TextInput.Password
                            enabled: !mgr.busy
                            color: "white"; font.pixelSize: 14; font.family: Theme.fontFamily
                            onTextEdited: mgr.secret = text
                            onAccepted: if (mgr.selected) mgr.connect(mgr.selected)
                            onActiveFocusChanged: if (activeFocus) { mgr.focusedInput = parent; Qt.callLater(() => flick.reveal(parent)) }
                            Text {
                                visible: parent.text.length === 0
                                anchors.verticalCenter: parent.verticalCenter
                                text: mgr.selected && mgr.selected.keyPath ? "Phrase de passe de la clé (si demandée)" : "Mot de passe SSH"
                                color: "#9FB1CE"; font.pixelSize: 14; font.family: Theme.fontFamily
                            }
                        }
                    }
                    Rectangle {
                        objectName: "loginSubmit"
                        width: mgr.adaptive ? 48 : 40; height: width; radius: width / 2
                        color: sm.containsMouse ? Theme.selection : "#CC2D6FE0"
                        opacity: mgr.busy ? 0.5 : 1
                        Text { anchors.centerIn: parent; text: mgr.busy ? "…" : "→"; color: "white"; font.pixelSize: 20 }
                        MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; enabled: !mgr.busy; onClicked: if (mgr.selected) mgr.connect(mgr.selected) }
                    }
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                    text: mgr.selected ? (mgr.selected.keyPath ? "Connexion par clé " + mgr.selected.keyPath.split(/[\\/]/).pop() : "Connexion par mot de passe")
                                         + "  ·  " + (mgr.selected.pinned ? "empreinte vérifiée" : "empreinte à vérifier") : ""
                    color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily; elide: Text.ElideMiddle
                }

                // Host key verification (first use) — never bypassed.
                Rectangle {
                    visible: mgr.controller && mgr.controller.pendingFingerprint.length > 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(520, parent.width); height: trust.implicitHeight + 24; radius: 12
                    color: "#33FACC15"; border.width: 1; border.color: "#80FACC15"
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
                            height: 32; radius: 8; width: tl.implicitWidth + 24
                            color: tm2.containsMouse ? "#CA8A04" : "#A16207"
                            opacity: mgr.busy ? 0.5 : 1
                            Text { id: tl; anchors.centerIn: parent; text: "Vérifiée — faire confiance à cet hôte"; color: "white"; font.pixelSize: 12; font.family: Theme.fontFamily }
                            MouseArea { id: tm2; anchors.fill: parent; hoverEnabled: true; enabled: !mgr.busy; onClicked: mgr.controller.trustHost() }
                        }
                    }
                }

                // Status / errors of the last attempt
                Text {
                    visible: mgr.controller && !mgr.connected && mgr.controller.statusText.length > 0 && mgr.controller.statusText !== "No Linux server connected."
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                    text: mgr.controller ? (mgr.busy ? "Connexion… " : "") + mgr.controller.statusText : ""
                    color: mgr.busy ? Theme.inkMuted : "#FCA5A5"; font.pixelSize: 12; font.family: Theme.fontFamily
                }

                // Edit / remove the chosen account.
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter; spacing: 10
                    Rectangle {
                        height: mgr.adaptive ? 40 : 28; radius: 14; width: el.implicitWidth + 24
                        color: em.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
                        Text { id: el; anchors.centerIn: parent; text: "✎  Modifier"; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                        MouseArea { id: em; anchors.fill: parent; hoverEnabled: true; onClicked: mgr.startEdit(mgr.selected) }
                    }
                    Rectangle {
                        height: mgr.adaptive ? 40 : 28; radius: 14; width: rl.implicitWidth + 24
                        color: rm.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
                        Text {
                            id: rl; anchors.centerIn: parent; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily
                            text: mgr.selected && mgr.confirmRemoveId === mgr.selected.id ? "✓  Confirmer le retrait de la liste" : "🗑  Retirer de la liste"
                        }
                        MouseArea {
                            id: rm; anchors.fill: parent; hoverEnabled: true
                            onClicked: {
                                if (!mgr.selected) return
                                if (mgr.confirmRemoveId === mgr.selected.id) { const id = mgr.selected.id; mgr.confirmRemoveId = ""; mgr.selectedId = ""; mgr.controller.removeServer(id) }
                                else mgr.confirmRemoveId = mgr.selected.id
                            }
                        }
                    }
                }
            }

            Text {
                visible: mgr.editingId === "" && mgr.servers.length === 0
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                text: "Aucun serveur enregistré. Ajoutez votre serveur Linux pour commencer."
                color: Theme.ink; font.pixelSize: 14; font.family: Theme.fontFamily
            }

            // Add / edit form
            Item {
                id: formCard
                visible: mgr.editingId !== ""
                width: parent.width; height: form.implicitHeight + 40
                Glass { anchors.fill: parent; backdrop: mgr.backdrop; radius: 18; tint: "#D9101A2E"; blurAmount: 0.9; blurEnabled: false }
                Column {
                    id: form
                    x: 20; y: 20; width: parent.width - 40
                    spacing: 8
                    Text { text: mgr.editingId === "new" ? "Nouveau serveur" : "Modifier le serveur"; color: Theme.ink; font.pixelSize: 16; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap
                        text: "Une connexion SSH sert toutes les applications : Fichiers, Journaux… Seuls l'adresse, l'utilisateur, le chemin de la clé et l'empreinte vérifiée de l'hôte sont mémorisés sur cet appareil ; les mots de passe ne le sont jamais."
                        color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
                    }
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
                            onActiveFocusChanged: if (activeFocus) { mgr.focusedInput = parent; Qt.callLater(() => flick.reveal(parent)) }
                            onAccepted: mgr.saveEdit()
                            Text { visible: parent.text.length === 0; text: parent.parent.hint; color: "#5F7395"; font.pixelSize: 13; font.family: Theme.fontFamily }
                        }
                    }
                    Row {
                        spacing: 8
                        Field { id: hostField; width: form.width - 108; label: "ADRESSE"; hint: "serveur.exemple.fr ou 192.0.2.10" }
                        Field { id: portField; width: 100; label: "PORT"; hint: "22"; input.validator: IntValidator { bottom: 1; top: 65535 } }
                    }
                    Row {
                        spacing: 8
                        Field { id: userField; width: (form.width - 8) / 2; label: "UTILISATEUR"; hint: "debian" }
                        Field { id: nameField; width: (form.width - 8) / 2; label: "NOM AFFICHÉ (OPTION)"; hint: "Mon VPS" }
                    }
                    Row {
                        spacing: 8
                        Field { id: keyField; width: form.width - 108; label: "CLÉ PRIVÉE OPENSSH (VIDE = MOT DE PASSE)"; hint: Qt.platform.os === "android" ? "Choisir via Parcourir…" : "C:/Users/vous/.ssh/id_ed25519" }
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
                            color: sm2.containsMouse ? Theme.selection : "#992D6FE0"
                            Text { id: sl; anchors.centerIn: parent; text: "Enregistrer"; color: "white"; font.pixelSize: 12; font.family: Theme.fontFamily }
                            MouseArea { id: sm2; anchors.fill: parent; hoverEnabled: true; onClicked: mgr.saveEdit() }
                        }
                        Rectangle {
                            visible: mgr.servers.length > 0
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
    Keys.onEscapePressed: back()
}
