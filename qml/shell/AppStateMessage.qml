import QtQuick
import QtQuick.Controls.Fusion

// Shared plain-text presentation for application loading, empty and error states.
// The app owns the state decision; this component only applies consistent UX:
// a spinner while loading, and a "Reessayer" button under an error when the app gives it a retry action.
Text {
    id: message
    property string state: "empty" // "loading" | "empty" | "error"
    property bool touch: false
    property string retryText: ""   // set to show a retry button under an error
    signal retry()
    textFormat: Text.PlainText
    wrapMode: Text.WordWrap
    color: state === "error" ? "#FCA5A5" : Theme.inkMuted
    font.pixelSize: touch ? 14 : 13
    leftPadding: state === "loading" ? 30 : 0

    BusyIndicator {
        visible: message.state === "loading"
        running: visible
        x: 0; y: 0; width: 24; height: 24
    }
    StateButton {
        objectName: "stateRetry"
        visible: message.state === "error" && message.retryText.length > 0
        x: message.horizontalAlignment === Text.AlignHCenter ? (message.width - width) / 2 : 0
        y: message.contentHeight + 12
        touch: message.touch
        text: message.retryText
        onClicked: message.retry()
    }
}
