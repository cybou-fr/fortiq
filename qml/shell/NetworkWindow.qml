import QtQuick
import QtQuick.Controls

// Réseau (lecture seule): interfaces, ports en écoute, routes et DNS du serveur,
// plus trois diagnostics lancés sur le serveur. Rien n'est modifié (docs/NETWORK.md).
Item {
    id: win
    objectName: "networkWindow"
    property bool adaptive: false
    property Item backdrop
    property var network: null
    property var remote: null
    property string section: "interfaces"
    property bool navigationOpen: false
    readonly property bool connected: remote !== null && remote.connected
    readonly property bool drawer: adaptive && width < 900
    readonly property real contentLeft: drawer ? 0 : 196
    signal closeRequested()
    signal requestServers()
    signal requestCopy(string what, string text)

    function sync() { if (network) network.setWindowActive(visible) }
    onVisibleChanged: sync()
    onNetworkChanged: sync()
    Component.onCompleted: sync()
    function handleBack() { if (navigationOpen) { navigationOpen = false; return true } return false }
    readonly property var sections: [
        { key: "interfaces", title: "Interfaces" }, { key: "ports", title: "Ports en écoute" },
        { key: "routes", title: "Routes" }, { key: "dns", title: "DNS" }, { key: "diag", title: "Diagnostic" }
    ]
    readonly property var listModel: !network ? [] : section === "interfaces" ? network.interfaces : section === "ports" ? network.listeners : section === "routes" ? network.routes : []

    Glass { anchors.fill: parent; backdrop: win.backdrop; tint: "#B3121C30"; blurAmount: 0.9; radius: 16 }
    MouseArea { visible: win.drawer && win.navigationOpen; anchors.fill: parent; z: 30; onClicked: win.navigationOpen = false }

    Rectangle {
        objectName: "networkSidebar"
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
        Text { x: 18; y: 48; text: "RÉSEAU"; color: Theme.inkMuted; font.pixelSize: 10; font.letterSpacing: 1 }
        Column {
            x: 8; y: 72; width: parent.width - 16; spacing: 6
            Repeater {
                model: win.sections
                Rectangle {
                    required property var modelData
                    objectName: "networkSection_" + modelData.key
                    width: parent.width; height: win.adaptive ? 44 : 34; radius: 8
                    color: win.section === modelData.key ? "#D02D73DA" : "transparent"
                    Text { x: 12; anchors.verticalCenter: parent.verticalCenter; text: modelData.title; color: Theme.ink; font.pixelSize: 12 }
                    Text {
                        visible: modelData.key === "ports" && win.network !== null && win.network.exposedCount > 0
                        anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                        text: win.network ? win.network.exposedCount : ""; color: "#FCD34D"; font.pixelSize: 11
                    }
                    MouseArea { anchors.fill: parent; onClicked: { win.section = modelData.key; win.navigationOpen = false } }
                }
            }
        }
        Text {
            x: 18; y: 270; width: parent.width - 36; wrapMode: Text.WordWrap
            text: "Serveur Linux sélectionné\nLecture seule : aucune modification du réseau"
            color: Theme.inkMuted; font.pixelSize: 11
        }
    }

    Item {
        x: win.contentLeft; width: parent.width - x; height: 48
        WindowDragArea { anchors.fill: parent; windowItem: win; movable: !win.adaptive }
        ToolButtonGlass { visible: win.drawer; x: 4; y: 2; touch: true; glyph: "☰"; onClicked: win.navigationOpen = !win.navigationOpen }
        Text {
            x: win.drawer ? 54 : 14; width: Math.max(0, parent.width - x - 52); anchors.verticalCenter: parent.verticalCenter
            text: "Réseau · " + (win.connected ? (win.network && win.network.hostname.length > 0 ? win.network.hostname : win.remote.serverLabel) : "Aucun serveur")
            elide: Text.ElideMiddle; textFormat: Text.PlainText; color: Theme.ink; font.pixelSize: 14
        }
        ToolButtonGlass {
            anchors.right: parent.right; anchors.rightMargin: 8; y: 4; touch: win.adaptive; glyph: "↻"; tip: "Actualiser"
            enabledState: win.connected && win.network !== null && !win.network.busy; onClicked: win.network.reload()
        }
    }

    Column {
        x: win.contentLeft + 16; y: 58; width: parent.width - x - 16; spacing: 4
        visible: win.section !== "diag" && win.section !== "dns"
        Text {
            width: parent.width; color: Theme.ink; font.pixelSize: 18
            text: win.section === "interfaces" ? "Interfaces réseau" : win.section === "ports" ? "Ports en écoute" : "Routes"
        }
        Text {
            width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText; font.pixelSize: 11
            color: win.network && win.network.state === "error" ? "#FCA5A5" : Theme.inkMuted
            text: win.network ? win.network.status + (win.network.updated ? " · mis à jour " + win.network.updated : "") : "Aucun serveur connecté"
        }
    }

    ListView {
        id: list
        objectName: "networkList"
        visible: win.connected && win.network !== null && (win.section === "interfaces" || win.section === "ports" || win.section === "routes")
        x: win.contentLeft + 12; y: 112; width: parent.width - x - 12; height: Math.max(0, parent.height - y - 12)
        clip: true; spacing: 8
        model: win.listModel
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Rectangle {
            required property var modelData
            width: ListView.view.width - 12
            height: win.section === "interfaces" ? 66 + Math.max(1, modelData.addresses.length) * 16 + (modelData.hasStats ? 18 : 0)
                  : win.section === "ports" ? 52 : 44
            radius: 10; color: "#30182740"; border.width: 1; border.color: "#25DCEBFF"

            // interfaces
            Rectangle {
                visible: win.section === "interfaces"
                x: 12; y: 14; width: 9; height: 9; radius: 4.5
                color: modelData.up ? "#4ADE80" : "#64748B"
            }
            Text {
                visible: win.section === "interfaces"
                x: 30; y: 8; width: parent.width - 42; elide: Text.ElideRight; textFormat: Text.PlainText
                text: win.section === "interfaces" ? modelData.name + "   " + modelData.state + (modelData.mtu ? "  ·  MTU " + modelData.mtu : "") : ""
                color: Theme.ink; font.pixelSize: 14
            }
            Text {
                visible: win.section === "interfaces"
                x: 30; y: 30; width: parent.width - 42; elide: Text.ElideRight; textFormat: Text.PlainText
                text: win.section === "interfaces" ? (modelData.mac && modelData.mac !== "00:00:00:00:00:00" ? modelData.mac : modelData.kind) : ""
                color: Theme.inkMuted; font.pixelSize: 11
            }
            Column {
                visible: win.section === "interfaces"
                x: 30; y: 50; width: parent.width - 42
                Repeater {
                    model: win.section === "interfaces" ? modelData.addresses : []
                    Text {
                        required property var modelData
                        width: parent.width; elide: Text.ElideMiddle; textFormat: Text.PlainText
                        text: modelData.text + "   " + modelData.scope
                        color: modelData.family === "inet6" ? "#9CC6FF" : Theme.ink; font.pixelSize: 11; font.family: Theme.monoFamily
                    }
                }
            }
            Text {
                visible: win.section === "interfaces" && modelData.hasStats
                x: 30; anchors.bottom: parent.bottom; anchors.bottomMargin: 6; width: parent.width - 42; elide: Text.ElideRight; textFormat: Text.PlainText
                text: win.section === "interfaces" ? "↓ " + modelData.rx + " (" + modelData.rxPackets + " paquets)   ↑ " + modelData.tx + " (" + modelData.txPackets + ")"
                        + (modelData.errors > 0 ? "   erreurs " + modelData.errors : "") + (modelData.dropped > 0 ? "   perdus " + modelData.dropped : "") : ""
                color: Theme.inkMuted; font.pixelSize: 10
            }

            // listening ports
            Text {
                visible: win.section === "ports"
                x: 12; y: 8; width: parent.width - 150; elide: Text.ElideRight; textFormat: Text.PlainText
                text: win.section === "ports" ? modelData.protocol.toUpperCase() + "  " + modelData.address + " : " + modelData.port : ""
                color: Theme.ink; font.pixelSize: 13; font.family: Theme.monoFamily
            }
            Text {
                visible: win.section === "ports"
                x: 12; y: 29; width: parent.width - 150; elide: Text.ElideRight; textFormat: Text.PlainText
                text: win.section === "ports" ? (modelData.process.length > 0 ? modelData.process + "  (pid " + modelData.pid + ")" : "processus : visible seulement pour votre utilisateur") : ""
                color: Theme.inkMuted; font.pixelSize: 11
            }
            Rectangle {
                visible: win.section === "ports"
                anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                height: 22; radius: 11; width: exposureText.implicitWidth + 20
                color: win.section === "ports" && modelData.exposure === "all" ? "#33FACC15" : (win.section === "ports" && modelData.exposure === "local" ? "#2616A34A" : "#26FFFFFF")
                Text {
                    id: exposureText; anchors.centerIn: parent; font.pixelSize: 10
                    text: win.section === "ports" ? (modelData.exposure === "all" ? "toutes les interfaces" : modelData.exposure === "local" ? "local seulement" : "une adresse") : ""
                    color: win.section === "ports" && modelData.exposure === "all" ? "#FDE68A" : Theme.ink
                }
            }

            // routes
            Text {
                visible: win.section === "routes"
                x: 12; y: 6; width: parent.width - 24; elide: Text.ElideRight; textFormat: Text.PlainText
                text: win.section === "routes" ? (modelData.isDefault ? "Route par défaut  " : "") + modelData.destination : ""
                color: win.section === "routes" && modelData.isDefault ? "#9CC6FF" : Theme.ink; font.pixelSize: 13; font.family: Theme.monoFamily
            }
            Text {
                visible: win.section === "routes"
                x: 12; y: 25; width: parent.width - 24; elide: Text.ElideRight; textFormat: Text.PlainText
                text: win.section === "routes" ? (modelData.gateway ? "via " + modelData.gateway + "  " : "") + "dev " + modelData.device
                        + (modelData.metric >= 0 ? "  ·  métrique " + modelData.metric : "") + (modelData.protocol ? "  ·  " + modelData.protocol : "") : ""
                color: Theme.inkMuted; font.pixelSize: 11
            }
        }
    }

    // DNS
    Flickable {
        visible: win.connected && win.network !== null && win.section === "dns"
        x: win.contentLeft + 16; y: 58; width: parent.width - x - 16; height: parent.height - y - 12
        contentWidth: width; contentHeight: dnsColumn.height; clip: true; boundsBehavior: Flickable.StopAtBounds
        Column {
            id: dnsColumn
            width: parent.width; spacing: 8
            Text { text: "DNS"; color: Theme.ink; font.pixelSize: 18 }
            Text { visible: win.network !== null && win.network.dnsNote.length > 0; width: parent.width; wrapMode: Text.WordWrap; text: win.network ? win.network.dnsNote : ""; color: Theme.inkMuted; font.pixelSize: 11 }
            Text { text: "SERVEURS"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2 }
            Repeater { model: win.network ? win.network.dnsServers : []; Text { required property string modelData; text: modelData; color: Theme.ink; font.pixelSize: 13; font.family: Theme.monoFamily } }
            Text { visible: win.network !== null && win.network.dnsServers.length === 0; text: "Aucun serveur DNS dans /etc/resolv.conf"; color: "#FCA5A5"; font.pixelSize: 12 }
            Text { visible: win.network !== null && win.network.dnsSearch.length > 0; topPadding: 6; text: "DOMAINES DE RECHERCHE"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2 }
            Text { visible: win.network !== null && win.network.dnsSearch.length > 0; width: parent.width; wrapMode: Text.WrapAnywhere; text: win.network ? win.network.dnsSearch.join("  ") : ""; color: Theme.ink; font.pixelSize: 12 }
            Text { visible: win.network !== null && win.network.resolvedText.length > 0; topPadding: 6; text: "SYSTEMD-RESOLVED"; color: "#8FA2C2"; font.pixelSize: 9; font.letterSpacing: 1.2 }
            Text { visible: win.network !== null && win.network.resolvedText.length > 0; width: parent.width; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; text: win.network ? win.network.resolvedText : ""; color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.monoFamily }
        }
    }

    // Diagnostic
    Item {
        id: diag
        visible: win.connected && win.network !== null && win.section === "diag"
        x: win.contentLeft + 16; y: 58; width: parent.width - x - 16; height: parent.height - y - 12
        Text { id: diagTitle; text: "Diagnostic depuis le serveur"; color: Theme.ink; font.pixelSize: 18 }
        Text {
            id: diagHint; y: 28; width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 11; color: Theme.inkMuted
            text: "Résolution, ping et chemin sont exécutés par le serveur (pas par cet appareil). Saisissez un nom d'hôte ou une adresse IP."
        }
        Rectangle {
            id: targetBox
            y: diagHint.y + diagHint.implicitHeight + 10; width: Math.min(parent.width, 420); height: win.adaptive ? 44 : 32; radius: 8
            color: "#1AFFFFFF"; border.width: 1; border.color: target.activeFocus ? Theme.accent : "#33FFFFFF"
            TextInput {
                id: target
                objectName: "networkTarget"
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; verticalAlignment: TextInput.AlignVCenter
                color: Theme.ink; font.pixelSize: 13; selectByMouse: true; clip: true
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                onAccepted: if (win.network) win.network.runProbe("resolve", text)
                Text { visible: parent.text.length === 0; anchors.verticalCenter: parent.verticalCenter; text: "exemple.fr ou 192.0.2.10"; color: "#7F93B5"; font.pixelSize: 13 }
            }
        }
        Row {
            id: probeButtons
            y: targetBox.y + targetBox.height + 8; spacing: 8
            Repeater {
                model: [ { k: "resolve", t: "Résoudre (DNS)" }, { k: "ping", t: "Ping ×4" }, { k: "trace", t: "Chemin" } ]
                Rectangle {
                    required property var modelData
                    objectName: "networkProbe_" + modelData.k
                    height: win.adaptive ? 44 : 30; radius: 8; width: probeLabel.implicitWidth + 24
                    opacity: win.network && !win.network.probeBusy ? 1 : 0.45
                    color: probeMouse.containsMouse ? Theme.selection : "#992D6FE0"
                    Text { id: probeLabel; anchors.centerIn: parent; text: modelData.t; color: "white"; font.pixelSize: 12 }
                    MouseArea { id: probeMouse; anchors.fill: parent; hoverEnabled: true; enabled: win.network !== null && !win.network.probeBusy; onClicked: win.network.runProbe(modelData.k, target.text) }
                }
            }
            Rectangle {
                visible: win.network !== null && win.network.probeBusy
                objectName: "networkProbeCancel"
                height: win.adaptive ? 44 : 30; radius: 8; width: cancelLabel.implicitWidth + 24; color: "#B91C1C"
                Text { id: cancelLabel; anchors.centerIn: parent; text: "Arrêter"; color: "white"; font.pixelSize: 12 }
                MouseArea { anchors.fill: parent; onClicked: win.network.cancelProbe() }
            }
        }
        Text {
            id: probeStatus; y: probeButtons.y + probeButtons.height + 8; width: parent.width - 90; textFormat: Text.PlainText; font.pixelSize: 11
            text: win.network ? (win.network.probeKind.length > 0 ? win.network.probeKind + " " + win.network.probeTarget + " — " : "") + win.network.probeStatus : ""
            color: win.network && win.network.probeStatus.indexOf("Saisissez") === 0 ? "#FCA5A5" : Theme.inkMuted
        }
        CopyChip {
            objectName: "networkCopyProbe"
            visible: win.network !== null && win.network.probeOutput.length > 0
            anchors.right: parent.right; y: probeStatus.y - 3; touch: win.adaptive
            onClicked: win.requestCopy("résultat du diagnostic", (win.network.probeKind.length > 0 ? win.network.probeKind + " " + win.network.probeTarget + "\n" : "") + win.network.probeOutput)
        }
        Flickable {
            y: probeStatus.y + probeStatus.implicitHeight + 6; width: parent.width; height: Math.max(0, parent.height - y)
            contentWidth: width; contentHeight: probeText.height + 12; clip: true; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            TextEdit {
                id: probeText
                objectName: "networkProbeOutput"
                width: parent.width - 8; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; textFormat: TextEdit.PlainText
                text: win.network ? win.network.probeOutput : ""; color: Theme.ink; font.pixelSize: 12; font.family: Theme.monoFamily
            }
        }
    }

    // No connection / explicit error
    Column {
        visible: !win.connected; x: win.contentLeft + 16; y: 150; width: parent.width - x - 16; spacing: 18
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: "Aucun serveur connecté"; color: Theme.ink; font.pixelSize: 16 }
        Button { objectName: "networkConnectButton"; anchors.horizontalCenter: parent.horizontalCenter; text: "Choisir un serveur…"; onClicked: win.requestServers() }
    }
    Text {
        visible: win.connected && win.network !== null && win.network.state === "error" && (win.section === "interfaces" || win.section === "ports" || win.section === "routes" || win.section === "dns")
        x: win.contentLeft + 20; y: 165; width: parent.width - x - 20; wrapMode: Text.WordWrap; textFormat: Text.PlainText
        text: win.network ? win.network.status : ""; color: "#FCA5A5"; font.pixelSize: 13
    }
    Text {
        visible: win.connected && win.network !== null && win.network.state === "ready" && win.section !== "diag" && win.section !== "dns" && list.count === 0
        x: win.contentLeft + 20; y: 165; width: parent.width - x - 20; wrapMode: Text.WordWrap; color: Theme.inkMuted; font.pixelSize: 13
        text: win.section === "ports" ? "Aucun port en écoute signalé par ss." : "Aucune entrée."
    }
    Text {
        x: win.contentLeft + 12; anchors.bottom: parent.bottom; anchors.bottomMargin: 8; width: parent.width - x - 12; elide: Text.ElideRight; textFormat: Text.PlainText
        visible: win.network !== null && win.network.notes.length > 0 && win.section !== "diag"
        text: win.network ? win.network.notes.join("  ·  ") : ""; color: Theme.inkMuted; font.pixelSize: 10
    }
    LinkBanner { remote: win.remote; touch: win.adaptive; onRetry: if (win.network && !win.network.busy) win.network.reload() }
}
