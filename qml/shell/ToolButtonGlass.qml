import QtQuick

// Compact glass toolbar button. `glyph` is text; "grid", "list", "eye" and
// "sort" are drawn so they do not depend on font coverage.
Rectangle {
    id: button
    property string glyph: ""
    property string tip: ""
    property bool checked: false
    property bool enabledState: true
    property bool touch: false
    signal clicked()
    width: touch ? 44 : 30; height: touch ? 44 : 28; radius: 8
    color: checked ? "#592F8CFF" : (mouse.containsMouse && enabledState ? "#26FFFFFF" : "transparent")
    border.width: checked ? 1 : 0
    border.color: "#802F8CFF"
    opacity: enabledState ? 1 : 0.35

    Text {
        visible: ["grid", "list", "eye", "sort"].indexOf(button.glyph) < 0
        anchors.centerIn: parent
        text: button.glyph
        color: Theme.ink
        font.pixelSize: 17
        font.family: Theme.fontFamily
    }
    Grid {
        visible: button.glyph === "grid"
        anchors.centerIn: parent
        columns: 2; spacing: 2
        Repeater { model: 4; Rectangle { width: 5; height: 5; radius: 1; color: Theme.ink } }
    }
    Column {
        visible: button.glyph === "list" || button.glyph === "sort"
        anchors.centerIn: parent
        spacing: 3
        Repeater {
            model: 3
            Rectangle { width: button.glyph === "sort" ? 13 - index * 4 : 13; height: 2; radius: 1; color: Theme.ink }
        }
    }
    Item {
        visible: button.glyph === "eye"
        anchors.centerIn: parent
        width: 16; height: 10
        Rectangle { anchors.fill: parent; radius: 5; color: "transparent"; border.width: 1.5; border.color: Theme.ink }
        Rectangle { anchors.centerIn: parent; width: 5; height: 5; radius: 2.5; color: Theme.ink }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: button.enabledState
        cursorShape: Qt.PointingHandCursor
        onClicked: button.clicked()
    }
    ToolTipLite { text: button.tip; shown: mouse.containsMouse && button.tip.length > 0 }
}
