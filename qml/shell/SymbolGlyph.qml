import QtQuick

// Android's system fonts do not cover several desktop symbols. Keep the
// desktop artwork and draw these small monochrome silhouettes on Android.
Item {
    id: icon
    property string glyph: ""
    property color color: Theme.ink
    property real size: 16
    property string fontFamily: ""
    readonly property bool drawn: Qt.platform.os === "android" && ["⌕", "⧉", "▤", "≣", "◷", "⛁", "ᛒ", "◠", "☰", "↻", "⤒", "⤓", "⤢", "↶", "⌃", "⌄"].indexOf(glyph) >= 0
    implicitWidth: drawn ? size : label.implicitWidth
    implicitHeight: drawn ? size : label.implicitHeight
    Text {
        id: label
        visible: !icon.drawn
        anchors.centerIn: parent
        text: icon.glyph; color: icon.color; font.pixelSize: icon.size; font.family: icon.fontFamily
    }
    Canvas {
        id: drawing
        anchors.fill: parent
        visible: icon.drawn
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: icon
            function onGlyphChanged() { drawing.requestPaint() }
            function onColorChanged() { drawing.requestPaint() }
        }
        onPaint: {
            const c = getContext("2d")
            c.reset(); c.scale(width / 20, height / 20)
            c.strokeStyle = icon.color; c.lineWidth = 1.5; c.lineCap = "round"; c.lineJoin = "round"
            c.beginPath()
            switch (icon.glyph) {
            case "⌕": c.arc(8, 8, 5, 0, 2 * Math.PI); c.moveTo(12, 12); c.lineTo(17, 17); break
            case "⧉": c.rect(3, 3, 10, 10); c.moveTo(7, 15); c.lineTo(7, 17); c.lineTo(17, 17); c.lineTo(17, 7); c.lineTo(15, 7); break
            case "▤": case "≣":
                c.rect(3, 3, 14, 14)
                for (let y = 6; y <= 14; y += 4) { c.moveTo(6, y); c.lineTo(14, y) }
                break
            case "◷": c.arc(10, 10, 7, 0, 2 * Math.PI); c.moveTo(10, 5); c.lineTo(10, 10); c.lineTo(14, 10); break
            case "⛁":
                c.ellipse(3, 3, 14, 5); c.moveTo(3, 5.5); c.lineTo(3, 15)
                c.bezierCurveTo(3, 19, 17, 19, 17, 15); c.lineTo(17, 5.5)
                c.moveTo(3, 10); c.bezierCurveTo(3, 14, 17, 14, 17, 10); break
            case "ᛒ": c.moveTo(7, 2); c.lineTo(14, 7); c.lineTo(4, 14); c.moveTo(7, 18); c.lineTo(14, 13); c.lineTo(4, 6); c.moveTo(7, 2); c.lineTo(7, 18); break
            case "◠": c.arc(10, 16, 12, 1.25 * Math.PI, 1.75 * Math.PI); c.moveTo(5.8, 11.8); c.arc(10, 16, 6, 1.25 * Math.PI, 1.75 * Math.PI); c.moveTo(9, 16); c.lineTo(11, 16); break
            case "☰": for (let y = 4; y <= 16; y += 6) { c.moveTo(3, y); c.lineTo(17, y) }; break
            case "↻": c.arc(10, 10, 7, .2 * Math.PI, 1.85 * Math.PI); c.moveTo(17, 3); c.lineTo(17, 8); c.lineTo(12, 8); break
            case "⤒": case "⤓": {
                const up = icon.glyph === "⤒", end = up ? 4 : 16
                c.moveTo(10, up ? 16 : 4); c.lineTo(10, end)
                c.moveTo(5, up ? 9 : 11); c.lineTo(10, end); c.lineTo(15, up ? 9 : 11)
                c.moveTo(4, up ? 2 : 18); c.lineTo(16, up ? 2 : 18); break
            }
            case "⤢": c.moveTo(3, 8); c.lineTo(3, 3); c.lineTo(8, 3); c.moveTo(3, 3); c.lineTo(8, 8); c.moveTo(12, 17); c.lineTo(17, 17); c.lineTo(17, 12); c.moveTo(17, 17); c.lineTo(12, 12); break
            case "↶": c.moveTo(7, 3); c.lineTo(3, 7); c.lineTo(7, 11); c.moveTo(3, 7); c.lineTo(12, 7); c.bezierCurveTo(19, 7, 19, 17, 11, 17); break
            case "⌃": c.moveTo(4, 13); c.lineTo(10, 7); c.lineTo(16, 13); break
            case "⌄": c.moveTo(4, 7); c.lineTo(10, 13); c.lineTo(16, 7); break
            }
            c.stroke()
        }
    }
}
