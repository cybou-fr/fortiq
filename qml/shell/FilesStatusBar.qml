import QtQuick

// Selection summary, notices and latency of the Files window.
Item {
    required property Item files
    id: statusBar
    x: files.contentLeft; y: parent.height - height
    width: parent.width - x; height: 26
    visible: files.fixture || files.connected
    Rectangle { width: parent.width; height: 1; color: "#14FFFFFF" }
    Text {
        x: 14; anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 250; elide: Text.ElideRight
        color: files.notice.length > 0 ? (files.noticeError ? "#FCA5A5" : "#86EFAC") : Theme.inkMuted
        font.pixelSize: 11; font.family: Theme.fontFamily
        text: {
            if (files.notice.length > 0) return files.notice
            const base = files.shown.length + " élément" + (files.shown.length > 1 ? "s" : "")
                + " · " + files.folderCount + " dossier" + (files.folderCount > 1 ? "s" : "")
            if (!files.selected) return base
            const s = files.selected
            return base + "   ·   " + s.name + (s.directory ? "" : " — " + files.formatSize(s.size))
                + (s.owner ? " — " + s.owner : "") + (s.mode ? " — " + s.mode : "")
        }
    }
    CopyChip {
        objectName: "filesTerminalHere"
        visible: !files.fixture && files.connected && files.controller !== null && files.controller.currentDirectory.length > 0
        anchors.right: parent.right; anchors.rightMargin: 130; anchors.verticalCenter: parent.verticalCenter
        label: "Terminal ici"
        onClicked: files.requestTerminal(files.controller.currentDirectory)
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 22
        anchors.verticalCenter: parent.verticalCenter
        color: "#7F93B5"
        font.pixelSize: 10; font.family: Theme.fontFamily
        text: files.fixture || !files.connected ? "SFTP" : (files.busy ? "chargement…" : "SFTP · " + files.controller.lastLatencyMs + " ms")
    }
}
