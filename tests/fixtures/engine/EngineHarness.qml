// Test harness of tests/EngineLiveTest.cpp: the application's own EngineView in a window.
import QtQuick
import QtQuick.Window
import Fortiq.Engine

Window {
    id: w
    width: 900; height: 640; visible: true
    property var engineObj: null
    property alias view: v
    property string pageTitle: v.title
    property string pageUrl: v.url.toString()
    property bool pageLoading: v.loading
    property string pageFailure: v.failure
    property var lastJs: null
    property int jsCount: 0
    property var notices: []
    EngineView {
        id: v
        anchors.fill: parent
        engine: w.engineObj
        url: "about:blank"
        onRefused: text => { var n = w.notices.slice(); n.push(text); w.notices = n }
    }
    function open(u) { v.url = u }
    function js(code) { v.runJavaScript(code, function(r) { w.lastJs = r; w.jsCount++ }) }
    function profileInfo() {
        const p = v.profile
        return { otr: p.offTheRecord, cookies: p.persistentCookiesPolicy, cache: p.httpCacheType, storage: String(p.persistentStoragePath), cachePath: String(p.cachePath), spell: p.spellCheckEnabled }
    }
    function settingsInfo() {
        const s = v.settings
        return { js: s.javascriptEnabled, dns: s.dnsPrefetchEnabled, audit: s.hyperlinkAuditingEnabled, rtc: s.webRTCPublicInterfacesOnly, plugins: s.pluginsEnabled, clip: s.javascriptCanAccessClipboard, popups: s.javascriptCanOpenWindows, remoteFromLocal: s.localContentCanAccessRemoteUrls, fileFromLocal: s.localContentCanAccessFileUrls, insecure: s.allowRunningInsecureContent }
    }
}
