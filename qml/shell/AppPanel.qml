import QtQuick
import QtQuick.Controls.Fusion

// The single application panel (owner decision 22.10.2026: one panel instead
// of rail + dock), docked on the left or at the bottom. Apps that do not
// exist yet are dimmed and say "bientôt" instead of launching. Right-click
// the panel to move it.
Item {
    id: panel
    property bool fixture:false
    property Item backdrop
    property string edge: "bottom"          // "left" | "bottom"
    property var running: ({})               // app -> true when its window is open
    signal launch(string app)
    signal moveTo(string edge)

    readonly property bool vertical: edge === "left"
    readonly property var apps: [
        { app: "launcher", kind: "apps", label: "Applications", ready: true },
        { app: "files", kind: "files", label: "Fichiers", ready: true },
        { app: "logs", kind: "logs", label: "Journaux", ready: true },
        { app: "terminal", kind: "terminal", label: "Terminal", ready: true },
        { app: "editor", kind: "editor", label: "Éditeur", ready: !fixture },
        { app: "browser", kind: "browser", label: "Navigateur", ready: !fixture },
        { app: "settings", kind: "settings", label: "Paramètres", ready: !fixture }
    ].concat(fixture?[]:[{app:"disks",kind:"disks",label:"Disques",ready:true},{app:"network",kind:"network",label:"Réseau",ready:true},{app:"system",kind:"system",label:"Système",ready:true},{app:"db",kind:"db",label:"Bases",ready:true},{app:"mail",kind:"mail",label:"Courrier",ready:true},{app:"docs",kind:"docs",label:"MDok",ready:true}])
    readonly property int cell: vertical ? 74 : 60

    Glass {
        id: body
        anchors.fill: parent
        backdrop: panel.backdrop
        radius: 18
        tint: Theme.rail
    }
    onXChanged: body.syncOrigin()
    onYChanged: body.syncOrigin()
    onWidthChanged: body.syncOrigin()
    onHeightChanged: body.syncOrigin()

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: (m) => edgeMenu.popup()
    }

    Flickable {
        anchors.fill:parent;anchors.bottomMargin:panel.vertical?90:0
        clip:true;interactive:panel.vertical
        contentWidth:width;contentHeight:panel.vertical?grid.height+24:height
    Grid {
        id: grid
        x: panel.vertical ? (parent.width - width) / 2 : 12
        y: panel.vertical ? 12 : (panel.height - height) / 2
        columns: panel.vertical ? 1 : panel.apps.length + 2
        spacing: panel.vertical ? 4 : 6

        Repeater {
            model: panel.apps
            Item {
                id: slot
                required property var modelData
                required property int index
                width: panel.vertical ? 76 : 54
                height: panel.cell
                opacity: modelData.ready ? 1 : 0.45
                Rectangle {
                    anchors.fill: parent; anchors.margins: 2; radius: 12
                    color: slotMouse.containsMouse && slot.modelData.ready ? "#1FFFFFFF" : "transparent"
                }
                AppIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: panel.vertical ? 6 : 8
                    size: slot.modelData.app === "launcher" ? 30 : 40
                    kind: slot.modelData.kind
                }
                Text {
                    visible: panel.vertical
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 52
                    text: slot.modelData.label
                    color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily
                }
                Rectangle { // running indicator
                    visible: panel.running[slot.modelData.app] === true
                    width: 5; height: 5; radius: 2.5; color: "#93C5FD"
                    x: panel.vertical ? 3 : (parent.width - width) / 2
                    y: panel.vertical ? (parent.height - height) / 2 : parent.height - 6
                }
                Rectangle { // separator after the launcher button
                    visible: slot.index === 0
                    color: "#33FFFFFF"
                    width: panel.vertical ? 44 : 1; height: panel.vertical ? 1 : 36
                    x: panel.vertical ? (parent.width - width) / 2 : parent.width + 2
                    y: panel.vertical ? parent.height + 1 : (parent.height - height) / 2
                }
                MouseArea {
                    id: slotMouse
                    anchors.fill: parent; hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: slot.modelData.ready ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: (m) => {
                        if (m.button === Qt.RightButton) edgeMenu.popup()
                        else if (slot.modelData.ready) panel.launch(slot.modelData.app)
                    }
                }
                ToolTipLite {
                    text: slot.modelData.ready ? slot.modelData.label : slot.modelData.label + " — bientôt"
                    shown: slotMouse.containsMouse && (!panel.vertical || !slot.modelData.ready)
                }
            }
        }
    }

    }

    // Trash at the far end of the panel.
    Item {
        width: panel.vertical ? 76 : 54; height: panel.cell
        x: panel.vertical ? (parent.width - width) / 2 : parent.width - width - 10
        y: panel.vertical ? parent.height - height - 10 : (parent.height - height) / 2
        AppIcon { anchors.horizontalCenter: parent.horizontalCenter; y: panel.vertical ? 6 : 10; size: 36; kind: "trash" }
        Text {
            visible: panel.vertical
            anchors.horizontalCenter: parent.horizontalCenter; y: 50
            text: "Corbeille"; color: Theme.ink; font.pixelSize: 11; font.family: Theme.fontFamily
        }
        MouseArea { id: trashMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: panel.launch("trash") }
        ToolTipLite { text: "Corbeille du serveur"; shown: trashMouse.containsMouse && !panel.vertical }
    }

    Menu {
        id: edgeMenu
        palette.window: "#152238"; palette.windowText: "#F5F9FF"; palette.highlight: "#2D6FE0"
        palette.text: "#F5F9FF"; palette.base: "#152238"; palette.light: "#152238"; palette.mid: "#2A3A58"
        palette.highlightedText: "#FFFFFF"; palette.buttonText: "#F5F9FF"
        MenuItem { text: (panel.edge === "left" ? "✓  " : "     ") + "Ancrer à gauche"; onTriggered: panel.moveTo("left") }
        MenuItem { text: (panel.edge === "bottom" ? "✓  " : "     ") + "Ancrer en bas"; onTriggered: panel.moveTo("bottom") }
    }
}
