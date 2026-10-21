pragma Singleton
import QtQuick

// Tokens calibrated against docs/design/reference/fortiq-shell-master.png
// (composited samples from UI_SPEC_V1 §2). Values are tuned by
// tool/compare_shell_screenshot.py, not invented.
QtObject {
    readonly property int canvasWidth: 1536
    readonly property int canvasHeight: 1024

    readonly property color topBar: "#C9001C42"
    readonly property color rail: "#E806162B"
    readonly property color filesBody: "#FEFEFE"
    readonly property color filesToolbar: "#F2F4F9"
    readonly property color filesSidebar: "#C21E2440"
    readonly property color launcher: "#D9152638"
    readonly property color browserChrome: "#F2132B52"
    readonly property color control: "#B0162A4A"
    readonly property color tileLight: "#E6EFF9FE"
    readonly property color tileDark: "#59424A66"
    readonly property color dock: "#E2121A28"

    readonly property color accent: "#2F8CFF"
    readonly property color accentDeep: "#1F6FE5"
    readonly property color selection: "#2D6FE0"
    readonly property color ink: "#F5F9FF"
    readonly property color inkMuted: "#B9C6DA"
    readonly property color paperInk: "#1C2333"
    readonly property color edge: "#33DCEBFF"
    readonly property color folder: "#3D8BF2"
    readonly property color folderTab: "#2E73DA"

    readonly property string fontFamily: Qt.platform.os === "windows" ? "Segoe UI" : "sans-serif"
}
