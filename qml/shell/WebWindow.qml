import QtQuick
import QtQuick.Controls

// Navigateur: a GUI over curl running on the selected Linux server (docs/BROWSER.md).
// Pages are fetched by the server, then shown here as safe text: no JavaScript,
// no styles, no automatic loading of images or other resources.
Item {
    id: win
    objectName: "webWindow"
    property bool adaptive: false
    property Item backdrop
    property var browser: null
    property var remote: null
    property bool infoOpen: false
    readonly property bool connected: remote !== null && remote.connected
    readonly property string kind: browser ? browser.kind : "empty"
    signal closeRequested()
    signal requestServers()

    function sync() { if (browser) browser.setWindowActive(visible) }
    onVisibleChanged: sync()
    onBrowserChanged: sync()
    Component.onCompleted: sync()
    function handleBack() {
        if (browser && browser.state === "confirm") { browser.cancelConfirm(); return true }
        if (infoOpen) { infoOpen = false; return true }
        if (browser && browser.canGoBack) { browser.goBack(); return true }
        return false
    }
    function focusAddress() { address.forceActiveFocus(); address.selectAll() }
    function fmtSeconds(v) { return v > 0 ? (v * 1000).toFixed(0) + " ms" : "—" }

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
            ToolButtonGlass { touch: win.adaptive; glyph: "‹"; tip: "Précédent"; enabledState: win.browser !== null && win.browser.canGoBack; onClicked: win.browser.goBack() }
            ToolButtonGlass { touch: win.adaptive; glyph: "›"; tip: "Suivant"; enabledState: win.browser !== null && win.browser.canGoForward; onClicked: win.browser.goForward() }
            ToolButtonGlass {
                touch: win.adaptive
                glyph: win.browser && win.browser.busy ? "×" : "↻"
                tip: win.browser && win.browser.busy ? "Arrêter" : "Actualiser"
                enabledState: win.connected && win.browser !== null && (win.browser.busy || win.browser.url.length > 0)
                onClicked: win.browser.busy ? win.browser.stop() : win.browser.reload()
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
            // The padlock only says what the server saw: https with a verified certificate.
            Text {
                x: 12; anchors.verticalCenter: parent.verticalCenter
                text: win.browser && win.browser.url.indexOf("https://") === 0 ? "🔒" : (win.browser && win.browser.url.length > 0 ? "⚠" : "⌕")
                color: win.browser && win.browser.url.indexOf("http://") === 0 ? "#FCD34D" : Theme.inkMuted
                font.pixelSize: 13
            }
            TextInput {
                id: address
                objectName: "webAddress"
                x: 36; width: parent.width - 52
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.ink; selectionColor: Theme.accent
                font.pixelSize: win.adaptive ? 14 : 12; font.family: Theme.fontFamily
                clip: true
                selectByMouse: true
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                enabled: win.connected
                onAccepted: if (win.browser) { win.browser.open(text); win.forceActiveFocus() }
                Keys.onEscapePressed: { text = win.browser ? win.browser.url : ""; win.forceActiveFocus() }
                Text {
                    visible: parent.text.length === 0 && !parent.activeFocus
                    anchors.verticalCenter: parent.verticalCenter
                    text: win.connected ? "Adresse web — chargée par le serveur Linux" : "Aucun serveur connecté"
                    color: "#7F93B5"; font.pixelSize: win.adaptive ? 14 : 12; font.family: Theme.fontFamily
                }
            }
            Connections {
                target: win.browser
                ignoreUnknownSignals: true
                function onChanged() { if (!address.activeFocus && win.browser.url.length > 0) address.text = win.browser.url }
            }
        }
        Row {
            id: tools
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            ToolButtonGlass {
                objectName: "webReader"
                visible: win.kind === "html"
                touch: win.adaptive; glyph: "☰"; tip: "Mode lecture (contenu principal seul)"
                checked: win.browser !== null && win.browser.reader
                onClicked: win.browser.reader = !win.browser.reader
            }
            ToolButtonGlass {
                objectName: "webInfo"
                touch: win.adaptive; glyph: "ⓘ"; tip: "Réponse du serveur : en-têtes, durées, redirections"
                checked: win.infoOpen
                enabledState: win.browser !== null && win.browser.url.length > 0
                onClicked: win.infoOpen = !win.infoOpen
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1AFFFFFF" }
        Rectangle {   // thin progress line while the server is fetching
            visible: win.browser !== null && win.browser.busy
            anchors.bottom: parent.bottom; width: parent.width; height: 2; color: "transparent"
            Rectangle {
                id: runner
                width: parent.width * 0.25; height: 2; radius: 1; color: Theme.accent
                NumberAnimation on x { from: -runner.width; to: toolbar.width; duration: 900; loops: Animation.Infinite; running: win.browser !== null && win.browser.busy }
            }
        }
    }

    // Notes (JavaScript, redirects, size limit...), kept short and above the page.
    Rectangle {
        id: notes
        objectName: "webNotes"
        visible: win.browser !== null && win.browser.notes.length > 0 && win.kind !== "error"
        y: toolbar.height; width: parent.width; height: visible ? notesText.implicitHeight + 12 : 0
        color: "#26FACC15"
        Text {
            id: notesText
            x: 14; y: 6; width: parent.width - 28
            wrapMode: Text.WordWrap; textFormat: Text.PlainText
            text: win.browser ? win.browser.notes.join("  ·  ") : ""
            color: "#FEF3C7"; font.pixelSize: 11; font.family: Theme.fontFamily
        }
    }

    // ── page ───────────────────────────────────────────────────────────
    Item {
        id: page
        y: toolbar.height + notes.height
        width: parent.width - (win.infoOpen && !win.adaptive ? 300 : 0)
        height: Math.max(0, parent.height - y - 26)
        clip: true

        Flickable {
            id: htmlFlick
            visible: win.kind === "html"
            anchors.fill: parent
            contentWidth: width; contentHeight: htmlText.height + 32
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            TextEdit {
                id: htmlText
                objectName: "webHtml"
                x: 24; y: 16; width: parent.width - 48
                readOnly: true; selectByMouse: true
                textFormat: TextEdit.RichText
                wrapMode: TextEdit.Wrap
                text: win.browser && win.kind === "html" ? "<style>a { color: #9CC6FF; } h1, h2, h3 { color: #FFFFFF; } code, pre { font-family: monospace; }</style>" + win.browser.contentHtml : ""
                color: Theme.ink; selectionColor: Theme.accent
                font.pixelSize: win.adaptive ? 15 : 13; font.family: Theme.fontFamily
                onLinkActivated: link => win.browser.openLink(link)
                onTextChanged: htmlFlick.contentY = 0
            }
        }
        Flickable {
            id: textFlick
            visible: win.kind === "text" || win.kind === "json"
            anchors.fill: parent
            contentWidth: width; contentHeight: plainText.height + 32
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            TextEdit {
                id: plainText
                objectName: "webText"
                x: 24; y: 16; width: parent.width - 48
                readOnly: true; selectByMouse: true
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.WrapAnywhere
                text: win.browser && (win.kind === "text" || win.kind === "json") ? win.browser.contentText : ""
                color: Theme.ink; selectionColor: Theme.accent
                font.pixelSize: win.adaptive ? 13 : 12; font.family: Theme.monoFamily
                onTextChanged: textFlick.contentY = 0
            }
        }
        Image {
            visible: win.kind === "image"
            anchors.fill: parent; anchors.margins: 16
            fillMode: Image.PreserveAspectFit
            source: win.browser && win.kind === "image" ? win.browser.imageSource : ""
            asynchronous: true
        }

        // Nothing to show: a hint, a connection prompt, or an explicit error.
        Column {
            visible: win.kind === "empty" || win.kind === "binary" || win.kind === "error" || !win.connected
            anchors.centerIn: parent; width: Math.min(parent.width - 48, 520); spacing: 12
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                text: !win.connected ? "Aucun serveur connecté"
                    : win.kind === "error" ? "La page n'a pas pu être chargée"
                    : win.kind === "binary" ? "Contenu non affichable"
                    : "Navigateur du serveur"
                color: win.kind === "error" ? "#FCA5A5" : Theme.ink; font.pixelSize: 16; font.family: Theme.fontFamily
            }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                text: !win.connected ? "Le navigateur charge les pages depuis le serveur Linux choisi."
                    : win.kind === "empty" && win.browser && win.browser.url.length === 0
                        ? "Saisissez une adresse : curl, exécuté sur le serveur, récupère la page ; ici elle est affichée sans JavaScript ni ressources externes."
                    : win.browser ? win.browser.status : ""
                color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily
            }
            Rectangle {
                visible: !win.connected
                anchors.horizontalCenter: parent.horizontalCenter
                height: 34; radius: 9; width: chooseLabel.implicitWidth + 30
                color: chooseMouse.containsMouse ? Theme.selection : "#992D6FE0"
                Text { id: chooseLabel; anchors.centerIn: parent; text: "Choisir un serveur…"; color: "white"; font.pixelSize: 13; font.family: Theme.fontFamily }
                MouseArea { id: chooseMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.requestServers() }
            }
            Rectangle {
                visible: win.connected && win.kind === "error" && win.browser !== null && win.browser.canGoBack === false && win.browser.info.requested !== undefined
                anchors.horizontalCenter: parent.horizontalCenter
                height: 30; radius: 8; width: retryLabel.implicitWidth + 26
                color: retryMouse.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
                Text { id: retryLabel; anchors.centerIn: parent; text: "Réessayer"; color: "white"; font.pixelSize: 12; font.family: Theme.fontFamily }
                MouseArea { id: retryMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.browser.open(win.browser.info.requested) }
            }
        }
    }

    // ── internal address: asked before anything is fetched (docs/BROWSER.md, address policy) ──
    Rectangle {
        id: confirmPanel
        objectName: "webConfirm"
        visible: win.browser !== null && win.browser.state === "confirm"
        x: 0; y: toolbar.height + notes.height; z: 30
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
                    Text { width: parent.width; wrapMode: Text.Wrap; color: "#FCD34D"; font.pixelSize: 15; font.bold: true; font.family: Theme.fontFamily
                           text: "Adresse interne au serveur" }
                    Text {
                        width: parent.width; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily
                        text: win.browser && win.browser.pendingConfirm.url ? win.browser.pendingConfirm.url : ""
                    }
                    Text {
                        visible: win.browser && win.browser.pendingConfirm.from !== undefined && win.browser.pendingConfirm.from !== ""
                        width: parent.width; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
                        text: win.browser && win.browser.pendingConfirm.from ? "Redirection depuis : " + win.browser.pendingConfirm.from : ""
                    }
                    Text {
                        width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
                        text: win.browser && win.browser.pendingConfirm.reason ? win.browser.pendingConfirm.reason : ""
                    }
                    Repeater {
                        model: win.browser && win.browser.pendingConfirm.details ? win.browser.pendingConfirm.details : []
                        Text {
                            required property var modelData
                            width: confirmColumn.width; textFormat: Text.PlainText; font.pixelSize: 11; font.family: Theme.monoFamily
                            color: modelData.cls === "public" ? Theme.inkMuted : "#FCD34D"
                            text: "• " + modelData.ip + "   " + modelData.label
                        }
                    }
                    Text {
                        width: parent.width; wrapMode: Text.Wrap; color: Theme.inkMuted; font.pixelSize: 10; font.family: Theme.fontFamily
                        text: "Aucune requête n'a été envoyée. « Ouvrir une fois » autorise cet hôte et ce port pour cette navigation seulement (redirections comprises) ; les adresses de métadonnées du cloud ne sont jamais ouvertes."
                    }
                    Row {
                        spacing: 8
                        Button { objectName: "webConfirmOpen"; text: "Ouvrir une fois"; onClicked: win.browser.confirmInternal() }
                        Button { objectName: "webConfirmCancel"; text: "Annuler"; onClicked: win.browser.cancelConfirm() }
                    }
                }
            }
        }
    }

    // ── response details ───────────────────────────────────────────────
    Rectangle {
        id: infoPanel
        objectName: "webInfoPanel"
        visible: win.infoOpen
        x: win.adaptive ? 0 : parent.width - 300
        y: toolbar.height; z: 20
        width: win.adaptive ? parent.width : 300; height: parent.height - y - 26
        color: win.adaptive ? "#F2101A2E" : "#40050B18"
        Rectangle { x: 0; width: 1; height: parent.height; color: "#1FFFFFFF" }
        Flickable {
            anchors.fill: parent; anchors.margins: 12
            contentWidth: width; contentHeight: infoColumn.height
            clip: true; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: infoColumn
                width: parent.width; spacing: 6
                Text { text: "RÉPONSE DU SERVEUR"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2; font.family: Theme.fontFamily }
                Repeater {
                    model: win.browser && win.browser.info.status !== undefined ? [
                        { k: "État", v: "HTTP " + win.browser.info.status + " " + win.browser.info.statusText },
                        { k: "Adresse finale", v: win.browser.url },
                        { k: "Protocole", v: "HTTP " + win.browser.info.httpVersion },
                        { k: "IP du serveur web", v: win.browser.info.remoteIp },
                        { k: "Type", v: win.browser.info.contentType + (win.browser.info.charset ? "  ·  " + win.browser.info.charset : "") },
                        { k: "Taille reçue", v: win.browser.info.size + " octets" },
                        { k: "Redirections", v: String(win.browser.info.redirects) },
                        { k: "Certificat TLS", v: win.browser.url.indexOf("https://") === 0 ? (win.browser.info.tlsVerified ? "vérifié par le serveur" : "non vérifié") : "pas de TLS (http)" }
                    ] : []
                    Column {
                        required property var modelData
                        width: infoColumn.width
                        Text { text: modelData.k; color: Theme.inkMuted; font.pixelSize: 10; font.family: Theme.fontFamily }
                        Text { width: parent.width; text: modelData.v; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily }
                    }
                }
                Text { visible: win.browser !== null && win.browser.info.timings !== undefined; topPadding: 6; text: "DURÉES (depuis le serveur)"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2; font.family: Theme.fontFamily }
                Repeater {
                    model: win.browser && win.browser.info.timings !== undefined ? [
                        { k: "DNS", v: win.fmtSeconds(win.browser.info.timings.dns) },
                        { k: "Connexion", v: win.fmtSeconds(win.browser.info.timings.connect) },
                        { k: "TLS", v: win.fmtSeconds(win.browser.info.timings.tls) },
                        { k: "Premier octet", v: win.fmtSeconds(win.browser.info.timings.firstByte) },
                        { k: "Total", v: win.fmtSeconds(win.browser.info.timings.total) }
                    ] : []
                    Row {
                        required property var modelData
                        spacing: 8
                        Text { width: 100; text: modelData.k; color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily }
                        Text { text: modelData.v; color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily }
                    }
                }
                Text { visible: win.browser !== null && win.browser.info.redirectChain !== undefined && win.browser.info.redirectChain.length > 0; topPadding: 6; text: "REDIRECTIONS"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2; font.family: Theme.fontFamily }
                Repeater {
                    model: win.browser && win.browser.info.redirectChain !== undefined ? win.browser.info.redirectChain : []
                    Text { required property var modelData; width: infoColumn.width; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; text: modelData.status + " → " + modelData.location; color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily }
                }
                Text { visible: win.browser !== null && win.browser.headers.length > 0; topPadding: 6; text: "EN-TÊTES"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2; font.family: Theme.fontFamily }
                Repeater {
                    model: win.browser ? win.browser.headers : []
                    Text {
                        required property var modelData
                        width: infoColumn.width; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText
                        text: modelData.name + ": " + modelData.value
                        color: Theme.inkMuted; font.pixelSize: 10; font.family: Theme.monoFamily
                    }
                }
            }
        }
    }

    // ── status bar ─────────────────────────────────────────────────────
    Item {
        y: parent.height - height; width: parent.width; height: 26
        Rectangle { width: parent.width; height: 1; color: "#14FFFFFF" }
        Text {
            x: 14; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 28; elide: Text.ElideMiddle
            textFormat: Text.PlainText
            text: !win.connected ? "Aucun serveur connecté" : (win.browser ? win.browser.status : "")
            color: win.browser && win.browser.state === "error" ? "#FCA5A5" : Theme.inkMuted
            font.pixelSize: 11; font.family: Theme.fontFamily
        }
    }

    Keys.onPressed: event => {
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_L) { win.focusAddress(); event.accepted = true }
        else if (event.key === Qt.Key_F5 && win.browser) { win.browser.reload(); event.accepted = true }
        else if ((event.modifiers & Qt.AltModifier) && event.key === Qt.Key_Left && win.browser) { win.browser.goBack(); event.accepted = true }
        else if ((event.modifiers & Qt.AltModifier) && event.key === Qt.Key_Right && win.browser) { win.browser.goForward(); event.accepted = true }
    }
    focus: true

    // Resize grip, as in the other windows.
    MouseArea {
        visible: !win.adaptive
        width: 16; height: 16
        anchors.right: parent.right; anchors.bottom: parent.bottom
        cursorShape: Qt.SizeFDiagCursor
        property point press
        onPressed: m => press = Qt.point(m.x, m.y)
        onPositionChanged: m => {
            win.width = Math.max(520, win.width + m.x - press.x)
            win.height = Math.max(340, win.height + m.y - press.y)
        }
    }
}
