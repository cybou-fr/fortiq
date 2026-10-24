import QtQuick
import QtQuick.Controls

// Incident inspector over the Logs content: deterministic synopsis on the
// left (observed / hypotheses / unknown / next checks, each linked to event
// references), the events of the time window on the right. No AI. Every
// log string is rendered as plain text.
Rectangle {
    id: insp
    readonly property bool narrow: width < 650
    property bool showEvents: false
    readonly property real bodyTop: header.height + (narrow ? 44 : 0)
    property var lc: null            // LogsController or fixture sample
    property var priorityColor: function(p) { return "#64748B" }
    signal openUnit(string unit)
    signal closed()

    property var selectedRefs: []
    readonly property var events: lc ? lc.inspectorEvents : []
    readonly property int anchor: lc ? lc.inspectorAnchor : -1
    readonly property var syn: lc ? lc.synopsis : ({})

    color: "#FA0D1626"
    focus: visible
    Keys.onEscapePressed: insp.closed()
    onVisibleChanged: { selectedRefs = []; if (visible) Qt.callLater(scrollToAnchor) }
    onAnchorChanged: Qt.callLater(scrollToAnchor)

    function scrollToAnchor() { if (anchor >= 0) eventList.positionViewAtIndex(anchor, ListView.Center) }
    function selectRefs(refs) {
        selectedRefs = refs
        if (narrow) showEvents = true
        if (refs.length > 0) eventList.positionViewAtIndex(refs[0], ListView.Center)
    }
    function copyReport() {
        clip.text = lc && lc.inspectorReport ? lc.inspectorReport() : ""
        clip.selectAll(); clip.copy()
        copied.visible = true; copiedTimer.restart()
    }
    TextEdit { id: clip; visible: false }
    Timer { id: copiedTimer; interval: 2000; onTriggered: copied.visible = false }

    // ── header ─────────────────────────────────────────────────────────
    Item {
        id: header
        width: parent.width; height: insp.narrow ? 100 : 44
        Column {
            objectName: "incidentTitle"
            x: 14; y: insp.narrow ? 8 : (parent.height - height) / 2
            width: insp.narrow ? parent.width - 28 : parent.width - 28 - controls.width
            Text {
                width: parent.width; elide: Text.ElideRight
                textFormat: Text.PlainText
                text: insp.anchor >= 0 && insp.anchor < insp.events.length
                      ? "Inspection autour de " + insp.events[insp.anchor].timeText + " · " + insp.events[insp.anchor].unit
                      : "Inspection d'incident"
                color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold; font.family: Theme.fontFamily
            }
            Text {
                width: parent.width; elide: Text.ElideRight
                text: insp.lc && insp.lc.inspectorBusy ? "Lecture du journal…"
                    : (insp.lc && insp.lc.inspectorError.length > 0 ? insp.lc.inspectorError
                       : insp.events.length + " événements dans la fenêtre · règles déterministes, sans AI")
                color: insp.lc && insp.lc.inspectorError.length > 0 ? "#FCA5A5" : Theme.inkMuted
                font.pixelSize: 10; font.family: Theme.fontFamily
            }
        }
        Row {
            id: controls
            objectName: "incidentControls"
            anchors.right: parent.right; anchors.rightMargin: 8
            y: insp.narrow ? 48 : (parent.height - height) / 2
            spacing: 4
            Repeater {
                model: [ { s: 60, t: "±1 min" }, { s: 300, t: "±5 min" }, { s: 900, t: "±15 min" } ]
                Rectangle {
                    required property var modelData
                    anchors.verticalCenter: parent.verticalCenter
                    height: insp.narrow ? 44 : 24; radius: 6; width: wl.implicitWidth + 14
                    color: insp.lc && insp.lc.inspectorWindow === modelData.s ? Theme.selection : (wm.containsMouse ? "#26FFFFFF" : "transparent")
                    Text { id: wl; anchors.centerIn: parent; text: modelData.t; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                    MouseArea { id: wm; anchors.fill: parent; hoverEnabled: true; onClicked: if (insp.lc.setInspectorWindow) insp.lc.setInspectorWindow(modelData.s) }
                }
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: insp.narrow ? 44 : 24; radius: 6; width: cl.implicitWidth + 16
                color: cm.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
                Text { id: cl; anchors.centerIn: parent; text: "Copier le rapport"; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                MouseArea { id: cm; anchors.fill: parent; hoverEnabled: true; onClicked: insp.copyReport() }
                Rectangle {
                    id: copied
                    visible: false
                    anchors.top: parent.bottom; anchors.topMargin: 4; anchors.horizontalCenter: parent.horizontalCenter
                    width: ct.implicitWidth + 14; height: 20; radius: 6; color: "#F0101B2E"; z: 5
                    Text { id: ct; anchors.centerIn: parent; text: "Copié"; color: "#86EFAC"; font.pixelSize: 10 }
                }
            }
            ToolButtonGlass { touch: insp.narrow; glyph: "×"; tip: "Fermer (Échap)"; onClicked: insp.closed() }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1FFFFFFF" }
    }

    Row {
        visible: insp.narrow; y: header.height; width: parent.width
        Button { width: parent.width / 2; height: 44; palette.button: "#1F3354"; palette.buttonText: Theme.ink; text: "Synthèse"; onClicked: insp.showEvents = false }
        Button { width: parent.width / 2; height: 44; palette.button: "#1F3354"; palette.buttonText: Theme.ink; text: "Événements"; onClicked: insp.showEvents = true }
    }
    // ── synopsis ───────────────────────────────────────────────────────
    Flickable {
        id: synFlick
        visible: !insp.narrow || !insp.showEvents
        y: insp.bodyTop; width: insp.narrow ? parent.width : parent.width * 0.5; height: parent.height - y
        contentHeight: synCol.height + 20
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
            id: synCol
            x: 14; y: 10; width: synFlick.width - 26
            spacing: 12
            Repeater {
                model: [
                    { k: "observed", t: "OBSERVÉ", c: "#86EFAC", hint: "faits lus dans le journal" },
                    { k: "inferred", t: "SUPPOSÉ", c: "#FCD34D", hint: "hypothèses fondées sur le temps et le texte" },
                    { k: "unknown", t: "INCONNU", c: "#FCA5A5", hint: "limites de cette analyse" },
                    { k: "next", t: "À VÉRIFIER ENSUITE", c: "#93C5FD", hint: "lecture seule" }
                ]
                Column {
                    id: section
                    required property var modelData
                    readonly property var items: insp.syn && insp.syn[modelData.k] ? insp.syn[modelData.k] : []
                    width: synCol.width
                    spacing: 4
                    visible: items.length > 0
                    Row {
                        spacing: 8
                        Text { text: section.modelData.t; color: section.modelData.c; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.2; font.family: Theme.fontFamily }
                        Text { text: section.modelData.hint; color: "#7F93B5"; font.pixelSize: 10; font.family: Theme.fontFamily }
                    }
                    Repeater {
                        model: section.items
                        Rectangle {
                            id: stmt
                            required property var modelData
                            readonly property bool chosen: insp.selectedRefs === modelData.refs
                            width: section.width
                            height: stmtText.implicitHeight + refFlow.height + 12
                            radius: 8
                            color: chosen ? "#262F8CFF" : (sm.containsMouse ? "#12FFFFFF" : "#0AFFFFFF")
                            border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.06)
                            Rectangle { x: 0; y: 6; width: 3; height: parent.height - 12; radius: 1.5; color: section.modelData.c; opacity: 0.8 }
                            Text {
                                id: stmtText
                                x: 12; y: 6; width: parent.width - 20
                                wrapMode: Text.WordWrap; textFormat: Text.PlainText
                                text: stmt.modelData.text
                                color: stmt.modelData.action ? "#9CC6FF" : Theme.ink
                                font.pixelSize: 11; font.underline: stmt.modelData.action !== undefined && sm.containsMouse
                                font.family: Theme.fontFamily
                            }
                            Flow {
                                id: refFlow
                                x: 12; y: stmtText.y + stmtText.height + (height > 0 ? 4 : 0); width: parent.width - 20
                                spacing: 4
                                Repeater {
                                    model: stmt.modelData.refs.slice(0, 12)
                                    Rectangle {
                                        required property var modelData
                                        height: 16; radius: 8; width: rt.implicitWidth + 10
                                        color: "#262F8CFF"
                                        Text { id: rt; anchors.centerIn: parent; text: "#" + (modelData + 1); color: "#BFDBFE"; font.pixelSize: 9; font.family: Theme.monoFamily }
                                    }
                                }
                                Text {
                                    visible: stmt.modelData.refs.length > 12
                                    text: "+" + (stmt.modelData.refs.length - 12); color: "#7F93B5"; font.pixelSize: 9
                                }
                            }
                            MouseArea {
                                id: sm
                                anchors.fill: parent; hoverEnabled: true
                                cursorShape: stmt.modelData.refs.length > 0 || stmt.modelData.action ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: {
                                    if (stmt.modelData.action === "unit") insp.openUnit(stmt.modelData.arg)
                                    else if (stmt.modelData.action === "widen") insp.lc.setInspectorWindow(Number(stmt.modelData.arg))
                                    else insp.selectRefs(stmt.modelData.refs)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle { visible: !insp.narrow; x: synFlick.width; y: header.height; width: 1; height: parent.height - y; color: "#1FFFFFFF" }

    // ── events of the window ───────────────────────────────────────────
    ListView {
        id: eventList
        visible: !insp.narrow || insp.showEvents
        x: insp.narrow ? 0 : synFlick.width + 6; y: insp.bodyTop + 4
        width: parent.width - x - 6; height: parent.height - y - 4
        model: insp.events
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Item {
            id: row
            required property var modelData
            required property int index
            readonly property bool isAnchor: index === insp.anchor
            readonly property bool referenced: insp.selectedRefs.indexOf(index) >= 0
            width: eventList.width
            height: msg.implicitHeight + 24
            Rectangle {
                anchors.fill: parent; anchors.bottomMargin: 2; radius: 6
                color: row.referenced ? "#332F8CFF" : (row.isAnchor ? "#26FACC15" : (row.index % 2 ? "#08FFFFFF" : "transparent"))
                border.width: row.isAnchor ? 1 : 0; border.color: "#80FACC15"
            }
            Rectangle { x: 4; y: 5; width: 3; height: parent.height - 12; radius: 1.5; color: insp.priorityColor(row.modelData.priority) }
            Text {
                x: 12; y: 4
                text: "#" + (row.index + 1) + "  " + row.modelData.timeText + "  " + row.modelData.unit + (row.isAnchor ? "   ▶ choisi" : "")
                textFormat: Text.PlainText
                color: row.isAnchor ? "#FDE68A" : "#9FB2D0"; font.pixelSize: 10; font.family: Theme.monoFamily
            }
            Text {
                id: msg
                x: 12; y: 19; width: parent.width - 20
                wrapMode: Text.WrapAnywhere; maximumLineCount: 3; elide: Text.ElideRight
                textFormat: Text.PlainText
                text: row.modelData.message
                color: row.modelData.priority >= 0 && row.modelData.priority <= 3 ? "#FECACA" : Theme.ink
                font.pixelSize: 11; font.family: Theme.monoFamily
            }
        }
    }
    BusyIndicator { anchors.centerIn: eventList; running: insp.lc && insp.lc.inspectorBusy; visible: running }
}
