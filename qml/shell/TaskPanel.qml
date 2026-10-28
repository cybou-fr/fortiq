import QtQuick
import QtQuick.Controls.Fusion

// Shared task list (transfers, file operations, saves, searches) of every app,
// fed by the application's TaskManager. Progress, a stop request for tasks that
// can be stopped, and the user's decision when a transfer meets an existing name
// (keep both / replace after confirmation / skip).
Rectangle {
    id: panel
    property var manager: null        // TaskManager (or the reference fixture)
    property var controller: null     // RemoteController: resolves name conflicts
    property var tasks: manager ? manager.tasks : []
    property bool expanded: true
    // Host-provided vertical space; the list scrolls rather than running offscreen.
    property real availableHeight: 260
    readonly property int active: tasks.filter(t => t.active).length

    radius: 12
    color: "#F20E182A"
    border.width: 1
    border.color: "#33FFFFFF"
    height: expanded ? Math.min(header.height + list.contentHeight + 8, 260, Math.max(header.height, availableHeight)) : header.height
    clip: true
    Behavior on height { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    readonly property var kindLabels: ({
        upload: "Envoi", download: "Téléchargement", createFolder: "Nouveau dossier", rename: "Renommer",
        trash: "Corbeille", restore: "Restaurer", deletePermanently: "Suppression définitive", emptyTrash: "Vider la corbeille",
        save: "Enregistrement", saveCopy: "Copie enregistrée", search: "Recherche"
    })
    function sizeText(bytes) {
        if (bytes < 0) return "?"
        const units = ["o", "Ko", "Mo", "Go"]
        let v = bytes, u = 0
        while (v >= 1024 && u < units.length - 1) { v /= 1024; ++u }
        return (u === 0 ? v : v.toFixed(1)) + " " + units[u]
    }
    function glyph(t) {
        switch (t.kind) {
        case "upload": return "⤒"
        case "download": return "⤓"
        case "search": return "⌕"
        default: return "✎"
        }
    }
    function titleText(t) {
        const label = kindLabels[t.kind] || t.kind
        if (t.kind === "upload" || t.kind === "download") return t.title + (t.kind === "upload" ? "  →  " + t.destination : "")
        return label + " — " + t.title
    }
    function stateText(t) {
        if (t.decision.length > 0) return t.message
        switch (t.state) {
        case "queued": return "En attente"
        case "running": return t.total > 0 ? sizeText(t.done) + " / " + sizeText(t.total) : (t.message.length > 0 ? t.message : "En cours…")
        case "cancelling": return "Annulation en cours…"
        case "succeeded": return t.message
        case "cancelled": return t.message.length > 0 ? t.message : "Annulé"
        case "unknown": return "État inconnu — " + t.message
        default: return t.message
        }
    }
    function bad(t) { return t.state === "failed" || t.state === "unknown" }

    Item {
        id: header
        width: parent.width; height: 34
        Text {
            x: 14; anchors.verticalCenter: parent.verticalCenter
            text: "⇅  Tâches" + (panel.active > 0 ? " — " + panel.active + " en cours" : "")
            color: Theme.ink; font.pixelSize: 12; font.weight: Font.DemiBold; font.family: Theme.fontFamily
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Text {
                objectName: "taskClear"
                visible: panel.tasks.length > panel.active
                anchors.verticalCenter: parent.verticalCenter
                text: "Effacer terminées"
                color: "#9CC6FF"; font.pixelSize: 11; font.family: Theme.fontFamily
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: panel.manager.clearFinished() }
            }
            ToolButtonGlass { glyph: panel.expanded ? "⌄" : "⌃"; width: 26; height: 24; onClicked: panel.expanded = !panel.expanded }
        }
        MouseArea { anchors.fill: parent; z: -1; onDoubleClicked: panel.expanded = !panel.expanded }
    }

    ListView {
        id: list
        y: header.height
        width: parent.width; height: parent.height - y - 4
        model: panel.tasks
        spacing: 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Item {
            id: row
            required property var modelData
            readonly property var t: modelData
            property bool confirmReplace: false
            width: list.width
            height: t.decision === "conflict" ? 42 + conflictActions.childrenRect.height + 4 : 42

            SymbolGlyph {
                x: 14; y: 6
                glyph: panel.glyph(row.t)
                color: panel.bad(row.t) ? "#FCA5A5" : row.t.state === "succeeded" ? "#4ADE80" : Theme.accent
                size: 16
            }
            Text {
                x: 38; y: 4
                width: parent.width - x - actions.width - 16
                elide: Text.ElideMiddle
                text: panel.titleText(row.t)
                color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily
            }
            Text {
                x: 38; y: 21
                width: parent.width - x - actions.width - 16
                elide: Text.ElideRight
                text: panel.stateText(row.t)
                color: panel.bad(row.t) ? "#FCA5A5" : row.t.decision === "conflict" ? "#FCD34D" : Theme.inkMuted
                font.pixelSize: 10; font.family: Theme.fontFamily
            }
            Rectangle {
                visible: (row.t.state === "running" || row.t.state === "cancelling") && row.t.total > 0
                x: 38; y: 36; width: parent.width - x - 16; height: 3; radius: 1.5
                color: "#26FFFFFF"
                Rectangle {
                    width: row.t.total > 0 ? parent.width * Math.min(1, row.t.done / row.t.total) : 0
                    height: parent.height; radius: 1.5; color: Theme.accent
                }
            }
            Row {
                id: actions
                anchors.right: parent.right; anchors.rightMargin: 10; y: 8
                spacing: 4
                ToolButtonGlass {
                    objectName: "taskCancel"
                    visible: row.t.active && row.t.cancellable && row.t.state !== "cancelling"
                    glyph: "×"; tip: "Annuler"; width: 26; height: 24
                    onClicked: panel.manager.requestCancel(row.t.id)
                }
            }
            Flow {
                id: conflictActions
                visible: row.t.decision === "conflict"
                x: 38; y: 38
                width: Math.max(0, row.width - x - 10)
                spacing: 6
                Repeater {
                    model: row.confirmReplace
                        ? [ { k: "replace", t: "Confirmer le remplacement", danger: true }, { k: "back", t: "Retour", danger: false } ]
                        : [ { k: "keepBoth", t: "Garder les deux", danger: false }, { k: "ask", t: "Remplacer…", danger: false }, { k: "skip", t: "Ignorer", danger: false } ]
                    Rectangle {
                        required property var modelData
                        height: 22; radius: 6
                        width: label.implicitWidth + 16
                        color: modelData.danger ? "#B91C1C" : (btnMouse.containsMouse ? "#33FFFFFF" : "#1FFFFFFF")
                        Text { id: label; anchors.centerIn: parent; text: modelData.k === "replace" && row.width < 340 ? "Confirmer" : modelData.t; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                        MouseArea {
                            id: btnMouse
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.k === "ask") row.confirmReplace = true
                                else if (modelData.k === "back") row.confirmReplace = false
                                else if (panel.controller) panel.controller.resolveConflict(row.t.id, modelData.k)
                            }
                        }
                    }
                }
            }
        }
    }
}
