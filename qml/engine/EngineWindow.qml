import QtQuick
import QtQuick.Controls.Fusion
import Fortiq.Shell

// Navigateur (moteur Chromium): the page runs on this device, JavaScript included, but EVERY network request leaves through the
// loopback gateway and the SSH tunnel of the selected server (docs/BROWSER.md, "Moteur"). Without a connected server nothing starts.
// One page at this stage; the structure (a Loader around one EngineView) is what the tabs of stage 4 will multiply.
Item {
    id: win
    objectName: "engineWindow"
    property bool adaptive: false
    property Item backdrop
    property var engine: null          // EngineBrowser (C++)
    property var legacy: null          // BrowserController: its search engine choice and address parsing are shared
    property var remote: null
    property string notice: ""
    readonly property bool connected: remote !== null && remote.connected
    readonly property bool ready: engine !== null && engine.state === "ready"
    readonly property var view: pageLoader.item
    readonly property string engineName: {
        if (!legacy) return ""
        for (const e of legacy.searchEngines) if (e.id === legacy.searchEngine) return e.name
        return ""
    }
    signal closeRequested()
    signal requestServers()
    signal requestTextMode()

    function handleBack() {
        if (engine && engine.pendingConfirm.host !== undefined) { engine.answerConfirm(false); return true }
        if (view && view.canGoBack) { view.goBack(); return true }
        return false
    }
    function focusAddress() { address.forceActiveFocus(); address.selectAll() }
    function showNotice(text) { notice = text; noticeTimer.restart() }
    function open(text) {
        if (!view || !legacy) return
        const r = legacy.resolveTyped(text)
        if (r.url.length === 0) { showNotice(r.error); return }
        view.url = r.url
    }
    function syncView() {
        const want = ready
        if (pageLoader.active) pageLoader.active = false      // another gateway = another secret: a fresh profile
        pageLoader.active = want
    }
    onVisibleChanged: if (engine) engine.setWindowActive(visible)
    onEngineChanged: if (engine) engine.setWindowActive(visible)
    Component.onCompleted: { if (engine) engine.setWindowActive(visible); syncView() }
    Connections {
        target: win.engine
        ignoreUnknownSignals: true
        function onChanged() { if (win.engine.state !== "ready" || !pageLoader.active || pageLoader.generation !== win.engine.generation) win.syncView() }
        function onNotice(text) { win.showNotice(text) }
        function onEngineMustStop() { pageLoader.active = false }
    }
    Timer { id: noticeTimer; interval: 7000; onTriggered: win.notice = "" }

    Glass { anchors.fill: parent; backdrop: win.backdrop; tint: "#B3121C30"; blurAmount: 0.9; radius: 16 }

    // ── toolbar (also the drag handle) ─────────────────────────────────
    Item {
        id: toolbar
        width: parent.width; height: win.adaptive ? 56 : 48
        WindowDragArea { anchors.fill: parent; windowItem: win; movable: !win.adaptive }
        Row {
            visible: !win.adaptive
            x: 14; y: 17; spacing: 9
            Repeater {
                model: ["#FF5F57", "#FEBC2E", "#28C840"]
                Rectangle {
                    required property string modelData
                    width: 12; height: 12; radius: 6; color: modelData
                    MouseArea { anchors.fill: parent; onClicked: if (parent.modelData === "#FF5F57") win.closeRequested() }
                }
            }
        }
        Row {
            id: nav
            x: win.adaptive ? 8 : 84; anchors.verticalCenter: parent.verticalCenter; spacing: 2
            ToolButtonGlass { objectName: "engineBack"; touch: win.adaptive; glyph: "‹"; tip: "Précédent"; enabledState: win.view !== null && win.view.canGoBack; onClicked: win.view.goBack() }
            ToolButtonGlass { objectName: "engineForward"; touch: win.adaptive; glyph: "›"; tip: "Suivant"; enabledState: win.view !== null && win.view.canGoForward; onClicked: win.view.goForward() }
            ToolButtonGlass {
                objectName: "engineReload"
                touch: win.adaptive
                glyph: win.view && win.view.loading ? "×" : "↻"
                tip: win.view && win.view.loading ? "Arrêter" : "Actualiser"
                enabledState: win.view !== null
                onClicked: win.view.loading ? win.view.stop() : win.view.reload()
            }
        }
        Rectangle {
            id: addressBar
            anchors.left: nav.right; anchors.leftMargin: 8
            anchors.right: tools.left; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            height: win.adaptive ? 44 : 30; radius: height / 2
            color: "#1AFFFFFF"
            border.width: 1; border.color: address.activeFocus ? Theme.accent : "#1FFFFFFF"
            readonly property string shown: win.view ? win.view.url.toString() : ""
            Text {
                objectName: "engineLock"
                x: 12; anchors.verticalCenter: parent.verticalCenter
                text: addressBar.shown.indexOf("https://") === 0 && !(win.view && win.view.certificateProblem) ? "🔒" : (addressBar.shown.length > 0 && addressBar.shown !== "about:blank" ? "⚠" : "⌕")
                color: addressBar.shown.indexOf("http://") === 0 ? "#FCD34D" : Theme.inkMuted
                font.pixelSize: 13
            }
            TextInput {
                id: address
                objectName: "engineAddress"
                x: 36; width: parent.width - 52
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.ink; selectionColor: Theme.accent
                font.pixelSize: win.adaptive ? 14 : 12; font.family: Theme.fontFamily
                clip: true
                selectByMouse: true
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                enabled: win.ready
                onAccepted: { win.open(text); win.forceActiveFocus() }
                Keys.onEscapePressed: { text = addressBar.shown; win.forceActiveFocus() }
                Text {
                    visible: parent.text.length === 0 && !parent.activeFocus
                    anchors.verticalCenter: parent.verticalCenter
                    text: win.ready ? "Rechercher ou saisir une adresse — tout passe par le serveur Linux" : "Aucun serveur connecté"
                    color: "#7F93B5"; font.pixelSize: win.adaptive ? 14 : 12; font.family: Theme.fontFamily
                }
            }
            // The bar shows the page's address unless the user is typing.
            Binding { target: address; property: "text"; value: addressBar.shown; when: !address.activeFocus && addressBar.shown.length > 0 && addressBar.shown !== "about:blank" }
        }
        Row {
            id: tools
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Rectangle {
                id: enginePill
                objectName: "engineSearch"
                anchors.verticalCenter: parent.verticalCenter
                height: win.adaptive ? 36 : 24; radius: height / 2
                width: engineLabel.implicitWidth + 22
                color: engineMouse.containsMouse ? "#33FFFFFF" : "#1AFFFFFF"
                Text { id: engineLabel; anchors.centerIn: parent; color: Theme.ink; font.family: Theme.fontFamily; font.pixelSize: win.adaptive ? 13 : 11; text: "⌕ " + win.engineName }
                MouseArea { id: engineMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; enabled: win.legacy !== null; onClicked: engineMenu.popup() }
                Menu {
                    id: engineMenu
                    Repeater {
                        model: win.legacy ? win.legacy.searchEngines : []
                        MenuItem {
                            required property var modelData
                            text: modelData.name
                            checkable: true
                            checked: win.legacy !== null && win.legacy.searchEngine === modelData.id
                            onTriggered: win.legacy.searchEngine = modelData.id
                        }
                    }
                }
            }
            ToolButtonGlass {
                objectName: "engineTextMode"
                touch: win.adaptive; glyph: "☰"; tip: "Lecteur simple (texte, sans moteur) — pour une liaison lente"
                onClicked: win.requestTextMode()
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1AFFFFFF" }
        Rectangle {   // thin progress line while a page loads
            visible: win.view !== null && win.view.loading
            anchors.bottom: parent.bottom; height: 2; width: parent.width * Math.max(0.05, win.view ? win.view.loadProgress / 100 : 0); color: Theme.accent
        }
    }

    // ── notice (refused address, download refused, certificate...) ─────
    Rectangle {
        id: noticeBar
        objectName: "engineNotice"
        visible: win.notice.length > 0
        y: toolbar.height; width: parent.width; height: visible ? noticeText.implicitHeight + 12 : 0; z: 20
        color: "#26FACC15"
        Text {
            id: noticeText
            x: 14; y: 6; width: parent.width - 28
            wrapMode: Text.WordWrap; textFormat: Text.PlainText
            text: win.notice
            color: "#FEF3C7"; font.pixelSize: 11; font.family: Theme.fontFamily
        }
    }

    // ── page ───────────────────────────────────────────────────────────
    Item {
        id: page
        y: toolbar.height + noticeBar.height
        width: parent.width
        height: Math.max(0, parent.height - y - 22)
        clip: true
        Loader {
            id: pageLoader
            objectName: "enginePage"
            anchors.fill: parent
            active: false
            property int generation: -1
            onActiveChanged: if (active) generation = win.engine.generation
            sourceComponent: EngineView {
                engine: win.engine
                url: "about:blank"
                onRefused: text => win.showNotice(text)
            }
        }
        // Start page and states.
        Column {
            visible: !win.ready || (win.view !== null && win.view.url.toString() === "about:blank")
            anchors.centerIn: parent; width: Math.min(parent.width - 48, 520); spacing: 12
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                text: !win.engine ? "Moteur indisponible"
                    : win.engine.state === "noserver" || !win.connected ? "Aucun serveur connecté"
                    : win.engine.state === "error" ? "Le navigateur ne peut pas démarrer"
                    : "Navigateur"
                color: win.engine && win.engine.state === "error" ? "#FCA5A5" : Theme.ink; font.pixelSize: 16; font.family: Theme.fontFamily
            }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                text: !win.engine ? "Ce FORTIQ est construit sans moteur web : utilisez le lecteur simple."
                    : win.ready ? "La page s'exécute ici, JavaScript compris ; chaque requête passe par le serveur " + win.engine.serverLabel + " (tunnel SSH). Rien n'est enregistré sur cet appareil : ni cookies, ni historique, ni cache."
                    : win.engine.status
                color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily
            }
            Flow {
                visible: win.ready
                width: parent.width; spacing: 8
                Repeater {
                    model: [ { t: "Wikipédia", u: "https://fr.wikipedia.org" }, { t: "Hacker News", u: "https://news.ycombinator.com" },
                             { t: "Debian", u: "https://www.debian.org" }, { t: "Ubuntu", u: "https://ubuntu.com" } ]
                    Rectangle {
                        required property var modelData
                        objectName: "engineQuick"
                        height: 30; radius: 15; width: quickLabel.implicitWidth + 28
                        color: quickMouse.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
                        Text { id: quickLabel; anchors.centerIn: parent; text: parent.modelData.t; color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily }
                        MouseArea { id: quickMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.open(parent.modelData.u) }
                    }
                }
            }
            Rectangle {
                visible: !win.connected
                anchors.horizontalCenter: parent.horizontalCenter
                height: 34; radius: 9; width: chooseLabel.implicitWidth + 30
                color: chooseMouse.containsMouse ? Theme.selection : "#992D6FE0"
                Text { id: chooseLabel; anchors.centerIn: parent; text: "Choisir un serveur…"; color: "white"; font.pixelSize: 13; font.family: Theme.fontFamily }
                MouseArea { id: chooseMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.requestServers() }
            }
        }
        // A failed load (name not found by the server, refused address, TLS error...).
        Column {
            visible: win.view !== null && win.view.failure.length > 0 && !win.view.loading
            anchors.centerIn: parent; width: Math.min(parent.width - 48, 520); spacing: 8
            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; color: "#FCA5A5"; font.pixelSize: 16; font.family: Theme.fontFamily; text: "La page n'a pas pu être chargée" }
            Text { objectName: "engineFailure"; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily; text: win.view ? win.view.failure : "" }
        }
    }

    // ── internal address: asked before anything is contacted (docs/BROWSER.md, address policy) ──
    Rectangle {
        id: confirmPanel
        objectName: "engineConfirm"
        readonly property var ask: win.engine ? win.engine.pendingConfirm : ({})
        visible: ask.host !== undefined
        x: 0; y: toolbar.height + noticeBar.height; z: 30
        width: page.width; height: page.height
        color: "#E6101A2E"
        MouseArea { anchors.fill: parent }   // modal for the page area
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 32, 560); height: Math.min(parent.height - 24, confirmColumn.implicitHeight + 32)
            radius: 12; color: "#F2121C30"; border.width: 1; border.color: "#60FCD34D"
            Flickable {
                anchors.fill: parent; anchors.margins: 16; clip: true; contentWidth: width; contentHeight: confirmColumn.implicitHeight; boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: confirmColumn
                    width: parent.width; spacing: 8
                    Text { width: parent.width; wrapMode: Text.Wrap; color: "#FCD34D"; font.pixelSize: 15; font.bold: true; font.family: Theme.fontFamily; text: "Adresse interne au serveur" }
                    Text {
                        width: parent.width; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily
                        text: confirmPanel.ask.host !== undefined ? confirmPanel.ask.host + ":" + confirmPanel.ask.port : ""
                    }
                    Text {
                        width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
                        text: confirmPanel.ask.reason ? confirmPanel.ask.reason : ""
                    }
                    Repeater {
                        model: confirmPanel.ask.details ? confirmPanel.ask.details : []
                        Text {
                            required property var modelData
                            width: confirmColumn.width; textFormat: Text.PlainText; font.pixelSize: 11; font.family: Theme.monoFamily
                            color: modelData.cls === "public" ? Theme.inkMuted : "#FCD34D"
                            text: "• " + modelData.ip + "   " + modelData.label
                        }
                    }
                    Text {
                        width: parent.width; wrapMode: Text.Wrap; color: Theme.inkMuted; font.pixelSize: 10; font.family: Theme.fontFamily
                        text: "Aucune requête n'a été envoyée. « Ouvrir une fois » autorise cet hôte et ce port jusqu'à la fermeture du navigateur ; les adresses de métadonnées du cloud ne sont jamais ouvertes."
                    }
                    Row {
                        spacing: 8
                        Button { objectName: "engineConfirmOpen"; text: "Ouvrir une fois"; onClicked: win.engine.answerConfirm(true) }
                        Button { objectName: "engineConfirmCancel"; text: "Annuler"; onClicked: win.engine.answerConfirm(false) }
                    }
                }
            }
        }
    }

    // ── status line ────────────────────────────────────────────────────
    Rectangle {
        anchors.bottom: parent.bottom; width: parent.width; height: 22; color: "#14FFFFFF"
        Text {
            x: 14; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 28; elide: Text.ElideRight
            textFormat: Text.PlainText
            text: win.view && win.view.loading ? "Chargement… " + win.view.loadProgress + " %" : (win.view && win.view.title.length > 0 ? win.view.title : (win.engine ? win.engine.status : ""))
            color: Theme.inkMuted; font.pixelSize: 10; font.family: Theme.fontFamily
        }
    }
}
