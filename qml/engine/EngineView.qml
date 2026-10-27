import QtQuick
import QtWebEngine

// One page of the Browser engine (Qt WebEngine = Chromium). Everything that makes the page safe lives HERE and in
// src/browser/engine/EngineProfile.cpp: an off-the-record profile (nothing on disk), no downloads, no permission, only http(s)
// navigations, the proxy password given to our own gateway only. One per live tab (EnginePages.qml); also used by the leak test.
WebEngineView {
    id: view
    // The EngineBrowser (C++): credentials, address policy and messages. Required: a view without it would never authenticate.
    required property var engine
    property string failure: ""            // last load error, "" when none
    property bool certificateProblem: false
    signal refused(string text)
    signal newTabRequested(string url)

    // Off-the-record: the profile is the window's shared one (qml/engine/PageProfile.qml: nothing on disk). Required, never defaulted: a view
    // without it would fall back to the default (persistent) profile of Qt WebEngine.
    required property var sharedProfile
    profile: sharedProfile

    // WebRTC needs UDP, which an HTTP proxy cannot carry: the whole API is removed from every frame before any page script runs, so no
    // socket is even bound (with only the proxy-only policy, creating an RTCPeerConnection still bound two UDP sockets: seen by the
    // leak test). The same goes for WebTransport (QUIC).
    userScripts.collection: [
        {
            name: "fortiq-no-webrtc",
            injectionPoint: WebEngineScript.DocumentCreation,
            worldId: WebEngineScript.MainWorld,
            runsOnSubFrames: true,
            sourceCode: "(function(){var n=['RTCPeerConnection','webkitRTCPeerConnection','RTCDataChannel','RTCSessionDescription','RTCIceCandidate','RTCPeerConnectionIceEvent','RTCRtpSender','RTCRtpReceiver','RTCRtpTransceiver','RTCDtlsTransport','RTCIceTransport','RTCSctpTransport','RTCCertificate','RTCDTMFSender','RTCStatsReport','RTCError','RTCErrorEvent','RTCTrackEvent','RTCDataChannelEvent','WebTransport','WebTransportBidirectionalStream','WebTransportDatagramDuplexStream','WebTransportError'];for(var i=0;i<n.length;i++){try{delete window[n[i]];Object.defineProperty(window,n[i],{value:undefined,configurable:false,writable:false});}catch(e){}}})();"
        }
    ]

    settings.javascriptEnabled: true
    settings.javascriptCanOpenWindows: false
    settings.javascriptCanAccessClipboard: false
    settings.javascriptCanPaste: false
    settings.localContentCanAccessRemoteUrls: false
    settings.localContentCanAccessFileUrls: false
    settings.allowRunningInsecureContent: false
    settings.allowGeolocationOnInsecureOrigins: false
    settings.dnsPrefetchEnabled: false
    settings.hyperlinkAuditingEnabled: false
    settings.webRTCPublicInterfacesOnly: true
    settings.pluginsEnabled: false
    settings.screenCaptureEnabled: false
    settings.fullScreenSupportEnabled: false
    settings.pdfViewerEnabled: false
    settings.autoLoadIconsForPage: false
    settings.touchIconsEnabled: false
    settings.playbackRequiresUserGesture: true
    settings.focusOnNavigationEnabled: false

    onNavigationRequested: request => {
        const v = view.engine.judge(request.url.toString(), request.isMainFrame)
        if (!v.allowed) { request.reject(); if (request.isMainFrame) view.refused(v.reason) }
    }
    // A link or window.open() that wants a NEW window becomes a tab (never an external window): only when the user caused it (a script alone
    // cannot spawn tabs), only for an allowed address; the window (EnginePages.qml) opens the tab next to this one and enforces the limit.
    onNewWindowRequested: request => {
        const v = view.engine.judge(request.requestedUrl.toString(), true)
        if (!request.userInitiated) { view.refused("Fenêtre non demandée par l'utilisateur : bloquée."); return }
        if (v.allowed) view.newTabRequested(request.requestedUrl.toString())
        else view.refused(v.reason)
    }
    onPermissionRequested: permission => {
        permission.deny()
        view.refused(view.engine.permissionRefusal())
    }
    // The only proxy is our gateway; its password goes nowhere else (no origin-server login, no foreign proxy).
    onAuthenticationDialogRequested: request => {
        request.accepted = true
        if (request.type === AuthenticationDialogRequest.AuthenticationTypeProxy) {
            const hostPort = String(request.proxyHost)
            const cut = hostPort.lastIndexOf(":")
            const host = cut > 0 ? hostPort.substring(0, cut) : hostPort
            const port = cut > 0 ? parseInt(hostPort.substring(cut + 1)) : view.engine.proxyPort
            const c = view.engine.proxyCredentials(host, port)
            if (c.user !== undefined) { request.dialogAccept(c.user, c.password); return }
        }
        request.dialogReject()
    }
    onCertificateError: error => {
        error.rejectCertificate()
        view.certificateProblem = true
        view.refused("Certificat refusé : " + error.description)
    }
    onLoadingChanged: loadRequest => {
        if (loadRequest.status === WebEngineLoadingInfo.LoadStartedStatus) { view.failure = ""; view.certificateProblem = false }
        else if (loadRequest.status === WebEngineLoadingInfo.LoadFailedStatus)
            view.failure = loadRequest.errorString + " (" + loadRequest.errorCode + ")"
    }
    onFullScreenRequested: request => request.reject()
    onJavaScriptDialogRequested: request => { request.accepted = true; request.dialogReject() }
    onFileDialogRequested: request => { request.accepted = true; request.dialogReject() }
    onColorDialogRequested: request => { request.accepted = true; request.dialogReject() }
    onRegisterProtocolHandlerRequested: request => request.reject()
}
