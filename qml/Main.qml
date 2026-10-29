import QtQuick
import QtQuick.Window
import QtQuick.Controls.Fusion
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
    // Android draws its own status icons. Give that safe inset the system
    // palette background rather than wallpaper; navigation stays edge-to-edge.
    background: Item {
        Wallpaper { anchors.fill: parent; source: shell.wallpaperUrl }
        Rectangle {
            objectName: "systemStatusBackground"
            visible: Qt.platform.os === "android"
            width: parent.width
            height: Math.max(window.topPadding, window.contentItem.y, window.SafeArea.margins.top)
            color: window.palette.window
        }
    }
    onClosing: close => { if(!shell.allowAppClose())close.accepted=false;else if ((adaptive || Qt.platform.os === "android") && shell.handleBack()) close.accepted = false }

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
        width: adaptive ? window.contentItem.width : window.contentItem.width / window.canvasScale
        height: adaptive ? Math.max(200, window.contentItem.height - window.keyboardOverlap) : window.contentItem.height / window.canvasScale
        controller: remote
        taskManager: typeof tasks !== "undefined" ? tasks : null
        logsController: logs
        terminalController: typeof terminal !== "undefined" ? terminal : null
        disksController: typeof disks !== "undefined" ? disks : null
        browserController: typeof browser !== "undefined" ? browser : null
        engineBrowser: typeof engineBrowser !== "undefined" ? engineBrowser : null
        networkController: typeof network !== "undefined" ? network : null
        systemController: typeof systemInfo !== "undefined" ? systemInfo : null
        dbController: typeof databases !== "undefined" ? databases : null
        mailController: typeof mail !== "undefined" ? mail : null
        documentsController: typeof documents !== "undefined" ? documents : null
        editorController: typeof codeEditor !== "undefined" ? codeEditor : null
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
