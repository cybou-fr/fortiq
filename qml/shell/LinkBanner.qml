import QtQuick

// Shown at the bottom of a window that displays data read from the server, once the link to that server was lost:
// what is on screen is the last known state, not the server's. "Réessayer" asks the window to refresh (a new
// connection is opened by the refresh itself); the banner disappears as soon as any app talks to the server again.
Rectangle {
    id: banner
    objectName: "linkBanner"
    property var remote: null
    property bool touch: false
    property real inset: 10        // distance from the bottom edge (Files keeps its status bar visible)
    signal retry()
    readonly property bool shown: remote !== null && remote.connected === true && remote.linkLost === true

    visible: shown
    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
    anchors.leftMargin: 10; anchors.rightMargin: 10; anchors.bottomMargin: inset
    height: touch ? 52 : 38
    radius: 10
    z: 50
    color: "#F0301F0C"
    border.width: 1; border.color: "#99F59E0B"

    Rectangle { x: 12; anchors.verticalCenter: parent.verticalCenter; width: 8; height: 8; radius: 4; color: "#F87171" }
    Text {
        objectName: "linkBannerText"
        x: 30; anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 30 - retryButton.width - 24
        text: (banner.remote && banner.remote.linkText) || ""
        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
        color: "#FDE68A"; font.pixelSize: banner.touch ? 13 : 11; font.family: Theme.fontFamily
    }
    Rectangle {
        id: retryButton
        objectName: "linkBannerRetry"
        anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
        width: retryLabel.implicitWidth + 24; height: banner.touch ? 38 : 26; radius: 13
        color: retryMouse.containsMouse ? "#66F59E0B" : "#33F59E0B"
        Text { id: retryLabel; anchors.centerIn: parent; text: "Réessayer"; color: "white"; font.pixelSize: banner.touch ? 13 : 11; font.family: Theme.fontFamily }
        MouseArea { id: retryMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: banner.retry() }
    }
}
