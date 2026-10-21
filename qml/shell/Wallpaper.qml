import QtQuick
import QtQuick.Shapes

// Wallpaper surface. The owner's original lake/sunset wallpaper is not in the
// repository (only the composited master screenshot is), so `source` is empty
// by default and a procedural stand-in with the same composition is drawn:
// blue sky, lit peaks centre-left, dark forest line, reflective lake.
// It is a placeholder, never the approved artwork.
Item {
    id: root
    property url source: ""
    readonly property bool usingPlaceholder: image.status !== Image.Ready

    Image {
        id: image
        anchors.fill: parent
        source: root.source
        fillMode: Image.PreserveAspectCrop
        visible: status === Image.Ready
        asynchronous: false
    }

    Item {
        anchors.fill: parent
        visible: root.usingPlaceholder

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.00; color: "#0A3F86" }
                GradientStop { position: 0.12; color: "#1B5AA7" }
                GradientStop { position: 0.22; color: "#4A7EC9" }
                GradientStop { position: 0.30; color: "#9597C8" }
                GradientStop { position: 0.36; color: "#E9B8A6" }
                GradientStop { position: 0.44; color: "#F2A27E" }
                GradientStop { position: 0.52; color: "#7A6F8E" }
                GradientStop { position: 0.60; color: "#3E4F7A" }
                GradientStop { position: 0.72; color: "#A07B88" }
                GradientStop { position: 0.82; color: "#C9918E" }
                GradientStop { position: 0.90; color: "#475C8B" }
                GradientStop { position: 1.00; color: "#1A3A6E" }
            }
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            // Far range, lit by the sunset.
            ShapePath {
                strokeWidth: 0
                fillGradient: LinearGradient {
                    x1: 0; y1: 170; x2: 0; y2: 560
                    GradientStop { position: 0.0; color: "#F4C2A6" }
                    GradientStop { position: 0.45; color: "#9C6E86" }
                    GradientStop { position: 1.0; color: "#2B3355" }
                }
                startX: 0; startY: 520
                PathLine { x: 120; y: 400 }
                PathLine { x: 210; y: 330 }
                PathLine { x: 300; y: 230 }
                PathLine { x: 345; y: 190 }
                PathLine { x: 395; y: 260 }
                PathLine { x: 470; y: 320 }
                PathLine { x: 620; y: 420 }
                PathLine { x: 800; y: 300 }
                PathLine { x: 880; y: 260 }
                PathLine { x: 960; y: 330 }
                PathLine { x: 1080; y: 420 }
                PathLine { x: 1200; y: 330 }
                PathLine { x: 1330; y: 250 }
                PathLine { x: 1440; y: 330 }
                PathLine { x: 1536; y: 300 }
                PathLine { x: 1536; y: 620 }
                PathLine { x: 0; y: 620 }
            }
            // Forest line.
            ShapePath {
                strokeWidth: 0
                fillColor: "#0D1424"
                startX: 0; startY: 560
                PathLine { x: 90; y: 520 }
                PathLine { x: 250; y: 560 }
                PathLine { x: 420; y: 540 }
                PathLine { x: 640; y: 600 }
                PathLine { x: 900; y: 585 }
                PathLine { x: 1150; y: 600 }
                PathLine { x: 1360; y: 560 }
                PathLine { x: 1536; y: 580 }
                PathLine { x: 1536; y: 680 }
                PathLine { x: 0; y: 680 }
            }
        }

        // Lake with sunset reflection.
        Rectangle {
            y: 670
            width: parent.width
            height: parent.height - y
            gradient: Gradient {
                GradientStop { position: 0.00; color: "#2C2636" }
                GradientStop { position: 0.25; color: "#A07B81" }
                GradientStop { position: 0.50; color: "#D99A8C" }
                GradientStop { position: 0.75; color: "#5C6A96" }
                GradientStop { position: 1.00; color: "#1A447E" }
            }
        }
    }
}
