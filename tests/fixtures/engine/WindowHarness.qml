// Test harness of tests/EngineLiveTest.cpp: the application's real EngineWindow (toolbar, address bar, confirmation panel) on a plain backdrop,
// with a stand-in for the text reader's search-engine choice and for the server connection state.
import QtQuick
import QtQuick.Window
import Fortiq.Engine

Window {
    id: w
    width: 1000; height: 700; visible: true
    property var engineObj: null
    property alias win: ew
    Rectangle {
        id: bd
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0b2a55" }
            GradientStop { position: 1.0; color: "#06152a" }
        }
    }
    QtObject {
        id: fakeLegacy
        property var searchEngines: [ { id: "bing", name: "Bing", note: "" }, { id: "duckduckgo", name: "DuckDuckGo", note: "" } ]
        property string searchEngine: "bing"
        function resolveTyped(t) { return t.indexOf("http") === 0 ? { url: t, error: "" } : { url: "http://127.0.0.1:1/search?q=" + encodeURIComponent(t), error: "" } }
    }
    QtObject { id: fakeRemote; property bool connected: true }
    EngineWindow { id: ew; anchors.fill: parent; backdrop: bd; engine: w.engineObj; legacy: fakeLegacy; remote: fakeRemote }
}
