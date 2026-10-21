import QtQuick
import QtQuick.Controls

// Read-only service view: systemctl status fields, 7-day starts / failures /
// restarts and the lifecycle history reported by systemd. FORTIQ never
// starts, stops or edits units here.
Item {
    id: panel
    property var lc: null
    signal showJournal()

    readonly property var info: lc ? lc.serviceInfo : ({})
    readonly property var history: lc ? lc.serviceHistory : []
    readonly property var days: lc ? lc.serviceDays : []
    readonly property int maxDay: {
        let m = 1
        for (const d of days) m = Math.max(m, d.starts + d.failures + d.restarts)
        return m
    }

    function stateColor(active, sub) {
        if (active === "failed" || sub === "failed") return "#F87171"
        if (active === "active") return "#4ADE80"
        if (active === "activating" || active === "reloading") return "#FACC15"
        return "#94A3B8"
    }
    function kindColor(k) {
        return k === "failed" || k === "gaveup" ? "#F87171" : (k === "restart" ? "#FB923C" : (k === "exited" ? "#FACC15" : (k === "started" ? "#4ADE80" : "#94A3B8")))
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.height + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: col
            x: 14; y: 10; width: parent.width - 28
            spacing: 12

            Text {
                visible: panel.lc && panel.lc.serviceError.length > 0
                width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                text: panel.lc ? panel.lc.serviceError : ""
                color: "#FCA5A5"; font.pixelSize: 12; font.family: Theme.fontFamily
            }

            // Status card
            Rectangle {
                width: parent.width; height: statusCol.height + 24; radius: 10
                color: "#0FFFFFFF"; border.width: 1; border.color: "#1FFFFFFF"
                Column {
                    id: statusCol
                    x: 14; y: 12; width: parent.width - 28
                    spacing: 8
                    Row {
                        spacing: 10
                        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 12; height: 12; radius: 6; color: panel.stateColor(panel.info.active, panel.info.sub) }
                        Text {
                            textFormat: Text.PlainText
                            text: (panel.info.active || "…") + (panel.info.sub ? " (" + panel.info.sub + ")" : "")
                                  + (panel.info.sinceText ? "  depuis " + panel.info.sinceText : "")
                            color: Theme.ink; font.pixelSize: 15; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                        }
                    }
                    Text {
                        width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText
                        text: panel.info.description || ""
                        color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
                    }
                    Grid {
                        columns: Math.max(2, Math.floor(statusCol.width / 170))
                        columnSpacing: 18; rowSpacing: 6
                        Repeater {
                            model: [
                                { k: "Activation au démarrage", v: panel.info.enabled },
                                { k: "Dernier résultat", v: panel.info.result },
                                { k: "Redémarrages auto.", v: panel.info.restarts !== undefined ? String(panel.info.restarts) : "" },
                                { k: "Politique Restart=", v: panel.info.restartPolicy },
                                { k: "PID principal", v: panel.info.pid },
                                { k: "Mémoire", v: panel.info.memory },
                                { k: "Temps CPU", v: panel.info.cpu },
                                { k: "Tâches", v: panel.info.tasks },
                                { k: "Statut de sortie", v: panel.info.exitStatus }
                            ].filter(x => x.v !== undefined && x.v !== null && String(x.v).length > 0)
                            Column {
                                required property var modelData
                                width: 150
                                Text { text: modelData.k; color: "#7F93B5"; font.pixelSize: 9; font.family: Theme.fontFamily }
                                Text { text: String(modelData.v); textFormat: Text.PlainText; color: Theme.ink; font.pixelSize: 12; font.family: Theme.fontFamily }
                            }
                        }
                    }
                    Text {
                        visible: (panel.info.fragment || "").length > 0
                        width: parent.width; elide: Text.ElideMiddle; textFormat: Text.PlainText
                        text: "Fichier d'unité : " + panel.info.fragment
                        color: "#7F93B5"; font.pixelSize: 10; font.family: "Consolas"
                    }
                    Row {
                        spacing: 10
                        Rectangle {
                            height: 26; radius: 7; width: jl.implicitWidth + 20
                            color: jm.containsMouse ? Theme.selection : "#802D6FE0"
                            Text { id: jl; anchors.centerIn: parent; text: "Voir le journal de ce service →"; color: "white"; font.pixelSize: 11; font.family: Theme.fontFamily }
                            MouseArea { id: jm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: panel.showJournal() }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Lecture seule : FORTIQ ne démarre, n'arrête ni ne modifie les services."
                            color: "#7F93B5"; font.pixelSize: 10; font.family: Theme.fontFamily
                        }
                    }
                }
            }

            // Seven days
            Column {
                width: parent.width
                spacing: 6
                Row {
                    spacing: 12
                    Text { text: "7 DERNIERS JOURS"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2; font.bold: true; font.family: Theme.fontFamily }
                    Repeater {
                        model: [ { c: "#4ADE80", t: "démarrages" }, { c: "#FB923C", t: "redémarrages" }, { c: "#F87171", t: "échecs" } ]
                        Row {
                            required property var modelData
                            spacing: 4
                            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 8; height: 8; radius: 2; color: modelData.c }
                            Text { text: modelData.t; color: "#9FB2D0"; font.pixelSize: 10; font.family: Theme.fontFamily }
                        }
                    }
                }
                Row {
                    height: 86
                    spacing: 6
                    Repeater {
                        model: panel.days
                        Item {
                            required property var modelData
                            width: (col.width - 36) / 7; height: 86
                            Column {
                                anchors.bottom: dayLabel.top; anchors.bottomMargin: 3
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Math.min(34, parent.width - 8)
                                Repeater {
                                    model: [ { n: modelData.failures, c: "#F87171" }, { n: modelData.restarts, c: "#FB923C" }, { n: modelData.starts, c: "#4ADE80" } ]
                                    Rectangle {
                                        required property var modelData
                                        width: parent.width; height: modelData.n * 60 / panel.maxDay
                                        color: modelData.c; radius: 2
                                    }
                                }
                            }
                            Text {
                                id: dayLabel
                                anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label + ((modelData.starts + modelData.failures + modelData.restarts) > 0
                                      ? "  " + (modelData.starts + modelData.failures + modelData.restarts) : "")
                                color: "#9FB2D0"; font.pixelSize: 9; font.family: Theme.fontFamily
                            }
                        }
                    }
                }
            }

            // History
            Column {
                width: parent.width
                spacing: 3
                Text { text: "HISTORIQUE (systemd, 7 jours)"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2; font.bold: true; font.family: Theme.fontFamily }
                Text {
                    visible: panel.info.historyLimited === true
                    width: parent.width; wrapMode: Text.WordWrap
                    text: "Accès limité : les messages de systemd sur les services système ne sont visibles qu'aux groupes adm ou systemd-journal ; l'historique peut être vide ou incomplet."
                    color: "#FCD34D"; font.pixelSize: 11; font.family: Theme.fontFamily
                }
                Text {
                    visible: panel.history.length === 0 && !(panel.lc && panel.lc.serviceBusy)
                    text: "Aucun démarrage, arrêt ou échec visible sur 7 jours."
                    color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
                }
                Repeater {
                    model: panel.history.slice(0, 200)
                    Rectangle {
                        required property var modelData
                        required property int index
                        width: col.width; height: 26; radius: 6
                        color: index % 2 ? "#08FFFFFF" : "transparent"
                        Rectangle { x: 6; anchors.verticalCenter: parent.verticalCenter; width: 8; height: 8; radius: 4; color: panel.kindColor(modelData.kind) }
                        Text { x: 22; anchors.verticalCenter: parent.verticalCenter; width: 104; text: modelData.timeText; color: "#9FB2D0"; font.pixelSize: 11; font.family: "Consolas" }
                        Text {
                            x: 130; anchors.verticalCenter: parent.verticalCenter; width: parent.width - x - 8; elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: modelData.label + (modelData.detail ? " — " + modelData.detail : "")
                            color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily
                        }
                    }
                }
                Text {
                    visible: panel.history.length > 200
                    text: "… " + (panel.history.length - 200) + " plus anciens non affichés"
                    color: "#7F93B5"; font.pixelSize: 10
                }
            }
        }
    }
    BusyIndicator { anchors.centerIn: parent; running: panel.lc && panel.lc.serviceBusy; visible: running }
}
