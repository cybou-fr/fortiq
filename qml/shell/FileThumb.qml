import QtQuick

// File tiles from the master grid: landscape image preview, red PDF,
// green sheet, pale text page. Drawn, not third-party logos.
Item {
    id: thumb
    property string kind: "text"
    width: kind === "image" ? 91 : 50
    height: kind === "image" ? 58 : 62

    Rectangle {
        visible: thumb.kind === "image"
        anchors.fill: parent
        radius: 2
        gradient: Gradient {
            GradientStop { position: 0; color: "#5C7FC0" }
            GradientStop { position: 0.45; color: "#E6B6A6" }
            GradientStop { position: 0.6; color: "#3B4566" }
            GradientStop { position: 1; color: "#1A2236" }
        }
    }

    Rectangle {
        visible: thumb.kind !== "image"
        anchors.fill: parent
        radius: 4
        color: thumb.kind === "pdf" ? "#D63A3A" : (thumb.kind === "sheet" ? "#F2F5F2" : "#EEF2F8")
        border.width: thumb.kind === "pdf" ? 0 : 1
        border.color: "#D3DAE6"
        Column {
            visible: thumb.kind === "text" || thumb.kind === "sheet"
            x: thumb.kind === "sheet" ? 24 : 9; y: 16; spacing: 5
            Repeater {
                model: 6
                Rectangle { width: thumb.kind === "sheet" ? 18 : 32; height: 2; color: thumb.kind === "sheet" ? "#3DAE6A" : "#9DB0CC" }
            }
        }
        Rectangle {
            visible: thumb.kind === "sheet"
            x: -6; y: 18; width: 26; height: 26; radius: 3; color: "#1E8E4E"
            Text { anchors.centerIn: parent; text: "X"; color: "white"; font.bold: true; font.pixelSize: 15 }
        }
        Text {
            visible: thumb.kind === "pdf"
            anchors.centerIn: parent
            text: "⅄"
            color: "white"; font.pixelSize: 30
        }
    }
}
