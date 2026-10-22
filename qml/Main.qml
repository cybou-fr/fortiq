import QtQuick
import QtQuick.Window
import QtQuick.Controls
import Fortiq.Shell

// ApplicationWindow applies Android safe-area padding on Qt 6.9+.
ApplicationWindow {
    id: window
    visible: true
    width: Theme.canvasWidth
    height: Theme.canvasHeight
    minimumWidth: 320
    minimumHeight: 320
    title: "FORTIQ"
    color: "#06152A"
    onClosing: close => { if ((adaptive || Qt.platform.os === "android") && shell.handleBack()) close.accepted = false }

    readonly property real keyboardOverlap: Qt.platform.os === "android" && Qt.inputMethod.visible && Qt.inputMethod.keyboardRectangle.height > 0
        ? Math.max(0, contentItem.y + contentItem.height - Qt.inputMethod.keyboardRectangle.y) : 0
    // A wide tablet must not shrink the entire desktop to fit above its IME.
    // Keep controls in logical units while typing, then restore the desktop.
    readonly property bool adaptive: contentItem.width < 1280 || contentItem.width < contentItem.height
                                    || (Qt.platform.os === "android" && Qt.inputMethod.visible)
    readonly property real canvasScale: adaptive ? 1 : Math.min(contentItem.width / Theme.canvasWidth, contentItem.height / Theme.canvasHeight)

    DesktopShell {
        id: shell
        adaptive: window.adaptive
        width: adaptive ? window.contentItem.width : Theme.canvasWidth
        height: adaptive ? Math.max(200, window.contentItem.height - window.keyboardOverlap) : Theme.canvasHeight
        controller: remote
        logsController: logs
        fixture: shellFixture
        fixturePreview: shellFixturePreview
        wallpaperSource: shellWallpaper
        wallpaperName: shellWallpaperName
        transformOrigin: Item.TopLeft
        scale: window.canvasScale
        x: (window.contentItem.width - width * scale) / 2
        y: window.adaptive ? 0 : (window.contentItem.height - height * scale) / 2
    }
}
