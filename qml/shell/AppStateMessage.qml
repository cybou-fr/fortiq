import QtQuick

// Shared plain-text presentation for application loading, empty and error states.
// The app owns the state decision; this component only applies consistent UX.
Text {
    id: message
    property string state: "empty" // "loading" | "empty" | "error"
    property bool touch: false
    textFormat: Text.PlainText
    wrapMode: Text.WordWrap
    color: state === "error" ? "#FCA5A5" : Theme.inkMuted
    font.pixelSize: touch ? 14 : 13
}
