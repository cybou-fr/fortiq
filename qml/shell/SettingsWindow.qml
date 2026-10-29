import QtQuick

// Paramètres: how this desktop looks on this device. Only local display preferences live here (wallpaper, where the app panel
// sits); everything about a server stays on the server. Nothing here is sent anywhere.
Item {
    id: win
    objectName: "settingsWindow"
    property bool adaptive: false
    property Item backdrop
    property var wallpapers: []          // names of the bundled wallpapers
    property string wallpaperName: ""    // "" = the plain placeholder
    property string panelEdge: "bottom"
    property string section: "appearance"
    property string appVersion: ""
    signal closeRequested()
    signal chooseWallpaper(string name)
    signal chooseEdge(string edge)
    signal requestServers()

    readonly property var sections: [
        { key: "appearance", title: "Apparence" }, { key: "servers", title: "Serveurs" }, { key: "about", title: "À propos" }
    ]

    Glass { anchors.fill: parent; backdrop: win.backdrop; tint: Theme.appWindow; blurAmount: 0.9; radius: 16 }
    WindowDragArea { anchors.left: parent.left; anchors.right: parent.right; height: 44; windowItem: win; movable: !win.adaptive }

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
    Text { x: win.adaptive ? 16 : 90; y: 12; text: "Paramètres"; color: Theme.ink; font.pixelSize: 15; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
    Rectangle {
        visible: win.adaptive
        objectName: "settingsClose"
        x: parent.width - 52; y: 6; width: 44; height: 36; radius: 10; color: "#1FFFFFFF"
        Text { anchors.centerIn: parent; text: "✕"; color: Theme.ink; font.pixelSize: 14 }
        MouseArea { anchors.fill: parent; onClicked: win.closeRequested() }
    }

    Row {
        x: 16; y: 52; spacing: 8
        Repeater {
            model: win.sections
            Rectangle {
                required property var modelData
                objectName: "settingsSection_" + modelData.key
                width: label.implicitWidth + 28; height: 32; radius: 16
                color: win.section === modelData.key ? "#503B82F6" : (sm.containsMouse ? "#26FFFFFF" : "#14FFFFFF")
                Text { id: label; anchors.centerIn: parent; text: parent.modelData.title; color: Theme.ink; font.pixelSize: 13; font.family: Theme.fontFamily }
                MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.section = parent.modelData.key }
            }
        }
    }

    Item {
        x: 16; y: 100; width: parent.width - 32; height: parent.height - 116
        clip: true

        Column {
            visible: win.section === "appearance"
            width: parent.width; spacing: 14
            Text { text: "Fond d’écran"; color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
            Flow {
                width: parent.width; spacing: 12
                Repeater {
                    model: win.wallpapers.concat([""])
                    Rectangle {
                        required property string modelData
                        objectName: "settingsWallpaper_" + (modelData.length > 0 ? modelData : "none")
                        readonly property bool current: win.wallpaperName === modelData
                        width: 150; height: 100; radius: 10; color: "#33253350"
                        border.width: current ? 3 : 1; border.color: current ? Theme.accent : "#40FFFFFF"
                        Image {
                            anchors.fill: parent; anchors.margins: 3
                            visible: parent.modelData.length > 0
                            source: parent.modelData.length > 0 ? Qt.resolvedUrl("../../assets/wallpapers/" + parent.modelData + ".jpg") : ""
                            fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 300
                        }
                        Text { anchors.centerIn: parent; visible: parent.modelData.length === 0; text: "Uni"; color: Theme.ink; font.pixelSize: 13; font.family: Theme.fontFamily }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.chooseWallpaper(parent.modelData) }
                    }
                }
            }
            Text { text: "Barre des applications"; color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
            Row {
                spacing: 10
                Repeater {
                    model: [ { k: "bottom", t: "En bas" }, { k: "left", t: "À gauche" } ]
                    Rectangle {
                        required property var modelData
                        objectName: "settingsEdge_" + modelData.k
                        width: 120; height: 36; radius: 18
                        color: win.panelEdge === modelData.k ? "#503B82F6" : (em.containsMouse ? "#26FFFFFF" : "#14FFFFFF")
                        Text { anchors.centerIn: parent; text: parent.modelData.t; color: Theme.ink; font.pixelSize: 13; font.family: Theme.fontFamily }
                        MouseArea { id: em; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.chooseEdge(parent.modelData.k) }
                    }
                }
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Ces choix restent sur cet appareil. Clic droit sur la barre : la déplacer."
                color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily
            }
        }

        Column {
            visible: win.section === "servers"
            width: parent.width; spacing: 14
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Les serveurs enregistrés (adresse, utilisateur, empreinte vérifiée) se gèrent depuis l’écran de connexion : ajout, modification, retrait, mot de passe mémorisé."
                color: Theme.ink; font.pixelSize: 13; font.family: Theme.fontFamily
            }
            Rectangle {
                objectName: "settingsOpenServers"
                width: 220; height: 36; radius: 18; color: osm.containsMouse ? "#66FFFFFF" : "#CC2D6FE0"
                Text { anchors.centerIn: parent; text: "Ouvrir la liste des serveurs"; color: "white"; font.pixelSize: 13; font.family: Theme.fontFamily }
                MouseArea { id: osm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.requestServers() }
            }
        }

        Column {
            visible: win.section === "about"
            width: parent.width; spacing: 10
            Text { text: "FORTIQ" + (win.appVersion.length > 0 ? " " + win.appVersion : ""); color: Theme.ink; font.pixelSize: 16; font.weight: Font.DemiBold; font.family: Theme.fontFamily }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Un bureau Linux distant : tout le travail se passe sur votre serveur, par SSH uniquement. Ce client ne se connecte à aucun autre service."
                color: Theme.ink; font.pixelSize: 13; font.family: Theme.fontFamily
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: "Construit avec Qt (LGPLv3), libssh2, OpenSSL et zlib. Les textes de licence accompagnent l’application (dossier licenses)."
                color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily
            }
        }
    }
}
