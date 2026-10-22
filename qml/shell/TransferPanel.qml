import QtQuick
import QtQuick.Controls

// Transfer queue inside the Files window: progress, cancel, and the
// conflict choice (keep both / replace after confirmation / skip).
Rectangle {
    id: panel
    property var controller: null
    property var transfers: controller ? controller.transfers : []
    property bool expanded: true
    readonly property int active: transfers.filter(t => t.state === "queued" || t.state === "running" || t.state === "conflict").length

    radius: 12
    color: "#F20E182A"
    border.width: 1
    border.color: "#33FFFFFF"
    height: expanded ? Math.min(header.height + list.contentHeight + 8, 260) : header.height
    clip: true
    Behavior on height { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    function sizeText(bytes) {
        if (bytes < 0) return "?"
        const units = ["o", "Ko", "Mo", "Go"]
        let v = bytes, u = 0
        while (v >= 1024 && u < units.length - 1) { v /= 1024; ++u }
        return (u === 0 ? v : v.toFixed(1)) + " " + units[u]
    }
    function stateText(t) {
        switch (t.state) {
        case "queued": return "En attente"
        case "running": return sizeText(t.done) + " / " + sizeText(t.total)
        case "conflict": return t.message
        case "done": return t.message
        case "cancelled": return t.message.length > 0 ? t.message : "Annulé"
        case "unknown": return "État inconnu — " + t.message
        default: return t.message
        }
    }

    Item {
        id: header
        width: parent.width; height: 34
        Text {
            x: 14; anchors.verticalCenter: parent.verticalCenter
            text: "⇅  Transferts" + (panel.active > 0 ? " — " + panel.active + " en cours" : "")
            color: Theme.ink; font.pixelSize: 12; font.weight: Font.DemiBold; font.family: Theme.fontFamily
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Text {
                visible: panel.transfers.length > panel.active
                anchors.verticalCenter: parent.verticalCenter
                text: "Effacer terminés"
                color: "#9CC6FF"; font.pixelSize: 11; font.family: Theme.fontFamily
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: panel.controller.clearFinishedTransfers() }
            }
            ToolButtonGlass { glyph: panel.expanded ? "⌄" : "⌃"; width: 26; height: 24; onClicked: panel.expanded = !panel.expanded }
        }
        MouseArea { anchors.fill: parent; z: -1; onDoubleClicked: panel.expanded = !panel.expanded }
    }

    ListView {
        id: list
        y: header.height
        width: parent.width; height: parent.height - y - 4
        model: panel.transfers
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
            height: t.state === "conflict" ? 64 : 42

            SymbolGlyph {
                x: 14; y: 6
                glyph: row.t.upload ? "⤒" : "⤓"
                color: row.t.state === "error" || row.t.state === "unknown" ? "#FCA5A5"
                     : row.t.state === "done" ? "#4ADE80" : Theme.accent
                size: 16
            }
            Text {
                x: 38; y: 4
                width: parent.width - x - actions.width - 16
                elide: Text.ElideMiddle
                text: row.t.name + (row.t.upload ? "  →  " + row.t.remote : "")
                color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily
            }
            Text {
                x: 38; y: 21
                width: parent.width - x - actions.width - 16
                elide: Text.ElideRight
                text: panel.stateText(row.t)
                color: row.t.state === "error" || row.t.state === "unknown" ? "#FCA5A5"
                     : row.t.state === "conflict" ? "#FCD34D" : Theme.inkMuted
                font.pixelSize: 10; font.family: Theme.fontFamily
            }
            Rectangle {
                visible: row.t.state === "running"
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
                    visible: row.t.state === "queued" || row.t.state === "running"
                    glyph: "×"; tip: "Annuler"; width: 26; height: 24
                    onClicked: panel.controller.cancelTransfer(row.t.id)
                }
            }
            Row {
                visible: row.t.state === "conflict"
                x: 38; y: 38; spacing: 6
                Repeater {
                    model: row.confirmReplace
                        ? [ { k: "replace", t: "Confirmer le remplacement", danger: true }, { k: "back", t: "Retour", danger: false } ]
                        : [ { k: "keepBoth", t: "Garder les deux", danger: false }, { k: "ask", t: "Remplacer…", danger: false }, { k: "skip", t: "Ignorer", danger: false } ]
                    Rectangle {
                        required property var modelData
                        height: 22; radius: 6
                        width: label.implicitWidth + 16
                        color: modelData.danger ? "#B91C1C" : (btnMouse.containsMouse ? "#33FFFFFF" : "#1FFFFFFF")
                        Text { id: label; anchors.centerIn: parent; text: modelData.t; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                        MouseArea {
                            id: btnMouse
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.k === "ask") row.confirmReplace = true
                                else if (modelData.k === "back") row.confirmReplace = false
                                else panel.controller.resolveConflict(row.t.id, modelData.k)
                            }
                        }
                    }
                }
            }
        }
    }
}
