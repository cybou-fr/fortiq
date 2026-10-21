import QtQuick
import QtQuick.Controls

// Read-only viewer over the Files content area. Text (first 1 MiB) with
// line numbers, images with fit / 100 % / wheel zoom, hex dump for binaries.
// Content stays in RAM; nothing is saved on this device.
Rectangle {
    id: viewer
    property var controller: null
    property string name: controller ? controller.previewPath.split("/").pop() : ""
    readonly property string kind: controller ? controller.previewKind : ""
    property real zoom: 0 // 0 = fit
    signal closed()

    color: "#FA0D1626"
    focus: visible
    onVisibleChanged: { zoom = 0; if (visible) forceActiveFocus() }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Space) { viewer.closed(); event.accepted = true }
        else if (viewer.kind === "image" && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)) { viewer.zoomBy(1.25); event.accepted = true }
        else if (viewer.kind === "image" && event.key === Qt.Key_Minus) { viewer.zoomBy(0.8); event.accepted = true }
        else if (viewer.kind === "image" && event.key === Qt.Key_0) { viewer.zoom = 0; event.accepted = true }
    }

    function fitScale() {
        if (image.implicitWidth <= 0) return 1
        return Math.min(1, imageFlick.width / image.implicitWidth, imageFlick.height / image.implicitHeight)
    }
    function zoomBy(f) {
        const base = zoom === 0 ? fitScale() : zoom
        zoom = Math.max(0.05, Math.min(16, base * f))
    }

    FontMetrics { id: metrics; font.family: "Consolas"; font.pixelSize: 12 }

    // Header
    Item {
        id: header
        width: parent.width; height: 40
        FileIcon {
            x: 12; anchors.verticalCenter: parent.verticalCenter
            size: 22; name: viewer.name
        }
        Column {
            x: 42; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 42 - tools.width - 16
            Text {
                width: parent.width; elide: Text.ElideMiddle
                text: viewer.name
                color: Theme.ink; font.pixelSize: 13; font.weight: Font.DemiBold; font.family: Theme.fontFamily
            }
            Text {
                width: parent.width; elide: Text.ElideRight
                text: viewer.controller ? viewer.controller.previewInfo + "  ·  lecture seule" : ""
                color: Theme.inkMuted; font.pixelSize: 10; font.family: Theme.fontFamily
            }
        }
        Row {
            id: tools
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            ToolButtonGlass { visible: viewer.kind === "image"; glyph: "−"; tip: "Réduire (−)"; onClicked: viewer.zoomBy(0.8) }
            Text {
                visible: viewer.kind === "image"
                anchors.verticalCenter: parent.verticalCenter
                width: 46; horizontalAlignment: Text.AlignHCenter
                text: Math.round((viewer.zoom === 0 ? viewer.fitScale() : viewer.zoom) * 100) + " %"
                color: Theme.inkMuted; font.pixelSize: 11; font.family: Theme.fontFamily
            }
            ToolButtonGlass { visible: viewer.kind === "image"; glyph: "+"; tip: "Agrandir (+)"; onClicked: viewer.zoomBy(1.25) }
            ToolButtonGlass { visible: viewer.kind === "image"; glyph: "⤢"; tip: "Ajuster (0)"; checked: viewer.zoom === 0; onClicked: viewer.zoom = 0 }
            ToolButtonGlass { visible: viewer.kind === "image"; glyph: "1:1"; tip: "Taille réelle"; onClicked: viewer.zoom = 1 }
            ToolButtonGlass { glyph: "×"; tip: "Fermer (Échap)"; onClicked: viewer.closed() }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1FFFFFFF" }
    }

    Item {
        id: body
        y: header.height
        width: parent.width; height: parent.height - y
        clip: true

        BusyIndicator {
            anchors.centerIn: parent
            running: viewer.controller && viewer.controller.previewBusy
            visible: running
        }

        Column {
            visible: viewer.kind === "error"
            anchors.centerIn: parent
            spacing: 8
            width: Math.min(parent.width - 40, 420)
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Aperçu impossible"; color: Theme.ink; font.pixelSize: 15; font.family: Theme.fontFamily }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                text: viewer.controller ? viewer.controller.previewInfo : ""
                color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily
            }
        }

        // Text and hex dump: line numbers + read-only selectable text.
        Flickable {
            id: textFlick
            visible: viewer.kind === "text" || viewer.kind === "binary"
            anchors.fill: parent
            contentWidth: gutter.width + textEdit.contentWidth + 24
            contentHeight: textEdit.contentHeight + 16
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            Rectangle {
                id: gutter
                visible: viewer.kind === "text"
                width: visible ? lineNumbers.implicitWidth + 20 : 0
                height: Math.max(textFlick.height, textFlick.contentHeight)
                color: "#26000000"
                Text {
                    id: lineNumbers
                    x: 10; y: 8
                    text: {
                        if (viewer.kind !== "text" || !viewer.controller) return ""
                        const t = viewer.controller.previewText
                        const n = Math.min(t.split("\n").length - (t.endsWith("\n") ? 1 : 0), 50000)
                        let s = ""
                        for (let i = 1; i <= n; ++i) s += i + "\n"
                        return s
                    }
                    color: "#5F7395"
                    font.family: "Consolas"; font.pixelSize: 12
                    horizontalAlignment: Text.AlignRight
                    lineHeight: metrics.lineSpacing; lineHeightMode: Text.FixedHeight
                }
            }
            TextEdit {
                id: textEdit
                x: gutter.width + 12; y: 8
                readOnly: true
                selectByMouse: true
                text: viewer.controller && (viewer.kind === "text" || viewer.kind === "binary") ? viewer.controller.previewText : ""
                color: "#E4ECF8"; selectionColor: Theme.accent
                font.family: "Consolas"; font.pixelSize: 12
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.NoWrap
            }
        }

        // Image with checkerboard for transparency.
        Flickable {
            id: imageFlick
            visible: viewer.kind === "image"
            anchors.fill: parent; anchors.margins: 12
            contentWidth: Math.max(width, image.width)
            contentHeight: Math.max(height, image.height)
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            Item {
                width: imageFlick.contentWidth; height: imageFlick.contentHeight
                Grid {
                    anchors.fill: image
                    columns: Math.ceil(image.width / 12) + 1
                    clip: true
                    Repeater {
                        model: image.status === Image.Ready ? Math.min(4000, (Math.ceil(image.width / 12) + 1) * (Math.ceil(image.height / 12) + 1)) : 0
                        Rectangle {
                            required property int index
                            readonly property int cols: Math.ceil(image.width / 12) + 1
                            width: 12; height: 12
                            color: ((index % cols) + Math.floor(index / cols)) % 2 ? "#2A3346" : "#1F2738"
                        }
                    }
                }
                Image {
                    id: image
                    anchors.centerIn: parent
                    source: viewer.kind === "image" && viewer.controller ? viewer.controller.previewImage : ""
                    asynchronous: true
                    cache: false
                    smooth: true
                    mipmap: true
                    readonly property real s: viewer.zoom === 0 ? viewer.fitScale() : viewer.zoom
                    width: implicitWidth * s
                    height: implicitHeight * s
                }
            }
            WheelHandler {
                acceptedModifiers: Qt.NoModifier
                onWheel: (e) => viewer.zoomBy(e.angleDelta.y > 0 ? 1.15 : 1 / 1.15)
            }
        }
        Text {
            visible: viewer.kind === "image" && image.status === Image.Ready
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 10
            text: image.implicitWidth + " × " + image.implicitHeight + " px"
            color: Theme.inkMuted; font.pixelSize: 10; font.family: Theme.fontFamily
        }
        Text {
            visible: viewer.kind === "image" && image.status === Image.Error
            anchors.centerIn: parent
            text: "Format d'image non pris en charge"
            color: Theme.inkMuted; font.pixelSize: 12; font.family: Theme.fontFamily
        }
    }
}
