import QtQuick

// Modal glass dialog inside a window: optional name field, Enter confirms,
// Escape cancels. `danger` paints the confirm button red.
Rectangle {
    id: dialog
    property string title: ""
    property string message: ""
    property string confirmText: "OK"
    property string altText: "" // optional middle button
    property string cancelText: "Annuler"
    property bool danger: false
    property bool withField: false
    property string fieldText: ""
    property string error: ""
    property bool busy: false
    signal accepted(string value)
    signal rejected()
    signal alternate()

    function openWith(t, m, confirm, isDanger, field, initial) {
        title = t; message = m; confirmText = confirm; danger = isDanger; altText = ""; cancelText = "Annuler"
        withField = field; fieldText = initial; error = ""
        visible = true
        if (field) {
            input.text = initial
            input.forceActiveFocus()
            // Select the base name, not the extension, like desktop file managers.
            const dot = initial.lastIndexOf(".")
            input.select(0, dot > 0 ? dot : initial.length)
        } else {
            forceActiveFocus()
        }
    }
    function confirm() {
        if (busy) return
        const value = withField ? input.text.trim() : ""
        if (withField && value.length === 0) { error = "Le nom ne peut pas être vide."; return }
        if (withField && (value.indexOf("/") >= 0 || value === "." || value === "..")) { error = "Le nom ne peut pas contenir « / » ni être « . » ou « .. »."; return }
        accepted(value)
    }

    visible: false
    color: "#99060C18"
    z: 50
    Keys.onEscapePressed: { if (!busy) { visible = false; rejected() } }
    Keys.onReturnPressed: confirm()
    Keys.onEnterPressed: confirm()
    MouseArea { anchors.fill: parent; onClicked: {} } // modal: swallow clicks

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 380)
        height: column.implicitHeight + 32
        radius: 14
        color: "#F5101B2E"
        border.width: 1; border.color: "#40FFFFFF"

        Column {
            id: column
            x: 18; y: 16; width: parent.width - 36
            spacing: 10
            Text { width: parent.width; text: dialog.title; color: Theme.ink; font.pixelSize: 15; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; font.family: Theme.fontFamily }
            Text { visible: text.length > 0; width: parent.width; text: dialog.message; color: Theme.inkMuted; font.pixelSize: 12; wrapMode: Text.WordWrap; font.family: Theme.fontFamily }
            Rectangle {
                visible: dialog.withField
                width: parent.width; height: 32; radius: 8
                color: "#1AFFFFFF"
                border.width: 1; border.color: input.activeFocus ? Theme.accent : "#33FFFFFF"
                TextInput {
                    id: input
                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.ink; selectionColor: Theme.accent
                    font.pixelSize: 13; font.family: Theme.fontFamily
                    clip: true
                    enabled: !dialog.busy
                    onTextEdited: dialog.error = ""
                    Keys.onEscapePressed: { if (!dialog.busy) { dialog.visible = false; dialog.rejected() } }
                    onAccepted: dialog.confirm()
                }
            }
            Text { visible: dialog.error.length > 0; width: parent.width; text: dialog.error; color: "#FCA5A5"; font.pixelSize: 11; wrapMode: Text.WordWrap; font.family: Theme.fontFamily }
            Row {
                anchors.right: parent.right
                spacing: 8
                Repeater {
                    model: dialog.altText.length > 0
                        ? [ { k: "cancel", t: dialog.cancelText }, { k: "alt", t: dialog.altText }, { k: "ok", t: dialog.confirmText } ]
                        : [ { k: "cancel", t: dialog.cancelText }, { k: "ok", t: dialog.confirmText } ]
                    Rectangle {
                        required property var modelData
                        height: 30; radius: 8
                        width: label.implicitWidth + 24
                        opacity: dialog.busy ? 0.5 : 1
                        color: modelData.k === "ok" ? (dialog.danger ? "#C2410C" : Theme.selection)
                                                   : (btn.containsMouse ? "#33FFFFFF" : "#1FFFFFFF")
                        Text { id: label; anchors.centerIn: parent; text: modelData.t; color: "white"; font.pixelSize: 12; font.family: Theme.fontFamily }
                        MouseArea {
                            id: btn
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            enabled: !dialog.busy
                            onClicked: modelData.k === "ok" ? dialog.confirm()
                                     : modelData.k === "alt" ? dialog.alternate()
                                     : (dialog.visible = false, dialog.rejected())
                        }
                    }
                }
            }
        }
    }
}
