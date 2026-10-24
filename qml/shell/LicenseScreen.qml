import QtQuick

// Product licence (docs/LICENSING.md): enter a key, buy, restore, see the state, free this device. It covers the shell when the build
// requires a licence and none is active, and opens from the session menu otherwise. It never touches the user's servers or data; the key
// is typed here and goes to the License API only through `license.activate`.
Rectangle {
    id: screen
    objectName: "licenseScreen"
    property var license: null
    property bool adaptive: false
    signal closeRequested()
    color: "#F2070D1A"

    readonly property string st: license ? license.state : "none"
    readonly property bool busy: !!license && license.busy
    readonly property bool hasLicense: st === "active" || st === "grace"
    readonly property string title: st === "active" ? "Licence active"
        : st === "grace" ? "Licence active (hors ligne)"
        : st === "checking" ? "Vérification…"
        : st === "expired" ? "Licence expirée"
        : st === "disabled" ? "Licence désactivée"
        : st === "refresh" ? "Vérification nécessaire"
        : "Activer FORTIQ"

    // Swallows input so nothing behind the screen can be reached while it is up.
    MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: keyField.forceActiveFocus() }

    Column {
        anchors.centerIn: parent
        width: Math.min(460, screen.width - 32)
        spacing: 12

        Text {
            objectName: "licenseTitle"
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: screen.title; color: "white"; font.pixelSize: 22; font.family: Theme.fontFamily
        }
        Text {
            objectName: "licenseSummary"
            visible: screen.hasLicense || (!!screen.license && screen.license.hasKey)
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: screen.license ? [screen.license.tierLabel, screen.license.expiresText, screen.license.maskedKey].filter(s => s.length > 0).join(" · ") : ""
            color: "#C9D6F0"; font.pixelSize: 13; font.family: Theme.fontFamily
        }
        Text {
            objectName: "licenseMessage"
            visible: text.length > 0
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: screen.license ? screen.license.message : ""
            color: screen.hasLicense ? "#9FB1CE" : "#FFB4A8"; font.pixelSize: 13; font.family: Theme.fontFamily
        }

        // Key entry: visible whenever there is no usable licence.
        Row {
            visible: !screen.hasLicense
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            Rectangle {
                width: Math.min(360, screen.width - 96); height: screen.adaptive ? 48 : 40; radius: 20
                color: "#590A1224"; border.width: 1; border.color: keyField.activeFocus ? Theme.accent : "#66FFFFFF"
                TextInput {
                    id: keyField
                    objectName: "licenseKey"
                    anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18
                    verticalAlignment: TextInput.AlignVCenter
                    enabled: !screen.busy
                    color: "white"; font.pixelSize: 14; font.family: Theme.fontFamily
                    inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhSensitiveData
                    onAccepted: submit.clicked()
                    Text {
                        visible: parent.text.length === 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Clé de licence"; color: "#9FB1CE"; font.pixelSize: 14; font.family: Theme.fontFamily
                    }
                }
            }
            Rectangle {
                id: submit
                objectName: "licenseSubmit"
                signal clicked()
                width: screen.adaptive ? 48 : 40; height: width; radius: width / 2
                color: sm.containsMouse ? Theme.selection : "#CC2D6FE0"
                opacity: screen.busy ? 0.5 : 1
                Text { anchors.centerIn: parent; text: screen.busy ? "…" : "→"; color: "white"; font.pixelSize: 20 }
                MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; enabled: !screen.busy; onClicked: submit.clicked() }
                onClicked: if (screen.license) { screen.license.activate(keyField.text); keyField.text = "" }
            }
        }

        Row {
            visible: !screen.hasLicense
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            Repeater {
                model: [ { id: "intro", label: "Acheter Intro (14 jours)" }, { id: "pro", label: "Acheter Pro (annuel)" } ]
                delegate: Rectangle {
                    required property var modelData
                    objectName: "licenseBuy_" + modelData.id
                    width: Math.min(200, (screen.width - 42) / 2); height: 36; radius: 9
                    color: bm.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
                    Text { anchors.centerIn: parent; text: parent.modelData.label; color: "white"; font.pixelSize: 13; font.family: Theme.fontFamily }
                    MouseArea { id: bm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: if (screen.license) screen.license.buy(parent.modelData.id) }
                }
            }
        }

        // Account actions.
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            Repeater {
                model: [
                    { id: "manage", label: "Gérer l’abonnement", shown: true },
                    { id: "release", label: "Libérer cet appareil", shown: !!screen.license && screen.license.hasKey },
                    { id: "recheck", label: "Revérifier", shown: !!screen.license && screen.license.hasKey }
                ]
                delegate: Rectangle {
                    required property var modelData
                    visible: modelData.shown
                    objectName: "licenseAction_" + modelData.id
                    width: visible ? Math.min(150, (screen.width - 52) / 3) : 0; height: 32; radius: 9
                    color: am.containsMouse ? "#33FFFFFF" : "#14FFFFFF"
                    opacity: screen.busy ? 0.5 : 1
                    Text { anchors.centerIn: parent; width: parent.width - 8; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                        text: parent.modelData.label; color: "#C9D6F0"; font.pixelSize: 12; font.family: Theme.fontFamily }
                    MouseArea { id: am; anchors.fill: parent; hoverEnabled: true; enabled: !screen.busy; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (!screen.license) return
                            if (parent.modelData.id === "manage") screen.license.manage()
                            else if (parent.modelData.id === "release") screen.license.deactivateThisDevice()
                            else screen.license.validateNow()
                        }
                    }
                }
            }
        }

        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: "Vos serveurs et vos données ne sont jamais supprimés ni verrouillés par la licence."
            color: "#7F90B0"; font.pixelSize: 11; font.family: Theme.fontFamily
        }
        Text {
            visible: !!screen.license && !screen.license.canRemember
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: "Ce système n’a pas de coffre protégé : la clé devra être ressaisie à chaque démarrage."
            color: "#FFD58A"; font.pixelSize: 11; font.family: Theme.fontFamily
        }
        Rectangle {
            visible: !!screen.license && (!screen.license.required || screen.license.entitled)
            objectName: "licenseClose"
            anchors.horizontalCenter: parent.horizontalCenter
            width: 120; height: 34; radius: 9
            color: cm.containsMouse ? "#33FFFFFF" : "#1FFFFFFF"
            Text { anchors.centerIn: parent; text: "Fermer"; color: "white"; font.pixelSize: 13; font.family: Theme.fontFamily }
            MouseArea { id: cm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: screen.closeRequested() }
        }
    }
}
