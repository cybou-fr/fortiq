import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 1180
    height: 780
    minimumWidth: 340
    minimumHeight: 520
    title: "FORTIQ — Distributed Desktop"
    color: "#0B1424"

    readonly property bool compact: width < 800
    property int page: 0
    readonly property color ink: "#EAF1FB"
    readonly property color muted: "#9DAEC7"
    readonly property color accent: "#57D9D0"
    readonly property color panel: "#142339"
    readonly property color outline: "#2B415A"
    readonly property bool trustedPending: remote.pendingFingerprint.length > 0

    component NavItem: Button {
        property string itemLabel: ""
        property string glyph: "◉"
        property bool selected: false
        property bool available: true
        implicitHeight: 48
        padding: 8
        font.pixelSize: 14
        enabled: available
        background: Rectangle {
            radius: 12
            color: parent.selected ? "#23475B" : (parent.hovered ? "#1B304B" : "transparent")
            border.width: parent.selected ? 1 : 0
            border.color: "#367588"
        }
        contentItem: RowLayout {
            spacing: 12
            Text { text: glyph; font.pixelSize: 19; color: selected ? accent : muted; Layout.preferredWidth: 24; horizontalAlignment: Text.AlignHCenter }
            Text { text: itemLabel; font.pixelSize: 14; font.weight: selected ? Font.DemiBold : Font.Normal; color: available ? ink : muted; Layout.fillWidth: true }
        }
    }

    header: ToolBar {
        implicitHeight: 68
        background: Rectangle { color: "#101F32"; border.color: "#20354E"; border.width: 1 }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: window.compact ? 16 : 28
            anchors.rightMargin: window.compact ? 16 : 28
            spacing: 14
            Rectangle {
                width: 36; height: 36; radius: 11
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#60E5D0" }
                    GradientStop { position: 1.0; color: "#377EC5" }
                }
                Text { anchors.centerIn: parent; text: "F"; font.pixelSize: 23; font.weight: Font.Black; color: "#0E2135" }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: -1
                Text { text: "FORTIQ"; color: ink; font.pixelSize: 19; font.bold: true; font.letterSpacing: 1.4 }
                Text { text: window.compact ? "Remote Linux workspace" : "One Linux workspace. Any screen."; color: muted; font.pixelSize: 11 }
            }
            Rectangle {
                implicitWidth: statusLine.implicitWidth + 23
                height: 30
                radius: 15
                color: remote.connected ? "#16493F" : "#273447"
                RowLayout {
                    anchors.centerIn: parent; spacing: 7
                    Rectangle { width: 7; height: 7; radius: 4; color: remote.connected ? "#5FE0AD" : "#8796A9" }
                    Text { id: statusLine; color: ink; font.pixelSize: 11; text: remote.connected ? "SSH verified" : "Offline" }
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            visible: !window.compact
            Layout.preferredWidth: 218
            Layout.fillHeight: true
            color: "#101F32"
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 14; anchors.rightMargin: 14
                anchors.topMargin: 20; anchors.bottomMargin: 20
                spacing: 5
                Text { text: "WORKSPACE"; color: muted; font.pixelSize: 10; font.letterSpacing: 2; Layout.leftMargin: 12; Layout.bottomMargin: 12 }
                NavItem { Layout.fillWidth: true; itemLabel: "Overview"; glyph: "◈"; selected: window.page === 0; onClicked: window.page = 0 }
                NavItem { Layout.fillWidth: true; itemLabel: "Files"; glyph: "▣"; selected: window.page === 1; onClicked: window.page = 1 }
                NavItem { Layout.fillWidth: true; itemLabel: "Applications"; glyph: "▦"; selected: window.page === 2; onClicked: window.page = 2 }
                Item { Layout.fillHeight: true }
                Rectangle { color: outline; height: 1; Layout.fillWidth: true }
                Text { text: "CLIENT PLATFORM"; color: muted; font.pixelSize: 10; font.letterSpacing: 1.5; Layout.topMargin: 12; Layout.leftMargin: 12 }
                Text { text: Qt.platform.os === "android" ? "Android · Qt 6" : "Windows / Desktop · Qt 6"; color: ink; font.pixelSize: 12; Layout.leftMargin: 12; Layout.bottomMargin: 12 }
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: window.page

            ScrollView {
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: Math.max(0, parent.width - 30)
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 18
                    Item { Layout.preferredHeight: 8 }
                    Text { text: "Your Linux, everywhere."; color: ink; font.pixelSize: window.compact ? 26 : 34; font.weight: Font.Bold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Text { text: "A clean workspace over SSH. Your tools and files stay on your server."; color: muted; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: connectionForm.implicitHeight + 36
                        radius: 18; color: panel
                        border.width: 1; border.color: outline
                        ColumnLayout {
                            id: connectionForm
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 18
                            spacing: 12
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Linux connection"; color: ink; font.pixelSize: 19; font.bold: true; Layout.fillWidth: true }
                                Text { text: "SSH / SFTP"; color: accent; font.pixelSize: 11; font.bold: true }
                            }
                            Text { text: "Only bootstrap and a verified host key are stored locally. No working files are cached."; color: muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            GridLayout {
                                columns: window.compact ? 1 : 2
                                rowSpacing: 12; columnSpacing: 16
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Server address"; font.pixelSize: 12; color: muted }
                                    TextField { id: hostInput; text: remote.rememberedHost; placeholderText: "example.org or IP"; Layout.fillWidth: true; selectByMouse: true; enabled: !remote.busy }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text { text: "SSH port"; font.pixelSize: 12; color: muted }
                                    TextField { id: portInput; text: String(remote.rememberedPort); validator: IntValidator { bottom: 1; top: 65535 }; inputMethodHints: Qt.ImhDigitsOnly; Layout.fillWidth: true; enabled: !remote.busy }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Linux username"; font.pixelSize: 12; color: muted }
                                    TextField { id: userInput; text: remote.rememberedUser; placeholderText: "username"; Layout.fillWidth: true; enabled: !remote.busy }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text { text: "SSH password (memory only)"; font.pixelSize: 12; color: muted }
                                    TextField { id: passwordInput; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "Not saved"; enabled: !remote.busy }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Button {
                                    id: connectButton
                                    text: remote.connected ? "Refresh files" : "Connect securely"
                                    enabled: !remote.busy && (remote.connected || (hostInput.text.trim().length > 0 && userInput.text.trim().length > 0 && passwordInput.text.length > 0))
                                    highlighted: true
                                    onClicked: {
                                        if (remote.connected) remote.refresh()
                                        else remote.connectServer(hostInput.text, Number(portInput.text), userInput.text, passwordInput.text)
                                    }
                                }
                                Button { text: "Disconnect"; visible: remote.connected; enabled: !remote.busy; onClicked: { remote.disconnectServer(); passwordInput.text = "" } }
                                BusyIndicator { running: remote.busy; visible: running; implicitWidth: 28; implicitHeight: 28 }
                                Item { Layout.fillWidth: true }
                            }
                            Text { text: remote.statusText; color: remote.connected ? "#77E3BD" : muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Rectangle {
                                visible: window.trustedPending
                                Layout.fillWidth: true
                                implicitHeight: trustColumn.implicitHeight + 24
                                radius: 12; color: "#342B27"; border.color: "#8E7042"
                                ColumnLayout {
                                    id: trustColumn
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
                                    spacing: 8
                                    Text { text: "First connection: verify the host key"; color: "#F6D99B"; font.bold: true; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    Text { text: remote.pendingFingerprint; color: ink; font.pixelSize: 12; font.family: "monospace"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                                    Text { text: "Compare with the fingerprint from your server console or another trusted channel. Do not accept an unexpected key."; color: "#E8D5B9"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    Button { text: "I verified it — trust this host"; enabled: !remote.busy; onClicked: remote.trustHost() }
                                }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: summaryColumn.implicitHeight + 36
                        radius: 18; color: panel; border.color: outline; border.width: 1
                        ColumnLayout {
                            id: summaryColumn
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 18; spacing: 12
                            Text { text: "Workspace status"; font.pixelSize: 18; font.bold: true; color: ink }
                            Text { text: remote.connected ? "Connected: " + remote.serverLabel : "Connect your Linux server to unlock the workspace."; color: muted; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Text { text: remote.connected ? "SFTP folder: " + remote.currentDirectory : "Your desktop remains usable; server operations are unavailable offline."; color: ink; font.pixelSize: 13; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                            Button { text: "Open Files  →"; enabled: remote.connected; onClicked: window.page = 1 }
                        }
                    }
                    Item { Layout.preferredHeight: 18 }
                }
            }

            ColumnLayout {
                spacing: 12
                anchors.margins: window.compact ? 16 : 26
                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 18
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: "Files"; color: ink; font.pixelSize: 28; font.bold: true }
                        Text { text: "Linux filesystem · SFTP"; color: muted; font.pixelSize: 12 }
                    }
                    Button { text: "↑"; enabled: remote.connected && !remote.busy; onClicked: remote.goUp(); ToolTip.visible: hovered; ToolTip.text: "Parent folder" }
                    Button { text: "⟳"; enabled: remote.connected && !remote.busy; onClicked: remote.refresh(); ToolTip.visible: hovered; ToolTip.text: "Refresh" }
                }
                Rectangle {
                    Layout.fillWidth: true; height: 45; radius: 12; color: panel; border.color: outline
                    Text { anchors.fill: parent; anchors.margins: 12; verticalAlignment: Text.AlignVCenter; color: ink; font.pixelSize: 13; elide: Text.ElideMiddle; text: remote.connected ? remote.currentDirectory : "No active SSH connection" }
                }
                ListView {
                    id: filesList
                    Layout.fillWidth: true; Layout.fillHeight: true
                    spacing: 5; clip: true
                    model: remote.entries
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: ItemDelegate {
                        required property var modelData
                        width: filesList.width
                        height: 62
                        enabled: !remote.busy && remote.connected && modelData.directory === true
                        background: Rectangle { radius: 10; color: parent.hovered ? "#203750" : panel; border.color: outline; border.width: 1 }
                        contentItem: RowLayout {
                            spacing: 12
                            Text { text: modelData.directory ? "▣" : (modelData.link ? "↗" : "▤"); color: modelData.directory ? accent : muted; font.pixelSize: 21; Layout.preferredWidth: 28 }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                Text { text: modelData.name; color: ink; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 14 }
                                Text { text: modelData.directory ? "Folder" : (modelData.link ? "Symbolic link" : "Remote file"); color: muted; font.pixelSize: 11 }
                            }
                            Text { text: modelData.directory ? "›" : ""; color: muted; font.pixelSize: 20 }
                        }
                        onClicked: if (modelData.directory) remote.openDirectory(modelData.path)
                    }
                    footer: Item { width: filesList.width; height: 18 }
                }
                Text { text: remote.connected ? remote.entries.length + " items · folders open on tap/click · file viewing comes next" : "Connect on the Overview tab to browse files."; color: muted; font.pixelSize: 12; Layout.bottomMargin: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }

            ScrollView {
                clip: true
                ColumnLayout {
                    width: Math.max(0, parent.width - 32)
                    spacing: 16
                    Item { Layout.preferredHeight: 12 }
                    Text { text: "Applications"; color: ink; font.pixelSize: 29; font.bold: true }
                    Text { text: "A universal desktop will grow around verified Linux tools, not a DevOps-only dashboard."; color: muted; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Repeater {
                        model: [
                            { title: "Editor & Documents", detail: "Server-side edit, preview and safe save · next" },
                            { title: "Mail", detail: "Existing Linux mail CLI over SSH · planned" },
                            { title: "Browser", detail: "SSH-routed web engine · planned" },
                            { title: "Services & Tools", detail: "Linux applications through SSH · planned" }
                        ]
                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 94
                            radius: 14; color: panel; border.color: outline; border.width: 1
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 15
                                Text { text: modelData.title; font.pixelSize: 16; color: ink; font.bold: true }
                                Text { text: modelData.detail; font.pixelSize: 12; color: muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }
        }
    }

    footer: ToolBar {
        visible: window.compact
        implicitHeight: window.compact ? 62 : 0
        background: Rectangle { color: "#101F32"; border.color: outline }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
            Repeater {
                model: [ { title: "Overview", glyph: "◈" }, { title: "Files", glyph: "▣" }, { title: "Apps", glyph: "▦" } ]
                ToolButton {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true; Layout.fillHeight: true
                    contentItem: ColumnLayout {
                        spacing: 0
                        Text { text: modelData.glyph; color: window.page === index ? accent : muted; font.pixelSize: 21; horizontalAlignment: Text.AlignHCenter; Layout.alignment: Qt.AlignHCenter }
                        Text { text: modelData.title; color: window.page === index ? ink : muted; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
                    }
                    onClicked: window.page = index
                }
            }
        }
    }
    Connections {
        target: remote
        function onChanged() {
            if (remote.connected && passwordInput.text.length > 0) passwordInput.text = ""
        }
    }
}
