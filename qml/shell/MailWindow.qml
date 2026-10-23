import QtQuick
import QtQuick.Controls

// Courrier (lecture et étiquettes): interface graphique pour notmuch sur le serveur.
// Rien n'est envoyé, déplacé ni supprimé; seules les étiquettes changent (docs/MAIL.md).
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

    function sync() { if (mailer) mailer.setWindowActive(visible) }
    onVisibleChanged: sync()
    onMailerChanged: sync()
    Component.onCompleted: sync()
    function handleBack() {
        if (navigationOpen) { navigationOpen = false; return true }
        if (readerShown && stacked) { mailer.closeThread(); return true }
        return false
    }
    function tagOf(thread, tag) { return thread !== null && thread.tags.indexOf(tag) >= 0 }

    Glass { anchors.fill: parent; backdrop: win.backdrop; tint: "#B3121C30"; blurAmount: 0.9; radius: 16 }
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
        Text { x: 18; y: 48; text: "COURRIER"; color: Theme.inkMuted; font.pixelSize: 10; font.letterSpacing: 1 }
        Column {
            x: 8; y: 72; width: parent.width - 16; spacing: 4
            Repeater {
                model: win.mailer ? win.mailer.folders : []
                Rectangle {
                    required property var modelData
                    objectName: "mailFolder_" + modelData.key
                    width: parent.width; height: win.adaptive ? 44 : 32; radius: 8
                    color: win.mailer && win.mailer.folderKey === modelData.key ? "#D02D73DA" : (folderMouse.containsMouse ? "#26FFFFFF" : "transparent")
                    Text { x: 12; width: parent.width - 52; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight; text: modelData.title; color: Theme.ink; font.pixelSize: 12 }
                    Text {
                        visible: modelData.count >= 0 && modelData.key !== "all" && modelData.key !== "deleted"
                        anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.count; color: modelData.key === "unread" && modelData.count > 0 ? "#8EC1FF" : Theme.inkMuted; font.pixelSize: 11; font.bold: modelData.key === "unread"
                    }
                    MouseArea { id: folderMouse; anchors.fill: parent; hoverEnabled: true; onClicked: { win.mailer.openFolder(modelData.key); win.navigationOpen = false } }
                }
            }
        }
        Text {
            x: 18; y: parent.height - 96; width: parent.width - 36; wrapMode: Text.WordWrap
            text: "Les messages sont lus par notmuch sur le serveur. Ici, seules les étiquettes changent : rien n'est envoyé ni supprimé."
            color: Theme.inkMuted; font.pixelSize: 10
        }
    }

    // ── header ─────────────────────────────────────────────────────────
    Item {
        id: header
        x: win.contentLeft; width: parent.width - x; height: 48
        WindowDragArea { anchors.fill: parent; windowItem: win; movable: !win.adaptive }
        ToolButtonGlass { visible: win.drawer; x: 4; y: 2; touch: true; glyph: "☰"; onClicked: win.navigationOpen = !win.navigationOpen }
        ToolButtonGlass { visible: win.stacked && win.readerShown; x: win.drawer ? 50 : 4; y: 2; touch: win.adaptive; glyph: "←"; tip: "Retour à la liste"; onClicked: win.mailer.closeThread() }
        Text {
            x: (win.drawer ? 54 : 14) + (win.stacked && win.readerShown ? 46 : 0); width: Math.max(0, parent.width - x - 60); anchors.verticalCenter: parent.verticalCenter
            text: "Courrier · " + (win.connected ? win.remote.serverLabel : "Aucun serveur"); elide: Text.ElideMiddle; textFormat: Text.PlainText; color: Theme.ink; font.pixelSize: 14
        }
        ToolButtonGlass {
            anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
            touch: win.adaptive; glyph: "↻"; tip: "Actualiser"
            enabledState: win.connected && win.mailer !== null && !win.mailer.busy; onClicked: win.mailer.refresh()
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
                color: Theme.ink; font.pixelSize: 12; clip: true; selectByMouse: true
                onAccepted: win.mailer.search(text)
                Text { visible: parent.text.length === 0; anchors.verticalCenter: parent.verticalCenter; text: "Rechercher (de:, sujet:, tag:…) puis Entrée"; color: "#7F93B5"; font.pixelSize: 12 }
            }
        }
        Text {
            id: listStatus
            y: searchBox.height + 6; width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText; font.pixelSize: 11
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
                    text: modelData.authors; color: Theme.ink; font.pixelSize: 12; font.bold: modelData.unread
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 8; y: 6; width: 80; horizontalAlignment: Text.AlignRight; textFormat: Text.PlainText
                    text: modelData.when; color: Theme.inkMuted; font.pixelSize: 10
                }
                Text {
                    x: 22; y: 23; width: parent.width - 60; elide: Text.ElideRight; textFormat: Text.PlainText
                    text: modelData.subject + (modelData.total > 1 ? "  (" + modelData.total + ")" : ""); color: Theme.ink; font.pixelSize: 12; font.bold: modelData.unread
                }
                Row {
                    x: 22; y: parent.height - 18; spacing: 8
                    Text { visible: modelData.flagged; text: "★ suivi"; color: "#FCD34D"; font.pixelSize: 10 }
                    Text { visible: modelData.attachment; text: "pièce jointe"; color: Theme.inkMuted; font.pixelSize: 10 }
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
                    Text { id: actionLabel; anchors.centerIn: parent; text: parent.has ? modelData.on : modelData.off; color: Theme.ink; font.pixelSize: 11 }
                    MouseArea { id: actionMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.mailer.setTag(win.mailer.selectedThreadId, modelData.tag, !parent.has) }
                }
            }
        }
        Text {
            id: threadStatus
            y: actions.height + 6; width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText; font.pixelSize: 11; color: Theme.inkMuted
            visible: win.mailer && win.mailer.threadStatus !== ""; text: win.mailer ? win.mailer.threadStatus : ""
        }
        ListView {
            id: messageList
            objectName: "mailMessages"
            y: actions.height + 6 + (threadStatus.visible ? threadStatus.implicitHeight + 6 : 0)
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
                    x: 12; y: 10; width: parent.width - 24; spacing: 4
                    Text { width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: modelData.subject; color: Theme.ink; font.pixelSize: 14; font.bold: true }
                    Text { width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: "De : " + modelData.from; color: Theme.ink; font.pixelSize: 11 }
                    Text { width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: "À : " + modelData.to + (modelData.cc !== "" ? "   Cc : " + modelData.cc : ""); color: Theme.inkMuted; font.pixelSize: 10 }
                    Text { width: parent.width; textFormat: Text.PlainText; text: modelData.date; color: Theme.inkMuted; font.pixelSize: 10 }
                    Text {
                        visible: modelData.attachments.length > 0; width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; font.pixelSize: 11; color: "#FCD34D"
                        text: "Pièces jointes (non téléchargeables dans ce premier module) : " + modelData.attachments.map(a => a.name).join(", ")
                    }
                    Rectangle { width: parent.width; height: 1; color: "#25DCEBFF" }
                    Text {
                        visible: modelData.hasText && !(win.showHtml && modelData.hasHtml)
                        width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: modelData.text; color: Theme.ink; font.pixelSize: 12
                    }
                    Text {
                        visible: !modelData.hasText && modelData.hasHtml || (win.showHtml && modelData.hasHtml)
                        width: parent.width; wrapMode: Text.Wrap; textFormat: Text.RichText; text: modelData.html; color: Theme.ink; font.pixelSize: 12; linkColor: "#8EC1FF"
                        onLinkHovered: (link) => win.hoveredLink = link
                    }
                    Text {
                        visible: modelData.hasHtml
                        width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; font.pixelSize: 10; color: Theme.inkMuted
                        text: "HTML réduit en texte sûr : " + modelData.htmlScripts + " script(s) ignoré(s), " + modelData.htmlImages + " image(s) remplacée(s) par leur texte, aucun contenu distant chargé."
                    }
                    Rectangle {
                        visible: modelData.hasText && modelData.hasHtml
                        objectName: "mailToggleHtml"
                        height: win.adaptive ? 44 : 26; radius: 8; width: toggleLabel.implicitWidth + 18; color: toggleMouse.containsMouse ? "#33FFFFFF" : "#1AFFFFFF"
                        Text { id: toggleLabel; anchors.centerIn: parent; text: win.showHtml ? "Afficher le texte brut" : "Afficher la version HTML"; color: Theme.ink; font.pixelSize: 11 }
                        MouseArea { id: toggleMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.showHtml = !win.showHtml }
                    }
                }
            }
        }
        Text {
            visible: win.hoveredLink !== ""; anchors.bottom: parent.bottom; width: parent.width; elide: Text.ElideMiddle; textFormat: Text.PlainText
            text: "Lien (non ouvert) : " + win.hoveredLink; color: "#8EC1FF"; font.pixelSize: 10
        }
    }
    Text {
        visible: win.connected && win.mailer !== null && !win.readerShown && !win.stacked && win.mailer.state === "ready"
        x: win.contentLeft + win.listWidth + 6 + 20; y: 140; width: parent.width - x - 20; wrapMode: Text.WordWrap
        text: "Choisissez une conversation."; color: Theme.inkMuted; font.pixelSize: 13
    }

    // ── no connection / errors ─────────────────────────────────────────
    Column {
        visible: !win.connected; x: win.contentLeft + 16; y: 150; width: parent.width - x - 16; spacing: 18
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: "Aucun serveur connecté"; color: Theme.ink; font.pixelSize: 16 }
        Button { objectName: "mailConnectButton"; anchors.horizontalCenter: parent.horizontalCenter; text: "Choisir un serveur…"; onClicked: win.requestServers() }
    }
    Text {
        objectName: "mailProblem"
        visible: win.connected && win.mailer !== null && win.mailer.state === "error" && win.mailer.folders.length === 0
        x: win.contentLeft + 20; y: 110; width: parent.width - x - 20; wrapMode: Text.WordWrap; textFormat: Text.PlainText
        text: win.mailer ? win.mailer.status : ""; color: "#FCA5A5"; font.pixelSize: 13
    }
}
