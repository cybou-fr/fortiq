import QtQuick
import QtQuick.Controls

// Small delayed hint next to its parent, in the shell's glass style. It is drawn in the window overlay, not inside the
// parent's window: a hint inside a window sits below the windows stacked above it and below the terminal/editor content
// that comes later in the same window, which is why hints used to be invisible. Below the parent, or above it when
// there is no room (a bottom panel).
Item {
    id: tip
    property string text: ""
    property bool shown: false
    readonly property Item target: parent
    visible: false   // the host has no size; the hint itself lives in the overlay

    function place() {
        const ov = Overlay.overlay
        if (!ov || !target) return
        const below = target.mapToItem(ov, target.width / 2, target.height + 6)
        const above = target.mapToItem(ov, target.width / 2, -6 - bubble.height)
        const wantsAbove = below.y + bubble.height > ov.height - 4
        bubble.x = Math.max(4, Math.min(below.x - bubble.width / 2, ov.width - bubble.width - 4))
        bubble.y = wantsAbove ? above.y : below.y
    }
    onShownChanged: { if (!shown) timer.elapsedEnough = false }

    Timer {
        id: timer
        property bool elapsedEnough: false
        interval: 450
        running: tip.shown
        onTriggered: { elapsedEnough = true; tip.place() }
    }

    Rectangle {
        id: bubble
        objectName: "toolTipBubble"
        parent: Overlay.overlay
        z: 100000
        visible: opacity > 0
        opacity: tip.shown && timer.elapsedEnough ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 120 } }
        width: label.implicitWidth + 18
        height: 24
        radius: 6
        color: "#F00B1424"
        border.width: 1
        border.color: "#55FFFFFF"
        Text {
            id: label
            anchors.centerIn: parent
            text: tip.text
            textFormat: Text.PlainText
            color: Theme.ink
            font.pixelSize: 12
            font.family: Theme.fontFamily
        }
    }
}
