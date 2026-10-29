import QtQuick
import QtQuick.Controls.Fusion
import QtCore
import QtQuick.Dialogs

// Courrier (lecture et étiquettes): interface graphique pour notmuch sur le serveur.
// Envoi via msmtp et fichiers joints sur le serveur sélectionné (docs/MAIL.md).
Item {
    id: win
    objectName: "mailWindow"
    property bool adaptive: false
    property Item backdrop
    property var mailer: null
    property var remote: null
    property bool navigationOpen: false
    property bool showHtml: false
    property string hoveredLink: ""
    property bool composeOpen: false
    property bool composeActive: false
    property var pendingQuit:null
    readonly property bool hasUnsentDraft:composeActive && !composeSending && (toField.text.length>0 || ccField.text.length>0 || bccField.text.length>0 || subjectField.text.length>0 || bodyField.text.length>0 || (mailer!==null && mailer.outgoingAttachments.length>0))
    property string composeServerId: ""
    property string composeSourceLabel: ""
    readonly property bool composeSending:mailer!==null && mailer.sendState==="sending"
    property int composeGeneration: -1
    readonly property bool composeSourceAvailable: connected && remote.activeServerId===composeServerId && remote.connectionGeneration===composeGeneration
    property string composeReplyTo: ""
    property string composeError: ""
    property bool wizardOpen: false
    property string wizardError: ""
    property var wizardKeys: ({})
    property string wizardServerId: ""
    property bool previewPending: false
    property string dlMessage: ""
    property int dlPart: 0
    property string dlName: ""
    readonly property bool connected: remote !== null && remote.connected
    readonly property bool drawer: adaptive && width < 900
    readonly property real contentLeft: drawer ? 0 : 196
    readonly property real contentWidth: width - contentLeft
    readonly property bool stacked: contentWidth < 700            // list and reader replace each other
    readonly property bool readerShown: mailer !== null && mailer.selectedThreadId !== ""
    readonly property real listWidth: stacked ? contentWidth : Math.min(360, Math.max(260, contentWidth * 0.38))
    readonly property var currentThread: {
        if (!mailer || mailer.selectedThreadId === "") return null
        for (const t of mailer.threads) if (t.id === mailer.selectedThreadId) return t
        return null
    }
    signal closeRequested()
    signal requestServers()
    signal requestAttachmentFiles(string serverId, int generation)

    function sync() { if (mailer) mailer.setWindowActive(visible) }
    onVisibleChanged: { sync(); if (!visible && wizardOpen) closeWizard() }
    onMailerChanged: sync()
    Component.onCompleted: sync()
    function handleBack() {
        if(discardDialog.visible){discardDialog.visible=false;pendingQuit=null;return true}
        if (wizardOpen) { closeWizard(); return true }
        if (composeOpen) { composeOpen = false; return true }
        if (navigationOpen) { navigationOpen = false; return true }
        if (readerShown && stacked) { mailer.closeThread(); return true }
        return false
    }
    function openCompose(draft) {
        if(composeActive){
            composeOpen=true
            if(draft)composeError="Un brouillon est déjà ouvert. Fermez-le ou abandonnez-le avant de répondre à un autre message."
            return
        }
        if(!connected || !mailer)return
        const d = draft || {}
        toField.text = d.to || ""; ccField.text = ""; bccField.text = ""
        subjectField.text = d.subject || ""; bodyField.text = d.body || ""
        composeReplyTo = d.inReplyTo || ""; composeError = ""
        composeServerId=remote.activeServerId;composeGeneration=remote.connectionGeneration;composeSourceLabel=remote.serverLabel
        composeActive=true;mailer.clearOutgoingAttachments();if(mailer.sendState!=="unknown")mailer.resetSend()
        composeViewport.contentY=0;composeOpen = true
        if (composeReplyTo !== "") { bodyField.cursorPosition = 0; bodyField.forceActiveFocus() } else toField.forceActiveFocus()
    }
    function attachFile(path){
        if(!connected || !mailer)return false
        openCompose(null)
        if(!composeSourceAvailable){composeError="Ce brouillon appartient à une autre session. Son texte est conservé ; abandonnez-le pour en créer un sur ce serveur.";return false}
        composeError=mailer.addOutgoingAttachment(path)
        return composeError.length===0
    }
    function submitCompose() {
        if (!mailer || !composeSourceAvailable) { composeError="La source du brouillon n'est plus disponible. Le texte est conservé.";return }
        const account = mailer.accounts[accountBox.currentIndex]
        composeError = mailer.send({ account: account ? account.name : "", to: toField.text, cc: ccField.text, bcc: bccField.text,
            subject: subjectField.text, body: bodyField.text, inReplyTo: composeReplyTo,
            serverId:composeServerId,connectionGeneration:composeGeneration })
    }
    function requestQuit(continuation){
        pendingQuit=continuation;composeOpen=true
        discardDialog.openWith("Quitter avec un message non envoyé ?","Ce brouillon n'est conservé qu'en mémoire. Vous pouvez revenir au message ou l'abandonner pour quitter.","Abandonner et continuer",true,false,"")
    }
    function discardCompose(){
        if(mailer && mailer.actionBusy)return
        if(mailer)mailer.clearOutgoingAttachments()
        toField.text="";ccField.text="";bccField.text="";subjectField.text="";bodyField.text=""
        composeReplyTo="";composeError="";composeActive=false;composeOpen=false
    }
    function replyToLast() {
        if (!mailer || mailer.messages.length === 0) return
        openCompose(mailer.replyDraft(mailer.messages[mailer.messages.length - 1]))
    }
    function openWizard() {
        if (!mailer) return
        wizardError = ""; wizardOpen = true
        wizardServerId = remote ? remote.activeServerId : ""
        mailer.clearSetup()
        mailer.probeSetup(wName.text.trim())
    }
    // Nothing typed as a secret survives the wizard: closing it, hiding the window, losing the connection or changing the
    // server all clear the passwords, the revealed preview and the consent.
    function clearWizardSecrets() {
        wPassword.text = ""; wSmtpPassword.text = ""
        revealBox.checked = false; consentBox.checked = false
        if (mailer) mailer.clearSetup()
    }
    function closeWizard() {
        clearWizardSecrets()
        wizardOpen = false
    }
    function wizardSpec() {
        return { name: wName.text, displayName: wDisplay.text, email: wEmail.text,
                 imapHost: wImapHost.text, imapPort: parseInt(wImapPort.text), imapSecurity: wImapSec.model[wImapSec.currentIndex].k, imapUser: wImapUser.text,
                 smtpHost: wSmtpHost.text, smtpPort: parseInt(wSmtpPort.text), smtpSecurity: wSmtpSec.model[wSmtpSec.currentIndex].k, smtpUser: wSmtpUser.text,
                 password: wPassword.text, smtpPassword: wSmtpPassword.text }
    }
    function wizardPreview() { wizardError = ""; previewPending = true; mailer.probeSetup(wName.text.trim()) }
    function finishPreview() {
        if (!mailer.setupState.ok) { wizardError = mailer.setupState.problem; return }
        wizardError = mailer.previewSetup(wizardSpec())
    }
    function checkedKeys() {
        const out = []
        for (const k in wizardKeys) if (wizardKeys[k]) out.push(k)
        return out
    }
    function sizeText(bytes) {
        return bytes >= 1048576 ? (bytes / 1048576).toFixed(1) + " Mio" : Math.max(1, Math.round(bytes / 1024)) + " Kio"
    }
    function askDownload(messageId, part, name) {
        dlMessage = messageId; dlPart = part; dlName = name
        downloadDialog.currentFile = StandardPaths.writableLocation(StandardPaths.DownloadLocation) + "/" + mailer.attachmentFileName(name)
        downloadDialog.open()
    }
    function tagOf(thread, tag) { return thread !== null && thread.tags.indexOf(tag) >= 0 }

    Glass { anchors.fill: parent; backdrop: win.backdrop; tint: Theme.appWindow; blurAmount: 0.9; radius: 16 }
    MouseArea { visible: win.drawer && win.navigationOpen; anchors.fill: parent; z: 30; onClicked: win.navigationOpen = false }

    Rectangle {
        objectName: "mailSidebar"
        visible: !win.drawer || win.navigationOpen
        width: win.drawer ? Math.min(260, win.width - 32) : 196; height: parent.height; z: win.drawer ? 40 : 0
        topLeftRadius: 16; bottomLeftRadius: 16; color: win.drawer ? "#F0121C30" : "#40050B18"
        WindowDragArea { anchors.fill: parent; windowItem: win; movable: !win.adaptive }
        Row {
            visible: !win.adaptive; x: 14; y: 17; spacing: 9
            Repeater {
                model: ["#FF5F57", "#FEBC2E", "#28C840"]
                Rectangle {
                    required property string modelData
                    width: 12; height: 12; radius: 6; color: modelData
                    MouseArea { anchors.fill: parent; onClicked: if (parent.modelData === "#FF5F57") win.closeRequested() }
                }
            }
        }
        Text { x: 18; y: 48; text: "COURRIER"; color: Theme.inkMuted; font.pixelSize: 11; font.letterSpacing: 1 }
        Column {
            x: 8; y: 72; width: parent.width - 16; spacing: 4
            Repeater {
                model: win.mailer ? win.mailer.folders : []
                Rectangle {
                    required property var modelData
                    objectName: "mailFolder_" + modelData.key
                    width: parent.width; height: win.adaptive ? 44 : 32; radius: 8
                    color: win.mailer && win.mailer.folderKey === modelData.key ? "#D02D73DA" : (folderMouse.containsMouse ? "#26FFFFFF" : "transparent")
                    Text { x: 12; width: parent.width - 52; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight; text: modelData.title; color: Theme.ink; font.pixelSize: 13 }
                    Text {
                        visible: modelData.count >= 0 && modelData.key !== "all" && modelData.key !== "deleted"
                        anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.count; color: modelData.key === "unread" && modelData.count > 0 ? "#8EC1FF" : Theme.inkMuted; font.pixelSize: 12; font.bold: modelData.key === "unread"
                    }
                    MouseArea { id: folderMouse; anchors.fill: parent; hoverEnabled: true; onClicked: { win.mailer.openFolder(modelData.key); win.navigationOpen = false } }
                }
            }
        }
        Text {
            x: 18; y: parent.height - 96; width: parent.width - 36; wrapMode: Text.WordWrap
            text: "Lecture par notmuch, envoi par msmtp sur le serveur. Les fichiers joints restent sur ce serveur."
            color: Theme.inkMuted; font.pixelSize: 11
        }
    }

    // ── header ─────────────────────────────────────────────────────────
    Item {
        id: header
        x: win.contentLeft; width: parent.width - x; height: 48
        WindowDragArea { anchors.fill: parent; windowItem: win; movable: !win.adaptive }
        ToolButtonGlass { visible: win.drawer; x: 4; y: 2; touch: true; glyph: "☰"; tip: "Menu"; onClicked: win.navigationOpen = !win.navigationOpen }
        ToolButtonGlass { visible: win.stacked && win.readerShown; x: win.drawer ? 50 : 4; y: 2; touch: win.adaptive; glyph: "←"; tip: "Retour à la liste"; onClicked: win.mailer.closeThread() }
        Text {
            x: (win.drawer ? 54 : 14) + (win.stacked && win.readerShown ? 46 : 0); width: Math.max(0, parent.width - x - 60); anchors.verticalCenter: parent.verticalCenter
            text: "Courrier · " + (win.connected ? win.remote.serverLabel : "Aucun serveur"); elide: Text.ElideMiddle; textFormat: Text.PlainText; color: Theme.ink; font.pixelSize: 14
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 2
            ToolButtonGlass {
                objectName: "mailNew"
                touch: win.adaptive; glyph: "✎"; tip: "Nouveau message"
                enabledState: win.connected && win.mailer !== null; onClicked: win.openCompose(null)
            }
            ToolButtonGlass {
                objectName: "mailAccounts"
                touch: win.adaptive; glyph: "⚙"; tip: "Comptes"
                enabledState: win.connected && win.mailer !== null; onClicked: win.openWizard()
            }
            ToolButtonGlass {
                touch: win.adaptive; glyph: "↻"; tip: "Actualiser"
                enabledState: win.connected && win.mailer !== null && !win.mailer.busy; onClicked: win.mailer.refresh()
            }
        }
    }

    // ── thread list ────────────────────────────────────────────────────
    Item {
        id: listPane
        visible: win.connected && win.mailer !== null && win.mailer.state !== "disconnected" && !(win.mailer.state === "error" && win.mailer.folders.length === 0) && !(win.stacked && win.readerShown)
        x: win.contentLeft + 12; y: 52; width: win.listWidth - (win.stacked ? 24 : 6); height: parent.height - y - 10
        Rectangle {
            id: searchBox
            width: parent.width; height: win.adaptive ? 44 : 30; radius: 15
            color: "#1AFFFFFF"; border.width: 1; border.color: searchInput.activeFocus ? Theme.accent : "#1FFFFFFF"
            TextInput {
                id: searchInput
                objectName: "mailSearch"
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; verticalAlignment: TextInput.AlignVCenter
                color: Theme.ink; font.pixelSize: 13; clip: true; selectByMouse: true
                onAccepted: win.mailer.search(text)
                Text { visible: parent.text.length === 0; anchors.verticalCenter: parent.verticalCenter; text: "Rechercher (de:, sujet:, tag:…) puis Entrée"; color: "#7F93B5"; font.pixelSize: 13 }
            }
        }
        Text {
            id: listStatus
            y: searchBox.height + 6; width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText; font.pixelSize: 12
            color: win.mailer && win.mailer.state === "error" ? "#FCA5A5" : Theme.inkMuted
            text: !win.mailer ? "" : win.mailer.state === "error" ? win.mailer.status : (win.mailer.busy ? "Chargement…" : win.mailer.threads.length + " conversation(s)" + (win.mailer.hasMore ? " (liste partielle)" : ""))
        }
        ListView {
            id: threadList
            objectName: "mailThreadList"
            y: listStatus.y + listStatus.height + 6; width: parent.width; height: Math.max(0, parent.height - y)
            clip: true; spacing: 2; model: win.mailer ? win.mailer.threads : []
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property var modelData
                objectName: "mailThread_" + modelData.id
                width: ListView.view.width - 8; height: win.adaptive ? 66 : 56; radius: 8
                color: win.mailer && win.mailer.selectedThreadId === modelData.id ? "#40307CF0" : (rowMouse.containsMouse ? "#14FFFFFF" : "transparent")
                Rectangle { x: 6; y: 12; width: 8; height: 8; radius: 4; color: "#4D9CFF"; visible: modelData.unread }
                Text {
                    x: 22; y: 5; width: parent.width - 100; elide: Text.ElideRight; textFormat: Text.PlainText
                    text: modelData.authors; color: Theme.ink; font.pixelSize: 13; font.bold: modelData.unread
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 8; y: 6; width: 80; horizontalAlignment: Text.AlignRight; textFormat: Text.PlainText
                    text: modelData.when; color: Theme.inkMuted; font.pixelSize: 11
                }
                Text {
                    x: 22; y: 23; width: parent.width - 60; elide: Text.ElideRight; textFormat: Text.PlainText
                    text: modelData.subject + (modelData.total > 1 ? "  (" + modelData.total + ")" : ""); color: Theme.ink; font.pixelSize: 13; font.bold: modelData.unread
                }
                Row {
                    x: 22; y: parent.height - 18; spacing: 8
                    Text { visible: modelData.flagged; text: "★ suivi"; color: "#FCD34D"; font.pixelSize: 11 }
                    Text { visible: modelData.attachment; text: "pièce jointe"; color: Theme.inkMuted; font.pixelSize: 11 }
                }
                MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.mailer.openThread(modelData.id) }
            }
            footer: Item {
                width: ListView.view.width; height: win.mailer && win.mailer.hasMore ? 44 : 0
                Button { visible: win.mailer && win.mailer.hasMore; anchors.centerIn: parent; text: "Plus de conversations"; enabled: win.mailer && !win.mailer.busy; onClicked: win.mailer.loadMore() }
            }
        }
    }

    // ── reader ─────────────────────────────────────────────────────────
    Item {
        id: readerPane
        objectName: "mailReader"
        visible: win.connected && win.readerShown
        x: win.stacked ? win.contentLeft + 12 : win.contentLeft + win.listWidth + 6
        y: 52; width: parent.width - x - 12; height: parent.height - y - 10

        Row {
            id: actions
            spacing: 4
            Rectangle {
                objectName: "mailReply"
                height: win.adaptive ? 44 : 28; radius: 8; width: replyLabel.implicitWidth + 18
                color: replyMouse.containsMouse ? "#33FFFFFF" : "#1AFFFFFF"
                Text { id: replyLabel; anchors.centerIn: parent; text: "Répondre"; color: Theme.ink; font.pixelSize: 12 }
                MouseArea { id: replyMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.replyToLast() }
            }
            Repeater {
                model: [
                    { tag: "unread", on: "Marquer comme lu", off: "Marquer comme non lu" },
                    { tag: "flagged", on: "Retirer le suivi", off: "Suivre ★" },
                    { tag: "inbox", on: "Archiver", off: "Remettre en boîte" },
                    { tag: "deleted", on: "Restaurer", off: "Corbeille" }
                ]
                Rectangle {
                    required property var modelData
                    objectName: "mailTag_" + modelData.tag
                    readonly property bool has: win.tagOf(win.currentThread, modelData.tag)
                    height: win.adaptive ? 44 : 28; radius: 8; width: actionLabel.implicitWidth + 18
                    color: actionMouse.containsMouse ? "#33FFFFFF" : "#1AFFFFFF"
                    Text { id: actionLabel; anchors.centerIn: parent; text: parent.has ? modelData.on : modelData.off; color: Theme.ink; font.pixelSize: 12 }
                    MouseArea { id: actionMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.mailer.setTag(win.mailer.selectedThreadId, modelData.tag, !parent.has) }
                }
            }
        }
        Text {
            id: attachStatusText
            objectName: "mailAttachStatus"
            y: actions.height + 4; width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; font.pixelSize: 12; color: "#8EC1FF"
            visible: win.mailer !== null && win.mailer.attachStatus !== ""; text: win.mailer ? win.mailer.attachStatus : ""
        }
        Text {
            id: threadStatus
            y: actions.height + 6 + (attachStatusText.visible ? attachStatusText.implicitHeight + 4 : 0); width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText; font.pixelSize: 12; color: Theme.inkMuted
            visible: win.mailer && win.mailer.threadStatus !== ""; text: win.mailer ? win.mailer.threadStatus : ""
        }
        ListView {
            id: messageList
            objectName: "mailMessages"
            y: actions.height + 6 + (attachStatusText.visible ? attachStatusText.implicitHeight + 4 : 0) + (threadStatus.visible ? threadStatus.implicitHeight + 6 : 0)
            width: parent.width; height: Math.max(0, parent.height - y - (win.hoveredLink !== "" ? 22 : 0))
            clip: true; spacing: 8; model: win.mailer ? win.mailer.messages : []
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width - 10; x: Math.min(modelData.depth, 3) * 8
                height: card.implicitHeight + 20; radius: 10
                color: "#30182740"; border.width: 1; border.color: modelData.unread ? "#804D9CFF" : "#25DCEBFF"
                opacity: modelData.match ? 1 : 0.8
                Column {
                    id: card
                    readonly property string messageId: modelData.id
                    x: 12; y: 10; width: parent.width - 24; spacing: 4
                    Text { width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: modelData.subject; color: Theme.ink; font.pixelSize: 14; font.bold: true }
                    Text { width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: "De : " + modelData.from; color: Theme.ink; font.pixelSize: 12 }
                    Text { width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: "À : " + modelData.to + (modelData.cc !== "" ? "   Cc : " + modelData.cc : ""); color: Theme.inkMuted; font.pixelSize: 11 }
                    Text { width: parent.width; textFormat: Text.PlainText; text: modelData.date; color: Theme.inkMuted; font.pixelSize: 11 }
                    Text {
                        visible: modelData.attachments.length > 0; width: parent.width; textFormat: Text.PlainText; font.pixelSize: 12; color: "#FCD34D"
                        text: "Pièces jointes (elles restent sur le serveur) :" + (modelData.attachmentsOmitted > 0 ? "  —  " + modelData.attachmentsOmitted + " autres non affichées" : "")
                    }
                    Repeater {
                        model: modelData.attachments
                        Item {
                            required property var modelData
                            required property int index
                            width: card.width; height: win.adaptive ? 48 : 30
                            Text {
                                x: 0; width: parent.width - 232; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideMiddle; textFormat: Text.PlainText
                                text: modelData.name + (modelData.size > 0 ? "  (≈ " + win.sizeText(modelData.size) + ")" : ""); color: Theme.ink; font.pixelSize: 12
                            }
                            Rectangle {
                                objectName: "mailAttachServer_" + index
                                visible: modelData.part > 0
                                anchors.right: attachPc.left; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
                                height: win.adaptive ? 44 : 26; radius: 8; width: serverLabel.implicitWidth + 16
                                color: serverMouse.containsMouse ? "#33FFFFFF" : "#1AFFFFFF"
                                Text { id: serverLabel; anchors.centerIn: parent; text: "Sur le serveur"; color: Theme.ink; font.pixelSize: 12 }
                                MouseArea { id: serverMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.mailer.saveAttachmentToServer(card.messageId, modelData.part, modelData.name) }
                            }
                            Rectangle {
                                id: attachPc
                                objectName: "mailAttachPc_" + index
                                visible: modelData.part > 0
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                height: win.adaptive ? 44 : 26; radius: 8; width: pcLabel.implicitWidth + 16
                                color: pcMouse.containsMouse ? "#33FFFFFF" : "#1AFFFFFF"
                                Text { id: pcLabel; anchors.centerIn: parent; text: "Sur ce poste…"; color: Theme.ink; font.pixelSize: 12 }
                                MouseArea { id: pcMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.askDownload(card.messageId, modelData.part, modelData.name) }
                            }
                        }
                    }
                    Rectangle { width: parent.width; height: 1; color: "#25DCEBFF" }
                    Text {
                        visible: modelData.hasText && !(win.showHtml && modelData.hasHtml)
                        width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: modelData.text; color: Theme.ink; font.pixelSize: 13
                    }
                    Text {
                        visible: !modelData.hasText && modelData.hasHtml || (win.showHtml && modelData.hasHtml)
                        width: parent.width; wrapMode: Text.Wrap; textFormat: Text.RichText; text: modelData.html; color: Theme.ink; font.pixelSize: 13; linkColor: "#8EC1FF"
                        onLinkHovered: (link) => win.hoveredLink = link
                    }
                    Text {
                        visible: modelData.hasHtml
                        width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; font.pixelSize: 11; color: Theme.inkMuted
                        text: "HTML réduit en texte sûr : " + modelData.htmlScripts + " script(s) ignoré(s), " + modelData.htmlImages + " image(s) remplacée(s) par leur texte, aucun contenu distant chargé."
                    }
                    Rectangle {
                        visible: modelData.hasText && modelData.hasHtml
                        objectName: "mailToggleHtml"
                        height: win.adaptive ? 44 : 26; radius: 8; width: toggleLabel.implicitWidth + 18; color: toggleMouse.containsMouse ? "#33FFFFFF" : "#1AFFFFFF"
                        Text { id: toggleLabel; anchors.centerIn: parent; text: win.showHtml ? "Afficher le texte brut" : "Afficher la version HTML"; color: Theme.ink; font.pixelSize: 12 }
                        MouseArea { id: toggleMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.showHtml = !win.showHtml }
                    }
                }
            }
        }
        Text {
            visible: win.hoveredLink !== ""; anchors.bottom: parent.bottom; width: parent.width; elide: Text.ElideMiddle; textFormat: Text.PlainText
            text: "Lien (non ouvert) : " + win.hoveredLink; color: "#8EC1FF"; font.pixelSize: 11
        }
    }
    Text {
        visible: win.connected && win.mailer !== null && !win.readerShown && !win.stacked && win.mailer.state === "ready"
        x: win.contentLeft + win.listWidth + 6 + 20; y: 140; width: parent.width - x - 20; wrapMode: Text.WordWrap
        text: "Choisissez une conversation."; color: Theme.inkMuted; font.pixelSize: 13
    }

    // ── no connection / errors ─────────────────────────────────────────
    NoServerState {
        visible: !win.connected; x: win.contentLeft + 16; y: 150; width: parent.width - x - 16
        touch: win.adaptive; buttonName: "mailConnectButton"; hint: "Le courrier est lu et envoyé par les outils du serveur Linux choisi."
        onChooseServer: win.requestServers()
    }
    AppStateMessage {
        objectName: "mailProblem"
        visible: win.connected && win.mailer !== null && win.mailer.state === "error" && win.mailer.folders.length === 0
        state: "error"; touch: win.adaptive
        x: win.contentLeft + 20; y: 110; width: Math.max(0, parent.width - x - 20)
        text: win.mailer ? win.mailer.status : ""
        retryText: "R\u00e9essayer"; onRetry: win.mailer.refresh()
    }

    FileDialog {
        id: downloadDialog
        title: "Enregistrer « " + win.dlName + " » sur ce poste"
        fileMode: FileDialog.SaveFile
        onAccepted: win.mailer.downloadAttachment(win.dlMessage, win.dlPart, win.dlName, selectedFile)
    }

    // ── send result banner ─────────────────────────────────────────────
    Rectangle {
        id: sendBanner
        objectName: "mailSendBanner"
        visible: win.mailer !== null && !win.composeOpen && (win.mailer.sendState === "sent" || win.mailer.sendState === "warning" || win.mailer.sendState === "unknown" || win.mailer.sendState === "failed")
        z: 20
        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 14
        width: Math.min(parent.width - 24, 560); height: bannerText.implicitHeight + 24; radius: 10
        color: "#F0121C30"; border.width: 1
        border.color: win.mailer && (win.mailer.sendState === "sent") ? "#804ADE80" : "#80FCA5A5"
        Text { id: bannerText; x: 12; y: 12; width: parent.width - 90; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: win.mailer ? win.mailer.sendStatus : ""; color: Theme.ink; font.pixelSize: 13 }
        Button { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: "OK"; onClicked: win.mailer.resetSend() }
    }

    // ── compose ────────────────────────────────────────────────────────
    Rectangle {
        id: composePanel
        objectName: "mailCompose"
        visible: win.composeOpen
        z: 50
        anchors.centerIn: parent
        width: Math.min(parent.width - 24, 640); height: Math.min(parent.height - 24, composeColumn.implicitHeight + 28)
        radius: 14; color: "#F0121C30"; border.width: 1; border.color: "#40DCEBFF"
        MouseArea { anchors.fill: parent }
        Flickable {
            id:composeViewport;objectName:"mailComposeScroll";anchors.fill: parent; anchors.margins: 14; clip: true; contentWidth: width; contentHeight: composeColumn.implicitHeight; boundsBehavior: Flickable.StopAtBounds
            Column {
                id: composeColumn
                width: parent.width; spacing: 8
                Text { text: win.composeReplyTo !== "" ? "Répondre" : "Nouveau message"; color: Theme.ink; font.pixelSize: 16 }
                Text {
                    visible: !win.mailer || win.mailer.accounts.length === 0; width: parent.width; wrapMode: Text.Wrap; color: "#FCD34D"; font.pixelSize: 12
                    text: "Aucun compte d'envoi trouvé sur ce serveur (~/.msmtprc). Ouvrez « Comptes » pour en créer un."
                }
                ComboBox {
                    id: accountBox
                    objectName: "mailComposeAccount"
                    enabled:win.composeSourceAvailable && !win.composeSending
                    width: parent.width
                    model: win.mailer ? win.mailer.accounts : []
                    textRole: "name"
                    displayText: currentIndex >= 0 && win.mailer && currentIndex < win.mailer.accounts.length ? "De : " + win.mailer.accounts[currentIndex].name + "  <" + win.mailer.accounts[currentIndex].from + ">" : "De : —"
                }
                TextField { id: toField;readOnly:win.composeSending; objectName: "mailComposeTo"; width: parent.width; placeholderText: "À (adresses séparées par des virgules)"; selectByMouse: true }
                Row {
                    width: parent.width; spacing: 6
                    TextField { id: ccField;readOnly:win.composeSending; objectName: "mailComposeCc"; width: (parent.width - 6) / 2; placeholderText: "Cc"; selectByMouse: true }
                    TextField { id: bccField;readOnly:win.composeSending; objectName: "mailComposeBcc"; width: (parent.width - 6) / 2; placeholderText: "Cci (invisible pour les autres)"; selectByMouse: true }
                }
                TextField { id: subjectField;readOnly:win.composeSending; objectName: "mailComposeSubject"; width: parent.width; placeholderText: "Objet"; selectByMouse: true }
                Text {
                    width:parent.width;wrapMode:Text.Wrap;textFormat:Text.PlainText;color:"#FCD34D";font.pixelSize:12
                    visible:win.composeActive && !win.composeSourceAvailable
                    text:"Brouillon de "+win.composeSourceLabel+" : source déconnectée ou changée. Texte conservé ; envoi désactivé."
                }
                Button {
                    objectName:"mailComposeChooseFiles";text:"Choisir une pièce jointe dans Fichiers…"
                    enabled:win.composeSourceAvailable && win.mailer!==null && !win.mailer.actionBusy
                    onClicked:win.requestAttachmentFiles(win.composeServerId,win.composeGeneration)
                }
                Repeater {
                    model:win.mailer?win.mailer.outgoingAttachments:[]
                    Row {
                        required property var modelData
                        required property int index
                        width:composeColumn.width;spacing:6
                        Text { width:parent.width-48;height:32;verticalAlignment:Text.AlignVCenter;elide:Text.ElideMiddle;textFormat:Text.PlainText;color:Theme.ink;font.pixelSize:12;text:parent.modelData.name+" · "+parent.modelData.size+" o" }
                        Button { width:40;height:32;text:"×";enabled:win.mailer!==null && !win.mailer.actionBusy;onClicked:win.mailer.removeOutgoingAttachment(parent.index) }
                    }
                }
                Text {
                    objectName:"mailComposeAttachmentStatus";width:parent.width;wrapMode:Text.Wrap;textFormat:Text.PlainText;color:Theme.inkMuted;font.pixelSize:11
                    text:(win.mailer?win.mailer.outgoingStatus:"")+"  ·  10 fichiers / 10 Mio au total. Les fichiers restent sur le serveur."
                }
                Rectangle {
                    width: parent.width; height: 200; radius: 8; color: "#30182740"; border.width: 1; border.color: bodyField.activeFocus ? Theme.accent : "#25DCEBFF"
                    ScrollView {
                        anchors.fill: parent; anchors.margins: 4; clip: true
                        TextArea { id: bodyField;readOnly:win.composeSending; objectName: "mailComposeBody"; color: Theme.ink; font.pixelSize: 13; wrapMode: TextEdit.Wrap; selectByMouse: true; background: null }
                    }
                }
                Text {
                    width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; font.pixelSize: 12
                    visible: win.composeError !== "" || (win.mailer && (win.mailer.sendState === "sending" || win.mailer.sendState === "failed" || win.mailer.sendState === "unknown"))
                    color: win.composeError !== "" || (win.mailer && win.mailer.sendState !== "sending") ? "#FCA5A5" : Theme.inkMuted
                    text: win.composeError !== "" ? win.composeError : (win.mailer ? win.mailer.sendStatus : "")
                }
                Text {
                    width: parent.width; wrapMode: Text.Wrap; color: Theme.inkMuted; font.pixelSize: 11
                    text: "Le message est construit ici puis envoyé par msmtp depuis le serveur Linux ; une copie est enregistrée dans le dossier Sent."
                }
                Button {
                    objectName:"mailComposeAcknowledgeUnknown";visible:win.mailer!==null && win.mailer.sendState==="unknown"
                    text:"J'ai vérifié l'envoi précédent";enabled:win.composeSourceAvailable
                    onClicked:{win.mailer.resetSend();win.composeError=""}
                }
                Flow {
                    width:parent.width;spacing:8
                    Button {
                        objectName: "mailComposeSend"
                        text: win.mailer && win.mailer.sendState === "sending" ? "Envoi…" : "Envoyer"
                        enabled: win.composeSourceAvailable && win.mailer !== null && win.mailer.sendState !== "sending" && win.mailer.sendState !== "unknown" && !win.mailer.actionBusy && win.mailer.accounts.length > 0
                        onClicked: win.submitCompose()
                    }
                    Button { objectName:"mailComposeClose";text:"Fermer";enabled:!win.mailer || win.mailer.sendState!=="sending";onClicked:win.composeOpen=false }
                    Button { objectName:"mailComposeDiscard";text:"Abandonner…";enabled:!win.mailer || !win.mailer.actionBusy;onClicked:{win.pendingQuit=null;discardDialog.openWith("Abandonner le brouillon ?","Le texte et la sélection des fichiers seront oubliés. Aucun fichier serveur ne sera supprimé.","Abandonner",true,false,"")} }
                }
            }
        }
    }

    GlassDialog {
        id:discardDialog;objectName:"mailComposeDiscardDialog";anchors.fill:parent;touchMode:win.adaptive;z:90
        busy:win.mailer!==null && win.mailer.actionBusy
        onAccepted:{const go=win.pendingQuit;win.pendingQuit=null;visible=false;win.discardCompose();if(go)go()}
        onRejected:win.pendingQuit=null
        Keys.onReleased:event=>{if(event.key===Qt.Key_Escape)event.accepted=true}
    }
    // ── account wizard ─────────────────────────────────────────────────
    Rectangle {
        id: wizardPanel
        objectName: "mailWizard"
        visible: win.wizardOpen
        z: 60
        anchors.centerIn: parent
        width: Math.min(parent.width - 24, 760); height: parent.height - 24
        radius: 14; color: "#F5121C30"; border.width: 1; border.color: "#40DCEBFF"
        MouseArea { anchors.fill: parent }
        Flickable {
            id: wizardFlick
            objectName: "mailWizFlick"
            anchors.fill: parent; anchors.margins: 14; clip: true; contentWidth: width; contentHeight: wizardColumn.implicitHeight + 8; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: wizardColumn
                width: parent.width - 12; spacing: 8
                Text { text: "Compte courrier (serveur Linux)"; color: Theme.ink; font.pixelSize: 16 }
                Text {
                    width: parent.width; wrapMode: Text.Wrap; color: Theme.inkMuted; font.pixelSize: 12
                    text: "Écrit ~/.mbsyncrc (réception IMAP, lecture seule côté serveur de courrier : Sync Pull), ~/.msmtprc (envoi) et, s'il manque, ~/.notmuch-config. Le mot de passe est enregistré dans ces fichiers, en mode 0600. Rien n'est écrit avant que vous ayez vu le contenu exact et donné votre accord."
                }
                Text {
                    visible: win.mailer !== null && win.mailer.setupState.ok === true; width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Theme.inkMuted; font.pixelSize: 11
                    text: win.mailer && win.mailer.setupState.ok ? "Serveur : " + (win.mailer.setupState.mbsync || "mbsync absent") + " · " + (win.mailer.setupState.msmtp || "msmtp absent") + " · " + (win.mailer.setupState.notmuch || "notmuch absent") + " · courrier dans " + win.mailer.setupState.mailRoot : ""
                }
                TextField { id: wName; objectName: "mailWizName"; width: parent.width; placeholderText: "Nom du compte (ex. perso)"; selectByMouse: true; text: "perso" }
                Row {
                    width: parent.width; spacing: 6
                    TextField { id: wDisplay; objectName: "mailWizDisplay"; width: (parent.width - 6) / 2; placeholderText: "Nom affiché (expéditeur)"; selectByMouse: true }
                    TextField { id: wEmail; objectName: "mailWizEmail"; width: (parent.width - 6) / 2; placeholderText: "Adresse e-mail"; selectByMouse: true }
                }
                Text { text: "Réception (IMAP)"; color: Theme.ink; font.pixelSize: 13; font.bold: true }
                Row {
                    width: parent.width; spacing: 6
                    TextField { id: wImapHost; objectName: "mailWizImapHost"; width: parent.width - 80 - 140 - 12; placeholderText: "Serveur IMAP"; selectByMouse: true }
                    TextField { id: wImapPort; objectName: "mailWizImapPort"; width: 80; placeholderText: "Port"; selectByMouse: true; text: "993" }
                    ComboBox {
                        id: wImapSec; objectName: "mailWizImapSec"; width: 140
                        model: [ { k: "tls", t: "SSL/TLS" }, { k: "starttls", t: "STARTTLS" }, { k: "none", t: "Aucun (localhost)" } ]; textRole: "t"
                        onActivated: wImapPort.text = currentIndex === 0 ? "993" : "143"
                    }
                }
                TextField { id: wImapUser; objectName: "mailWizImapUser"; width: parent.width; placeholderText: "Utilisateur IMAP"; selectByMouse: true }
                Text { text: "Envoi (SMTP)"; color: Theme.ink; font.pixelSize: 13; font.bold: true }
                Row {
                    width: parent.width; spacing: 6
                    TextField { id: wSmtpHost; objectName: "mailWizSmtpHost"; width: parent.width - 80 - 140 - 12; placeholderText: "Serveur SMTP"; selectByMouse: true }
                    TextField { id: wSmtpPort; objectName: "mailWizSmtpPort"; width: 80; placeholderText: "Port"; selectByMouse: true; text: "587" }
                    ComboBox {
                        id: wSmtpSec; objectName: "mailWizSmtpSec"; width: 140
                        model: [ { k: "starttls", t: "STARTTLS" }, { k: "tls", t: "SSL/TLS" }, { k: "none", t: "Aucun (localhost)" } ]; textRole: "t"
                        onActivated: wSmtpPort.text = currentIndex === 0 ? "587" : "465"
                    }
                }
                TextField { id: wSmtpUser; objectName: "mailWizSmtpUser"; width: parent.width; placeholderText: "Utilisateur SMTP (vide = identique à IMAP)"; selectByMouse: true }
                Rectangle {
                    objectName: "mailWizPasswordWarning"
                    width: parent.width; height: warningColumn.implicitHeight + 16; radius: 8
                    color: "#33F59E0B"; border.width: 1; border.color: "#80F59E0B"
                    Column {
                        id: warningColumn
                        x: 10; y: 8; width: parent.width - 20; spacing: 4
                        Text { objectName: "mailWizWarningText"; width: parent.width; wrapMode: Text.Wrap; color: "#FDE68A"; font.pixelSize: 12; font.family: Theme.fontFamily
                               text: win.mailer ? win.mailer.passwordWarning() : "" }
                        Text { objectName: "mailWizAdvice"; visible: text !== ""; width: parent.width; wrapMode: Text.Wrap; color: "#FEF3C7"; font.pixelSize: 12; font.family: Theme.fontFamily
                               text: win.mailer ? win.mailer.passwordAdviceFor(wImapHost.text, wEmail.text) : "" }
                    }
                }
                TextField { id: wPassword; objectName: "mailWizPassword"; width: parent.width; echoMode: TextInput.Password; placeholderText: "Mot de passe"; selectByMouse: true }
                TextField { id: wSmtpPassword; objectName: "mailWizSmtpPassword"; width: parent.width; echoMode: TextInput.Password; placeholderText: "Mot de passe SMTP (vide = identique)"; selectByMouse: true }
                Text { visible: win.wizardError !== ""; width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: "#FCA5A5"; font.pixelSize: 12; text: win.wizardError }
                Row {
                    spacing: 8
                    Button { objectName: "mailWizPreview"; text: "Aperçu des fichiers"; enabled: win.mailer !== null && !win.mailer.actionBusy; onClicked: win.wizardPreview() }
                    Button { text: "Fermer"; onClicked: win.closeWizard() }
                }

                // preview with the exact content
                Column {
                    id: previewColumn
                    objectName: "mailWizFiles"
                    visible: win.mailer !== null && win.mailer.setupFiles.length > 0
                    width: parent.width; spacing: 8
                    Rectangle { width: parent.width; height: 1; color: "#25DCEBFF" }
                    Text { width: parent.width; wrapMode: Text.Wrap; color: Theme.ink; font.pixelSize: 13; text: "Contenu exact qui sera écrit (mot de passe masqué tant que vous ne le révélez pas) :" }
                    CheckBox {
 palette.windowText: Theme.ink
 id: revealBox; objectName: "mailWizReveal"; text: "Révéler le mot de passe dans l'aperçu" }
                    Repeater {
                        model: win.mailer ? win.mailer.setupFiles : []
                        Column {
                            required property var modelData
                            width: previewColumn.width; spacing: 4
                            CheckBox {
 palette.windowText: Theme.ink

                                objectName: "mailWizWrite_" + modelData.key
                                checked: true
                                text: "Écrire " + modelData.path + (modelData.kind === "block" ? "  (seul le bloc FORTIQ de ce compte est ajouté ou remplacé ; le reste du fichier est conservé, avec une sauvegarde)" : "  (nouveau fichier)")
                                onToggled: { const k = Object.assign({}, win.wizardKeys); k[modelData.key] = checked; win.wizardKeys = k }
                                Component.onCompleted: { const k = Object.assign({}, win.wizardKeys); k[modelData.key] = checked; win.wizardKeys = k }
                            }
                            Rectangle {
                                width: parent.width; height: Math.min(260, fileText.implicitHeight + 16); radius: 8; color: "#30182740"; border.width: 1; border.color: "#25DCEBFF"
                                Flickable {
                                    anchors.fill: parent; anchors.margins: 8; clip: true; contentWidth: fileText.implicitWidth; contentHeight: fileText.implicitHeight; boundsBehavior: Flickable.StopAtBounds
                                    Text { id: fileText; objectName: "mailWizText_" + modelData.key; textFormat: Text.PlainText; text: revealBox.checked ? modelData.content : modelData.shown; color: Theme.ink; font.pixelSize: 12; font.family: Theme.monoFamily }
                                }
                            }
                        }
                    }
                    CheckBox {
 palette.windowText: Theme.ink
 id: consentBox; objectName: "mailWizConsent"; text: "J'ai relu le contenu exact ci-dessus et j'autorise son écriture sur le serveur (mode 0600) ; je sais que le mot de passe y sera en clair" }
                    Button {
                        objectName: "mailWizWrite"
                        text: "Écrire sur le serveur"
                        enabled: consentBox.checked && win.mailer !== null && !win.mailer.actionBusy && win.checkedKeys().length > 0
                        onClicked: { win.mailer.applySetup(win.checkedKeys()); consentBox.checked = false; wPassword.text = ""; wSmtpPassword.text = "" }
                    }
                }

                // tests and first sync
                Rectangle { width: parent.width; height: 1; color: "#25DCEBFF" }
                Row {
                    spacing: 8
                    Button { objectName: "mailWizTestSmtp"; text: "Tester SMTP"; enabled: win.mailer !== null && !win.mailer.actionBusy; onClicked: win.mailer.testSmtp(wName.text.trim()) }
                    Button { objectName: "mailWizTestImap"; text: "Tester IMAP"; enabled: win.mailer !== null && !win.mailer.actionBusy; onClicked: win.mailer.testImap(wName.text.trim()) }
                    Button { objectName: "mailWizSync"; text: "Relever maintenant"; enabled: win.mailer !== null && !win.mailer.actionBusy; onClicked: win.mailer.syncNow(wName.text.trim()) }
                    Button { visible: win.mailer !== null && win.mailer.actionBusy; text: "Annuler"; onClicked: win.mailer.cancelAction() }
                }
                Text {
                    objectName: "mailWizStatus"
                    visible: win.mailer !== null && win.mailer.actionStatus !== ""; width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Theme.ink; font.pixelSize: 12
                    text: win.mailer ? win.mailer.actionStatus : ""
                }
                Rectangle {
                    visible: win.mailer !== null && win.mailer.actionOutput !== ""
                    width: parent.width; height: Math.min(200, outText.implicitHeight + 16); radius: 8; color: "#30182740"; border.width: 1; border.color: "#25DCEBFF"
                    Flickable {
                        anchors.fill: parent; anchors.margins: 8; clip: true; contentWidth: outText.implicitWidth; contentHeight: outText.implicitHeight; boundsBehavior: Flickable.StopAtBounds
                        Text { id: outText; objectName: "mailWizOutput"; textFormat: Text.PlainText; text: win.mailer ? win.mailer.actionOutput : ""; color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.monoFamily }
                    }
                }
            }
        }
    }

    Connections {
        target: win.remote
        ignoreUnknownSignals: true
        function onConnectedChanged() { if (win.wizardOpen && win.remote && !win.remote.connected) win.closeWizard() }
        function onActiveServerIdChanged() { if (win.wizardOpen && win.remote && win.remote.activeServerId !== win.wizardServerId) win.closeWizard() }
    }
    Connections {
        target: win.mailer
        ignoreUnknownSignals: true
        function onSendChanged() { if (win.mailer && (win.mailer.sendState === "sent" || win.mailer.sendState === "warning")) { win.composeOpen=false;win.composeActive=false } }
        function onActionChanged() {
            if (win.mailer && !win.mailer.actionBusy && win.previewPending) { win.previewPending = false; win.finishPreview() }
        }
    }
    LinkBanner { remote: win.remote; touch: win.adaptive; onRetry: if (win.mailer && !win.mailer.busy) win.mailer.refresh() }
}
