import QtQuick
import QtQuick.Window
import Fortiq.Shell

// Hosts the master 1536×1024 Desktop Shell scaled uniformly to the window.
// Tablet/narrow adaptations of UI_SPEC_V1 §4 are not implemented yet.
Window {
    id: window
    visible: true
    width: Theme.canvasWidth
    height: Theme.canvasHeight
    minimumWidth: 640
    minimumHeight: 427
    title: "FORTIQ"
    color: "#06152A"

    readonly property real canvasScale: Math.min(width / Theme.canvasWidth, height / Theme.canvasHeight)

    DesktopShell {
        id: shell
        controller: remote
        fixture: shellFixture
        fixturePreview: shellFixturePreview
        wallpaperSource: shellWallpaper
        wallpaperName: shellWallpaperName
        transformOrigin: Item.TopLeft
        scale: window.canvasScale
        x: (window.width - width * scale) / 2
        y: (window.height - height * scale) / 2
    }
}
