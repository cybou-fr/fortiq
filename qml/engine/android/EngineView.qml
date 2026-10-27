import QtQuick

// One page of the Browser engine on Android: the system WebView (Chromium) laid over this item (src/browser/engine/android/AndroidWebView.cpp,
// android/src/fr/cybou/fortiq/FortiqWeb.java). Same surface as the desktop qml/engine/EngineView.qml, so EngineWindow.qml is shared. The
// whole network of the WebView is the loopback gateway of the selected server (ProxyController override, no bypass); without the
// confirmed override no WebView exists and `failure` says why.
AndroidWebView {
    id: view
    property var sharedProfile: null     // unused on Android (no profile object); kept so that EnginePages.qml is shared
    signal newTabRequested(string url)   // never emitted: the WebView does not open extra windows (setSupportMultipleWindows(false))
    // A native view cannot sit under QML: it is hidden while QML shows something over the page area (the start page, a failure text, the
    // question about an internal address, which is asked BEFORE any contact).
    suspended: view.failure.length > 0 || view.url.toString() === "about:blank" || view.url.toString().length === 0
               || (engine !== null && engine.pendingConfirm.host !== undefined)
}
