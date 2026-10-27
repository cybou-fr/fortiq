import QtQuick

// Android: the system WebView has no profile object (cookies and storage are wiped by FortiqWeb.java when the view is created and closed).
// Same surface as the desktop qml/engine/PageProfile.qml so that EnginePages.qml is shared.
Item {
    required property var engine
    readonly property var profile: null
}
