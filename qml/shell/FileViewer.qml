import QtQuick
import QtQuick.Controls.Fusion

// Read-only viewer over the Files content area. Text (first 1 MiB) with
// line numbers, images with fit / 100 % / wheel zoom, hex dump for binaries.
// Content stays in RAM; nothing is saved on this device.
Rectangle {
    id: viewer
    property bool adaptive: false
    property var controller: null
    property string name: controller ? controller.previewPath.split("/").pop() : ""
    readonly property string kind: controller ? controller.previewKind : ""
    property real zoom: 0 // 0 = fit
    // Editing state: `dirty` once the text differs from the last loaded/saved one.
    property bool editing: false
    property bool dirty: false
    property string pendingAction: "" // "close" | "stop" | "conflict"
    readonly property bool editable: controller && controller.previewEditable === true
    readonly property bool saving: controller && controller.saving === true
    onEditableChanged: if (!editable) { editing = false; dirty = false }

    function startEditing() {
        if (!editable) return
        editing = true
        textEdit.forceActiveFocus()
    }
    function save() { if (editing && dirty && !saving) controller.saveFile(textEdit.text, false) }
    function requestClose() {
        if (editing && dirty) {
            pendingAction = "close"
            confirm.openWith("Modifications non enregistrées", "Les modifications de « " + name + " » seront perdues.",
                             "Fermer sans enregistrer", true, false, "")
            confirm.altText = "Enregistrer"
            return
        }
        closed()
    }
    function stopEditing() {
        if (dirty) {
            pendingAction = "stop"
            confirm.openWith("Annuler les modifications ?", "Le texte reviendra à la dernière version enregistrée.",
                             "Annuler les modifications", true, false, "")
            confirm.cancelText = "Continuer"
            return
        }
        editing = false
    }
    function revert() {
        editing = false
        dirty = false
        textEdit.text = controller ? controller.previewText : ""
        dirty = false
    }
    signal closed()

    color: "#FA0D1626"
    focus: visible
    onVisibleChanged: { zoom = 0; if (visible) forceActiveFocus() }

    Keys.onPressed: (event) => {
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_S) { viewer.save(); event.accepted = true; return }
        if (viewer.editing) {
            if (event.key === Qt.Key_Escape) { viewer.stopEditing(); event.accepted = true }
            return
        }
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_E) { viewer.startEditing(); event.accepted = true }
        else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Space) { viewer.requestClose(); event.accepted = true }
        else if (viewer.kind === "image" && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)) { viewer.zoomBy(1.25); event.accepted = true }
        else if (viewer.kind === "image" && event.key === Qt.Key_Minus) { viewer.zoomBy(0.8); event.accepted = true }
        else if (viewer.kind === "image" && event.key === Qt.Key_0) { viewer.zoom = 0; event.accepted = true }
    }

    function fitScale() {
        if (image.implicitWidth <= 0) return 1
        return Math.min(1, imageFlick.width / image.implicitWidth, imageFlick.height / image.implicitHeight)
    }
    function zoomBy(f) {
        const base = zoom === 0 ? fitScale() : zoom
        zoom = Math.max(0.05, Math.min(16, base * f))
    }

    FontMetrics { id: metrics; font.family: Theme.monoFamily; font.pixelSize: 12 }

    // Header
    Item {
        id: header
        width: parent.width; height: viewer.adaptive ? 56 : 40
        FileIcon {
            x: 12; anchors.verticalCenter: parent.verticalCenter
            size: 22; name: viewer.name
        }
        Column {
            x: 42; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 42 - tools.width - 16
            Text {
                width: parent.width; elide: Text.ElideMiddle
                text: viewer.name
                color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold; font.family: Theme.fontFamily
            }
            Text {
                width: parent.width; elide: Text.ElideRight
                text: !viewer.controller ? ""
                    : viewer.saving ? "Enregistrement sûr en cours…"
                    : viewer.editing ? (viewer.dirty ? "● Modifié — Ctrl+S pour enregistrer" : "Édition — " + viewer.controller.previewInfo)
                    : viewer.controller.previewInfo + (viewer.editable ? "" : "  ·  lecture seule")
                color: viewer.editing && viewer.dirty ? "#FCD34D" : Theme.inkMuted
                font.pixelSize: 10; font.family: Theme.fontFamily
            }
        }
        Row {
            id: tools
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            ToolButtonGlass { touch: viewer.adaptive; visible: viewer.kind === "image"; glyph: "−"; tip: "Réduire (−)"; onClicked: viewer.zoomBy(0.8) }
            Text {
                visible: viewer.kind === "image"
                anchors.verticalCenter: parent.verticalCenter
                width: 46; horizontalAlignment: Text.AlignHCenter
                text: Math.round((viewer.zoom === 0 ? viewer.fitScale() : viewer.zoom) * 100) + " %"
                color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
            }
            ToolButtonGlass { touch: viewer.adaptive; visible: viewer.kind === "image"; glyph: "+"; tip: "Agrandir (+)"; onClicked: viewer.zoomBy(1.25) }
            ToolButtonGlass { touch: viewer.adaptive; visible: viewer.kind === "image"; glyph: "⤢"; tip: "Ajuster (0)"; checked: viewer.zoom === 0; onClicked: viewer.zoom = 0 }
            ToolButtonGlass { touch: viewer.adaptive; visible: viewer.kind === "image"; glyph: "1:1"; tip: "Taille réelle"; onClicked: viewer.zoom = 1 }
            Rectangle {
                visible: viewer.editable && !viewer.editing
                anchors.verticalCenter: parent.verticalCenter
                height: 26; radius: 7; width: editLabel.implicitWidth + 20
                color: editMouse.containsMouse ? Theme.selection : "#802D6FE0"
                Row { id: editLabel; anchors.centerIn: parent; spacing: 6
                            SymbolGlyph { anchors.verticalCenter: parent.verticalCenter; glyph: "✎"; color: "white"; size: 12 }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: "Modifier"; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily } }
                MouseArea { id: editMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: viewer.startEditing() }
            }
            Rectangle {
                visible: viewer.editing
                anchors.verticalCenter: parent.verticalCenter
                height: 26; radius: 7; width: saveLabel.implicitWidth + 20
                opacity: viewer.dirty && !viewer.saving ? 1 : 0.45
                color: saveMouse.containsMouse ? "#16A34A" : "#B315803D"
                Text { id: saveLabel; anchors.centerIn: parent; text: viewer.saving ? "Enregistrement…" : "Enregistrer  Ctrl+S"; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                MouseArea { id: saveMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; enabled: viewer.dirty && !viewer.saving; onClicked: viewer.save() }
            }
            ToolButtonGlass { touch: viewer.adaptive; visible: viewer.editing; glyph: "↶"; tip: "Quitter l'édition (Échap)"; enabledState: !viewer.saving; onClicked: viewer.stopEditing() }
            ToolButtonGlass { touch: viewer.adaptive; glyph: "×"; tip: "Fermer (Échap)"; enabledState: !viewer.saving; onClicked: viewer.requestClose() }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1FFFFFFF" }
    }

    Item {
        id: body
        y: header.height
        width: parent.width; height: parent.height - y
        clip: true

        BusyIndicator {
            anchors.centerIn: parent
            running: viewer.controller && viewer.controller.previewBusy
            visible: running
        }

        Column {
            visible: viewer.kind === "error"
            anchors.centerIn: parent
            spacing: 8
            width: Math.min(parent.width - 40, 420)
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Aperçu impossible"; color: Theme.ink; font.pixelSize: 15; font.family: Theme.fontFamily }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                text: viewer.controller ? viewer.controller.previewInfo : ""
                color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily
            }
        }

        // Text and hex dump: line numbers + read-only selectable text.
        Flickable {
            id: textFlick
            visible: viewer.kind === "text" || viewer.kind === "binary"
            anchors.fill: parent
            contentWidth: gutter.width + textEdit.contentWidth + 24
            contentHeight: textEdit.contentHeight + 16
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            Rectangle {
                id: gutter
                visible: viewer.kind === "text"
                width: visible ? lineNumbers.implicitWidth + 20 : 0
                height: Math.max(textFlick.height, textFlick.contentHeight)
                color: "#26000000"
                Text {
                    id: lineNumbers
                    x: 10; y: 8
                    text: {
                        if (viewer.kind !== "text" || !viewer.controller) return ""
                        const t = textEdit.text
                        const n = Math.min(t.split("\n").length - (t.endsWith("\n") ? 1 : 0), 50000)
                        let s = ""
                        for (let i = 1; i <= n; ++i) s += i + "\n"
                        return s
                    }
                    color: "#5F7395"
                    font.family: Theme.monoFamily; font.pixelSize: 12
                    horizontalAlignment: Text.AlignRight
                    lineHeight: metrics.lineSpacing; lineHeightMode: Text.FixedHeight
                }
            }
            TextEdit {
                id: textEdit
                x: gutter.width + 12; y: 8
                readOnly: !viewer.editing || viewer.saving
                selectByMouse: true
                persistentSelection: true
                tabStopDistance: metrics.averageCharacterWidth * 4
                text: viewer.controller && (viewer.kind === "text" || viewer.kind === "binary") ? viewer.controller.previewText : ""
                onTextChanged: if (viewer.editing) viewer.dirty = text !== (viewer.controller ? viewer.controller.previewText : "")
                // Keep the caret visible while typing.
                onCursorRectangleChanged: {
                    const r = cursorRectangle
                    if (r.y + y < textFlick.contentY) textFlick.contentY = Math.max(0, r.y + y - 8)
                    else if (r.y + y + r.height > textFlick.contentY + textFlick.height) textFlick.contentY = r.y + y + r.height - textFlick.height + 8
                    if (r.x + x < textFlick.contentX) textFlick.contentX = Math.max(0, r.x + x - 24)
                    else if (r.x + x > textFlick.contentX + textFlick.width - 24) textFlick.contentX = r.x + x - textFlick.width + 48
                }
                color: "#E4ECF8"; selectionColor: Theme.accent
                font.family: Theme.monoFamily; font.pixelSize: 12
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.NoWrap
            }
        }

        // Image with checkerboard for transparency.
        Flickable {
            id: imageFlick
            visible: viewer.kind === "image"
            anchors.fill: parent; anchors.margins: 12
            contentWidth: Math.max(width, image.width)
            contentHeight: Math.max(height, image.height)
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            Item {
                width: imageFlick.contentWidth; height: imageFlick.contentHeight
                Grid {
                    anchors.fill: image
                    columns: Math.ceil(image.width / 12) + 1
                    clip: true
                    Repeater {
                        model: image.status === Image.Ready ? Math.min(4000, (Math.ceil(image.width / 12) + 1) * (Math.ceil(image.height / 12) + 1)) : 0
                        Rectangle {
                            required property int index
                            readonly property int cols: Math.ceil(image.width / 12) + 1
                            width: 12; height: 12
                            color: ((index % cols) + Math.floor(index / cols)) % 2 ? "#2A3346" : "#1F2738"
                        }
                    }
                }
                Image {
                    id: image
                    anchors.centerIn: parent
                    source: viewer.kind === "image" && viewer.controller ? viewer.controller.previewImage : ""
                    asynchronous: true
                    cache: false
                    smooth: true
                    mipmap: true
                    readonly property real s: viewer.zoom === 0 ? viewer.fitScale() : viewer.zoom
                    width: implicitWidth * s
                    height: implicitHeight * s
                }
            }
            WheelHandler {
                acceptedModifiers: Qt.NoModifier
                onWheel: (e) => viewer.zoomBy(e.angleDelta.y > 0 ? 1.15 : 1 / 1.15)
            }
        }
        Text {
            visible: viewer.kind === "image" && image.status === Image.Ready
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 10
            text: image.implicitWidth + " × " + image.implicitHeight + " px"
            color: Theme.inkMuted; font.pixelSize: 10; font.family: Theme.fontFamily
        }
        Text {
            visible: viewer.kind === "image" && image.status === Image.Error
            anchors.centerIn: parent
            text: "Format d'image non pris en charge"
            color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily
        }
    }

    // Accent frame while editing.
    Rectangle {
        visible: viewer.editing
        y: header.height; width: parent.width; height: parent.height - y
        color: "transparent"
        border.width: 1; border.color: viewer.dirty ? "#80FCD34D" : "#662F8CFF"
    }

    GlassDialog {
        touchMode: viewer.adaptive
        id: confirm
        objectName: "unsavedDialog"
        anchors.fill: parent
        bottomRightRadius: 16
        busy: viewer.saving
        onAccepted: {
            visible = false
            if (viewer.pendingAction === "close") { viewer.revert(); viewer.closed() }
            else if (viewer.pendingAction === "stop") viewer.revert()
            else if (viewer.pendingAction === "conflict") viewer.controller.saveFile(textEdit.text, true)
            else if (viewer.pendingAction === "unknown") viewer.controller.openFile(viewer.controller.previewPath)
            viewer.pendingAction = ""
        }
        onAlternate: {
            visible = false
            if (viewer.pendingAction === "close") { viewer.pendingAction = "closeAfterSave"; viewer.save() }
            else if (viewer.pendingAction === "conflict") { viewer.pendingAction = ""; viewer.controller.saveFileCopy(textEdit.text) }
        }
        onRejected: { viewer.pendingAction = ""; textEdit.forceActiveFocus() }
    }

    Connections {
        target: viewer.controller
        ignoreUnknownSignals: true
        function onSaveFinished(ok, message, code, path) {
            if (ok) {
                // A copy leaves the original open and still modified.
                if (message.indexOf("Copie") !== 0) viewer.dirty = false
                viewer.notice = message
                noticeTimer.restart()
                if (viewer.pendingAction === "closeAfterSave") { viewer.pendingAction = ""; viewer.editing = false; viewer.closed() }
                return
            }
            if (code === "conflict") {
                viewer.pendingAction = "conflict"
                confirm.openWith("Conflit : le fichier a changé sur le serveur",
                    message + " Votre version n'est pas perdue. Écraser remplace la version du serveur ;"
                    + " « Enregistrer une copie » garde les deux.", "Écraser la version du serveur", true, false, "")
                confirm.altText = "Enregistrer une copie"
                confirm.cancelText = "Continuer à éditer"
            } else if (code === "unknown") {
                viewer.pendingAction = "unknown"
                confirm.openWith("État de l'enregistrement inconnu", message
                    + " Copiez votre texte si besoin, puis rechargez le fichier pour voir la version du serveur.",
                    "Recharger depuis le serveur", true, false, "")
                confirm.cancelText = "Continuer à éditer"
            } else {
                viewer.pendingAction = ""
                viewer.notice = message
                noticeTimer.restart()
            }
        }
    }

    property string notice: ""
    Timer { id: noticeTimer; interval: 4500; onTriggered: viewer.notice = "" }
    Rectangle {
        visible: viewer.notice.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 14
        width: noticeText.implicitWidth + 24; height: 28; radius: 14
        color: "#F0101B2E"; border.width: 1; border.color: "#40FFFFFF"
        Text { id: noticeText; anchors.centerIn: parent; text: viewer.notice; color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily }
    }
}
