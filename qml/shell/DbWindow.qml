import QtQuick
import QtQuick.Controls.Fusion

// Bases de données (lecture seule): SQLite et PostgreSQL via sqlite3 / psql sur le serveur.
// Les mots de passe restent dans le stockage protégé du client (docs/DATABASES.md).
Item {
    id: win
    objectName: "dbWindow"
    property bool adaptive: false
    property Item backdrop
    property var db: null
    property var remote: null
    property bool navigationOpen: false
    property bool formOpen: false
    property string formEngine: "sqlite"
    property string formId: ""
    property bool formHadPassword: false
    property string formError: ""
    property var expanded: ({})
    readonly property bool connected: remote !== null && remote.connected
    readonly property bool drawer: adaptive && width < 900
    readonly property real contentLeft: drawer ? 0 : 196
    readonly property bool narrow: width - contentLeft < 640
    readonly property bool hasProfile: db !== null && db.selectedId !== ""
    readonly property int colWidth: 150
    signal closeRequested()
    signal requestServers()
    signal requestCopy(string what, string text)
    function resultAsText() {
        if (!db || db.columns.length === 0) return ""
        const cell = v => v === null || v === undefined ? "NULL" : String(v)
        return [db.columns.map(cell).join("\t")].concat(db.rows.map(r => r.map(cell).join("\t"))).join("\n")
    }

    function sync() { if (db) db.setWindowActive(visible) }
    onVisibleChanged: sync()
    onDbChanged: sync()
    Component.onCompleted: sync()
    function handleBack() {
        if (formOpen) { formOpen = false; return true }
        if (navigationOpen) { navigationOpen = false; return true }
        return false
    }
    function openForm(profile) {
        const p = profile || {}
        formId = p.id || ""
        formEngine = p.engine || "sqlite"
        nameField.text = p.name || ""
        pathField.text = p.path || ""
        hostField.text = p.host || ""
        portField.text = String(p.port || 5432)
        userField.text = p.user || ""
        dbField.text = p.database || ""
        passwordField.text = ""
        clearPassword.checked = false
        formHadPassword = !!p.hasPassword
        formError = ""
        formOpen = true
        navigationOpen = false
    }
    function submitForm() {
        const result = db.saveProfile({ id: formId, name: nameField.text, engine: formEngine, path: pathField.text, host: hostField.text,
                                        port: parseInt(portField.text), user: userField.text, database: dbField.text },
                                      passwordField.text, clearPassword.checked)
        passwordField.text = ""
        if (result === "") { formOpen = false; formError = ""; db.loadSchema() } else formError = result
    }
    function runCurrent() { if (db && hasProfile && !db.busy) db.runQuery(editor.text) }

    readonly property var schemaRows: {
        if (!db) return []
        const rows = []
        for (const o of db.schema) {
            const key = o.schema + "." + o.name
            rows.push({ kind: "object", key: key, label: o.label, objectKind: o.kind, count: o.columns.length, object: o })
            if (expanded[key]) for (const c of o.columns) rows.push({ kind: "column", key: key + "." + c.name, label: c.name, type: c.type, nullable: c.nullable })
        }
        return rows
    }

    Glass { anchors.fill: parent; backdrop: win.backdrop; tint: Theme.appWindow; blurAmount: 0.9; radius: 16 }
    MouseArea { visible: win.drawer && win.navigationOpen; anchors.fill: parent; z: 30; onClicked: win.navigationOpen = false }

    Rectangle {
        objectName: "dbSidebar"
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
        Text { x: 18; y: 48; text: "PROFILS"; color: Theme.inkMuted; font.pixelSize: 11; font.letterSpacing: 1 }
        ListView {
            objectName: "dbProfileList"
            x: 8; y: 70; width: parent.width - 16; height: Math.max(0, parent.height - y - 150)
            clip: true; spacing: 4; model: win.db ? win.db.profiles : []
            delegate: Rectangle {
                required property var modelData
                objectName: "dbProfile_" + modelData.name
                width: ListView.view.width; height: win.adaptive ? 52 : 44; radius: 8
                color: win.db && win.db.selectedId === modelData.id ? "#D02D73DA" : (profMouse.containsMouse ? "#26FFFFFF" : "transparent")
                Text { x: 12; y: 6; width: parent.width - 48; elide: Text.ElideRight; textFormat: Text.PlainText; text: modelData.name; color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold }
                Text { x: 12; y: 24; width: parent.width - 48; elide: Text.ElideMiddle; textFormat: Text.PlainText; text: (modelData.engine === "sqlite" ? "SQLite · " : "PostgreSQL · ") + modelData.summary; color: Theme.inkMuted; font.pixelSize: 11 }
                MouseArea { id: profMouse; anchors.fill: parent; hoverEnabled: true; onClicked: { win.db.select(modelData.id); win.navigationOpen = false } }
                Rectangle {
                    anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
                    width: 30; height: 30; radius: 6; color: editMouse.containsMouse ? "#33FFFFFF" : "transparent"
                    SymbolGlyph { anchors.centerIn: parent; glyph: "✎"; color: Theme.ink; size: 13 }
                    MouseArea { id: editMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.openForm(modelData) }
                }
            }
        }
        Button {
            objectName: "dbNewProfile"
            x: 12; y: parent.height - 140; width: parent.width - 24
            text: "+ Nouveau profil"; enabled: win.db !== null && win.db.canEdit
            onClicked: win.openForm(null)
        }
        Text {
            x: 18; y: parent.height - 100; width: parent.width - 36; wrapMode: Text.WordWrap
            text: "Lecture seule : aucune écriture dans la base. Pour une vraie garantie, utilisez un rôle de base de données sans droit d'écriture."
            color: Theme.inkMuted; font.pixelSize: 11
        }
    }

    // ── header ─────────────────────────────────────────────────────────
    Item {
        id: header
        x: win.contentLeft; width: parent.width - x; height: 48
        WindowDragArea { anchors.fill: parent; windowItem: win; movable: !win.adaptive }
        ToolButtonGlass { visible: win.drawer; x: 4; y: 2; touch: true; glyph: "☰"; tip: "Menu"; onClicked: win.navigationOpen = !win.navigationOpen }
        Text {
            x: win.drawer ? 54 : 14; width: Math.max(0, parent.width - x - 330); anchors.verticalCenter: parent.verticalCenter
            text: "Bases de données · " + (win.hasProfile && win.db.selected.name ? win.db.selected.name : (win.connected ? win.remote.serverLabel : "Aucun serveur"))
            elide: Text.ElideMiddle; textFormat: Text.PlainText; color: Theme.ink; font.pixelSize: 14
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 4
            visible: win.hasProfile
            Text { anchors.verticalCenter: parent.verticalCenter; text: "Délai"; color: Theme.inkMuted; font.pixelSize: 11 }
            ComboBox {
                id: timeoutBox
                objectName: "dbTimeout"
                width: 78; height: win.adaptive ? 44 : 28
                model: [5, 15, 30, 60, 120]
                displayText: currentText + " s"
                currentIndex: win.db ? Math.max(0, model.indexOf(win.db.timeoutSec)) : 1
                onActivated: win.db.setLimits(model[currentIndex], win.db.maxRows)
            }
            Text { anchors.verticalCenter: parent.verticalCenter; text: "Lignes"; color: Theme.inkMuted; font.pixelSize: 11 }
            ComboBox {
                id: rowsBox
                objectName: "dbMaxRows"
                width: 86; height: win.adaptive ? 44 : 28
                model: [100, 500, 1000, 5000]
                currentIndex: win.db ? Math.max(0, model.indexOf(win.db.maxRows)) : 1
                onActivated: win.db.setLimits(win.db.timeoutSec, model[currentIndex])
            }
            Button {
                objectName: "dbRun"
                height: win.adaptive ? 44 : 28
                text: win.db && win.db.busy ? "Annuler" : "Exécuter ▶"
                enabled: win.db !== null && (win.db.busy || win.hasProfile)
                onClicked: if (win.db.busy) win.db.cancelQuery(); else win.runCurrent()
            }
        }
    }

    // ── what the connected role may do (audit R4) ──────────────────────
    Rectangle {
        id: roleBanner
        objectName: "dbRoleBanner"
        readonly property var check: win.db ? win.db.roleCheck : ({})
        readonly property string level: check.level !== undefined ? check.level : ""
        property bool expanded: false
        readonly property color tone: level === "danger" ? "#FCA5A5" : level === "warning" ? "#FCD34D" : level === "ok" ? "#4ADE80" : level === "note" ? "#8EC1FF" : "#94A3B8"
        visible: win.connected && win.db !== null && win.hasProfile && level !== ""
        x: win.contentLeft + 12; y: 52; width: parent.width - x - 12
        height: visible ? (expanded && level !== "ok" ? bannerColumn.implicitHeight + 16 : 30) : 0
        radius: 8; clip: true; color: Qt.rgba(tone.r, tone.g, tone.b, 0.12); border.width: 1; border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.5)
        Connections { target: win.db; ignoreUnknownSignals: true; function onRoleChanged() { roleBanner.expanded = roleBanner.level === "danger" } }
        MouseArea { anchors.left: parent.left; anchors.right: parent.right; height: 30; enabled: roleBanner.level !== "ok"; onClicked: roleBanner.expanded = !roleBanner.expanded }
        Column {
            id: bannerColumn
            x: 10; y: 8; width: parent.width - 20; spacing: 6
            Row {
                spacing: 8; width: parent.width
                Text { text: roleBanner.level === "ok" ? "✓" : roleBanner.level === "unknown" ? "?" : "!"; color: roleBanner.tone; font.pixelSize: 13; font.bold: true }
                Text {
                    width: parent.width - 80; textFormat: Text.PlainText; color: Theme.ink; font.pixelSize: 12
                    elide: roleBanner.expanded ? Text.ElideNone : Text.ElideRight; wrapMode: roleBanner.expanded ? Text.Wrap : Text.NoWrap
                    text: roleBanner.check.summary ? roleBanner.check.summary : ""
                }
                Text { visible: roleBanner.level !== "ok"; text: roleBanner.expanded ? "Moins ▴" : "Détails ▾"; color: roleBanner.tone; font.pixelSize: 11 }
            }
            Repeater {
                model: roleBanner.expanded && roleBanner.check.findings ? roleBanner.check.findings : []
                Text { required property string modelData; width: bannerColumn.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: "• " + modelData; color: Theme.ink; font.pixelSize: 12 }
            }
            Text {
                visible: roleBanner.expanded && roleBanner.check.recommendation !== undefined && roleBanner.check.recommendation !== ""
                width: parent.width; wrapMode: Text.Wrap; color: Theme.inkMuted; font.pixelSize: 11
                text: "Pour une vraie garantie, utilisez un rôle sans droit d'écriture. Recette (à faire exécuter par un administrateur) :"
            }
            TextEdit {
                id: recipeText
                objectName: "dbRoleRecipe"
                visible: roleBanner.expanded && roleBanner.check.recommendation !== undefined && roleBanner.check.recommendation !== ""
                width: parent.width; readOnly: true; selectByMouse: true; wrapMode: TextEdit.NoWrap; textFormat: TextEdit.PlainText
                color: Theme.ink; font.pixelSize: 11; font.family: Theme.monoFamily
                text: roleBanner.check.recommendation ? roleBanner.check.recommendation : ""
            }
            Button {
                objectName: "dbRoleCopy"
                visible: recipeText.visible
                text: "Copier la recette"
                onClicked: { recipeText.selectAll(); recipeText.copy(); recipeText.deselect() }
            }
        }
    }

    // ── workspace ──────────────────────────────────────────────────────
    Item {
        id: workspace
        visible: win.connected && win.db !== null && win.hasProfile
        x: win.contentLeft + 12; y: 52 + (roleBanner.visible ? roleBanner.height + 6 : 0); width: parent.width - x - 12; height: parent.height - y - 10

        Rectangle {
            id: schemaPane
            objectName: "dbSchema"
            visible: !win.narrow
            width: visible ? 210 : 0; height: parent.height; radius: 10; color: "#30182740"; border.width: 1; border.color: "#25DCEBFF"
            Text { x: 10; y: 8; text: "SCHÉMA"; color: "#8FA2C2"; font.pixelSize: 11; font.letterSpacing: 1.2 }
            Text {
                visible: win.db && win.db.schemaStatus !== ""
                x: 10; y: 28; width: parent.width - 20; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                text: win.db ? win.db.schemaStatus : ""; color: Theme.inkMuted; font.pixelSize: 11
            }
            ListView {
                objectName: "dbSchemaList"
                x: 4; y: 28; width: parent.width - 8; height: parent.height - y - 4
                visible: win.db && win.db.schemaStatus === ""
                clip: true; model: win.schemaRows
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width - 8; height: modelData.kind === "object" ? 30 : 22; radius: 6
                    color: rowMouse.containsMouse ? "#14FFFFFF" : "transparent"
                    Text {
                        x: modelData.kind === "object" ? 6 : 24; anchors.verticalCenter: parent.verticalCenter; width: parent.width - x - (modelData.kind === "object" ? 34 : 6)
                        elide: Text.ElideRight; textFormat: Text.PlainText; font.pixelSize: modelData.kind === "object" ? 12 : 11
                        color: modelData.kind === "object" ? Theme.ink : Theme.inkMuted
                        text: modelData.kind === "object" ? ((win.expanded[modelData.key] ? "▾ " : "▸ ") + modelData.label + (modelData.objectKind === "view" ? " (vue)" : ""))
                                                          : modelData.label + "  " + modelData.type + (modelData.nullable ? "" : " ·NN")
                    }
                    MouseArea {
                        id: rowMouse; anchors.fill: parent; hoverEnabled: true
                        onClicked: if (modelData.kind === "object") { const e = Object.assign({}, win.expanded); e[modelData.key] = !e[modelData.key]; win.expanded = e }
                    }
                    Rectangle {
                        visible: modelData.kind === "object"
                        anchors.right: parent.right; anchors.rightMargin: 2; anchors.verticalCenter: parent.verticalCenter
                        width: 28; height: 24; radius: 5; color: selMouse.containsMouse ? "#33FFFFFF" : "transparent"
                        Text { anchors.centerIn: parent; text: "⏵"; color: Theme.ink; font.pixelSize: 13 }
                        MouseArea { id: selMouse; anchors.fill: parent; hoverEnabled: true; onClicked: { editor.text = win.db.selectAllFor(modelData.object); editor.forceActiveFocus() } }
                    }
                }
            }
        }

        Item {
            x: schemaPane.width + (schemaPane.visible ? 10 : 0); width: parent.width - x; height: parent.height
            Rectangle {
                id: editorBox
                width: parent.width; height: Math.min(160, Math.max(96, parent.height * 0.28)); radius: 10
                color: "#30182740"; border.width: 1; border.color: editor.activeFocus ? Theme.accent : "#25DCEBFF"
                ScrollView {
                    anchors.fill: parent; anchors.margins: 4; clip: true
                    TextArea {
                        id: editor
                        objectName: "dbEditor"
                        color: Theme.ink; font.pixelSize: 13; font.family: Theme.monoFamily; wrapMode: TextEdit.WrapAnywhere; selectByMouse: true
                        placeholderText: "SELECT … (Ctrl+Entrée pour exécuter)"; placeholderTextColor: "#7F93B5"
                        background: null
                        Keys.onPressed: (event) => {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) { win.runCurrent(); event.accepted = true }
                        }
                    }
                }
            }
            Text {
                id: infoLine
                objectName: "dbInfo"
                y: editorBox.height + 6; width: parent.width - 90; elide: Text.ElideRight; textFormat: Text.PlainText; font.pixelSize: 12
                color: win.db && win.db.error !== "" ? "#FCA5A5" : Theme.inkMuted
                text: !win.db ? "" : (win.db.error !== "" ? "" : (win.db.busy ? win.db.status : (win.db.resultInfo !== "" ? win.db.resultInfo : win.db.status)))
                visible: win.db && win.db.error === ""
            }
            CopyChip {
                objectName: "dbCopyResult"
                visible: win.db !== null && win.db.error === "" && win.db.columns.length > 0
                anchors.right: parent.right; y: editorBox.height + 3; touch: win.adaptive
                onClicked: win.requestCopy("résultat de la requête", win.resultAsText())
            }
            AppStateMessage {
                id: errorLine
                objectName: "dbError"
                visible: win.db && win.db.error !== ""
                state: "error"; touch: win.adaptive
                y: editorBox.height + 6; width: parent.width
                text: win.db ? win.db.error : ""; maximumLineCount: 5; elide: Text.ElideRight
            }
            Flickable {
                id: tableFlick
                objectName: "dbResult"
                y: editorBox.height + 6 + Math.max(infoLine.visible ? infoLine.implicitHeight : errorLine.implicitHeight, 16) + 6
                width: parent.width; height: Math.max(0, parent.height - y)
                clip: true; boundsBehavior: Flickable.StopAtBounds; flickableDirection: Flickable.HorizontalFlick
                contentWidth: Math.max(width, (win.db ? win.db.columns.length : 0) * win.colWidth); contentHeight: height
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                Item {
                    width: tableFlick.contentWidth; height: tableFlick.height
                    visible: win.db && win.db.columns.length > 0
                    Row {
                        id: headRow
                        height: 28
                        Repeater {
                            model: win.db ? win.db.columns : []
                            Rectangle {
                                required property string modelData
                                required property int index
                                width: win.colWidth; height: 28; color: "#40307CF0"; border.width: 1; border.color: "#25DCEBFF"
                                Text { anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; textFormat: Text.PlainText
                                       horizontalAlignment: win.db.numeric[index] ? Text.AlignRight : Text.AlignLeft; text: modelData; color: Theme.ink; font.pixelSize: 12; font.weight: Font.DemiBold }
                            }
                        }
                    }
                    ListView {
                        objectName: "dbRows"
                        y: headRow.height; width: parent.width; height: parent.height - y
                        clip: true; model: win.db ? win.db.rows : []
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Row {
                            required property var modelData
                            required property int index
                            Repeater {
                                model: modelData
                                Rectangle {
                                    required property var modelData
                                    required property int index
                                    width: win.colWidth; height: 26
                                    color: parent.parent.index % 2 ? "#0DFFFFFF" : "transparent"
                                    border.width: 1; border.color: "#14FFFFFF"
                                    Text {
                                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; textFormat: Text.PlainText
                                        horizontalAlignment: win.db.numeric[index] ? Text.AlignRight : Text.AlignLeft
                                        text: modelData === null ? "NULL" : String(modelData).replace(/\n/g, "⏎")
                                        font.pixelSize: 12; font.italic: modelData === null; font.family: win.db.numeric[index] ? Theme.monoFamily : ""
                                        color: modelData === null ? "#7F93B5" : Theme.ink
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── placeholders ───────────────────────────────────────────────────
    NoServerState {
        visible: !win.connected; x: win.contentLeft + 16; y: 150; width: parent.width - x - 16
        touch: win.adaptive; buttonName: "dbConnectButton"; hint: "Les bases de données sont lues sur le serveur Linux choisi, en lecture seule."
        onChooseServer: win.requestServers()
    }
    Text {
        visible: win.connected && win.db !== null && !win.hasProfile && !win.formOpen
        x: win.contentLeft + 20; y: 110; width: parent.width - x - 20; wrapMode: Text.WordWrap; textFormat: Text.PlainText
        text: win.db ? (win.db.canEdit ? (win.db.profiles.length === 0 ? "Aucun profil. Créez un profil SQLite (fichier sur le serveur) ou PostgreSQL." : "Choisissez un profil dans la liste.") : win.db.status) : ""
        color: Theme.inkMuted; font.pixelSize: 13
    }

    // ── profile form ───────────────────────────────────────────────────
    Rectangle {
        id: formPanel
        objectName: "dbForm"
        visible: win.formOpen
        z: 50
        anchors.centerIn: parent
        width: Math.min(parent.width - 24, 460); height: Math.min(parent.height - 24, formColumn.implicitHeight + 28)
        radius: 14; color: "#F0121C30"; border.width: 1; border.color: "#40DCEBFF"
        MouseArea { anchors.fill: parent }
        Flickable {
            anchors.fill: parent; anchors.margins: 14; clip: true; contentWidth: width; contentHeight: formColumn.implicitHeight; boundsBehavior: Flickable.StopAtBounds
            Column {
                id: formColumn
                width: parent.width; spacing: 8
                Text { text: win.formId === "" ? "Nouveau profil" : "Modifier le profil"; color: Theme.ink; font.pixelSize: 16 }
                Row {
                    spacing: 6
                    Repeater {
                        model: [ { k: "sqlite", t: "SQLite" }, { k: "postgres", t: "PostgreSQL" } ]
                        Rectangle {
                            required property var modelData
                            objectName: "dbEngine_" + modelData.k
                            width: 110; height: win.adaptive ? 44 : 30; radius: 8
                            color: win.formEngine === modelData.k ? "#592F8CFF" : "#1AFFFFFF"; border.width: win.formEngine === modelData.k ? 1 : 0; border.color: "#802F8CFF"
                            Text { anchors.centerIn: parent; text: modelData.t; color: Theme.ink; font.pixelSize: 13 }
                            MouseArea { anchors.fill: parent; onClicked: win.formEngine = modelData.k }
                        }
                    }
                }
                TextField { id: nameField; objectName: "dbName"; width: parent.width; placeholderText: "Nom du profil"; selectByMouse: true }
                TextField { id: pathField; objectName: "dbPath"; visible: win.formEngine === "sqlite"; width: parent.width; placeholderText: "Fichier sur le serveur, ex. /home/user/data.db"; selectByMouse: true }
                Row {
                    visible: win.formEngine === "postgres"; spacing: 6; width: parent.width
                    TextField { id: hostField; objectName: "dbHost"; width: parent.width - 86; placeholderText: "Hôte (vide = socket local)"; selectByMouse: true }
                    TextField { id: portField; objectName: "dbPort"; width: 80; placeholderText: "Port"; selectByMouse: true; inputMethodHints: Qt.ImhDigitsOnly }
                }
                TextField { id: userField; objectName: "dbUser"; visible: win.formEngine === "postgres"; width: parent.width; placeholderText: "Utilisateur"; selectByMouse: true }
                TextField { id: dbField; objectName: "dbDatabase"; visible: win.formEngine === "postgres"; width: parent.width; placeholderText: "Base de données"; selectByMouse: true }
                TextField {
                    id: passwordField; objectName: "dbPassword"; visible: win.formEngine === "postgres"; width: parent.width; echoMode: TextInput.Password
                    enabled: win.db !== null && win.db.secretStoreAvailable
                    placeholderText: win.db && !win.db.secretStoreAvailable ? "Stockage protégé indisponible sur cette plateforme" : (win.formHadPassword ? "Mot de passe enregistré (laisser vide pour le garder)" : "Mot de passe (enregistré dans le stockage protégé)")
                }
                CheckBox {
 palette.windowText: Theme.ink
 id: clearPassword; visible: win.formEngine === "postgres" && win.formHadPassword; text: "Effacer le mot de passe enregistré" }
                Text {
                    visible: win.formEngine === "postgres"; width: parent.width; wrapMode: Text.WordWrap; color: Theme.inkMuted; font.pixelSize: 11
                    text: "La connexion est ouverte depuis le serveur Linux (psql), pas depuis ce poste. Le mot de passe n'est jamais dans la ligne de commande ni dans un journal."
                }
                Text { visible: win.formError !== ""; width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText; color: "#FCA5A5"; font.pixelSize: 12; text: win.formError }
                Row {
                    spacing: 8
                    Button { objectName: "dbFormSave"; text: "Enregistrer"; onClicked: win.submitForm() }
                    Button { text: "Annuler"; onClicked: win.formOpen = false }
                    Button {
                        visible: win.formId !== ""; text: "Supprimer"
                        onClicked: { win.db.removeProfile(win.formId); win.formOpen = false }
                    }
                }
            }
        }
    }
    LinkBanner { remote: win.remote; touch: win.adaptive; onRetry: if (win.db && !win.db.busy) win.db.loadSchema() }
}
