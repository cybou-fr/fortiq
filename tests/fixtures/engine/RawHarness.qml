// Test harness of tests/EngineLiveTest.cpp: a raw WebEngineView on chrome://gpu (the command line Chromium really runs with).
import QtQuick
import QtQuick.Window
import QtWebEngine

Window {
    id: w
    width: 700; height: 500; visible: true
    property var lastJs: null
    property int jsCount: 0
    property string pageTitle: v.title
    WebEngineView {
        id: v
        anchors.fill: parent
        profile: WebEngineProfile { offTheRecord: true }
        url: "chrome://gpu"
    }
    function js(code) { v.runJavaScript(code, function(r) { w.lastJs = r; w.jsCount++ }) }
}
