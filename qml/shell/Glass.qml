import QtQuick
import QtQuick.Effects

// Frosted glass: blurred slice of the wallpaper behind the item, tinted,
// with a 1px cool edge. `blurEnabled: false` is the high-contrast fallback.
Item {
    id: glass
    property Item backdrop
    property real radius: 18
    property color tint: "#99303650"
    property color edgeColor: "#33DCEBFF"
    property bool blurEnabled: true
    property real blurAmount: 0.75
    property bool shadow: true

    function syncOrigin() {
        if (!backdrop) return
        const p = glass.mapToItem(backdrop, 0, 0)
        source.sourceRect = Qt.rect(p.x, p.y, glass.width, glass.height)
    }
    onXChanged: syncOrigin()
    onYChanged: syncOrigin()
    onWidthChanged: syncOrigin()
    onHeightChanged: syncOrigin()
    Component.onCompleted: syncOrigin()
    // Ancestor movement (for example dragging a window) does not change this
    // item's local x/y. Sample the actual position before each rendered frame.
    FrameAnimation {
        running: glass.visible && glass.blurEnabled && glass.backdrop !== null
        onTriggered: glass.syncOrigin()
    }

    RectangularShadow {
        visible: glass.shadow
        anchors.fill: parent
        offset.y: 10
        radius: glass.radius
        blur: 36
        spread: 0
        color: "#55000814"
    }

    ShaderEffectSource {
        id: source
        anchors.fill: parent
        sourceItem: glass.blurEnabled ? glass.backdrop : null
        visible: false
        live: true
        recursive: false
    }

    Rectangle {
        id: mask
        anchors.fill: parent
        radius: glass.radius
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: parent
        // Blend the frosted wallpaper with the real scene below, so lower
        // windows remain visible rather than being replaced by an opaque crop.
        opacity: 0.55
        visible: glass.blurEnabled && glass.backdrop !== null
        source: source
        blurEnabled: true
        blur: glass.blurAmount
        blurMax: 48
        saturation: 0.1
        maskEnabled: true
        maskSource: mask
    }

    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: glass.blurEnabled ? glass.tint : Qt.rgba(glass.tint.r, glass.tint.g, glass.tint.b, 1)
        border.width: 1
        border.color: glass.edgeColor
    }
}
