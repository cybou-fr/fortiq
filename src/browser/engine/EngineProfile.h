#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

namespace fortiq::engine {

// Configuration of the Browser engine (Qt WebEngine = Chromium; docs/BROWSER.md, "Moteur"). This file is PURE (Qt Core only): the same
// rules are applied by the application and checked by the offline tests, so a Qt update that renames a switch cannot silently
// reopen a way around the SSH gateway. The goal: the engine has exactly ONE way out, the loopback gateway of the selected server.

// Chromium switches, one per entry ("--name=value"), for a gateway listening on 127.0.0.1:`proxyPort`.
// Applied BEFORE the first profile is created, through QTWEBENGINE_CHROMIUM_FLAGS (applyChromiumFlags).
QStringList chromiumSwitches(quint16 proxyPort);

// The names passed to --disable-features (also reachable through chromiumSwitches()).
QStringList disabledFeatures();

// Puts the switches in the environment (QTWEBENGINE_CHROMIUM_FLAGS). Returns false when the variable already holds something the
// user or a script set: foreign switches could reopen a direct route, so the engine is then refused instead of started.
bool applyChromiumFlags(quint16 proxyPort, QString *problem = nullptr);

// Reads a switch list back (the command line of a running QtWebEngineProcess, or the environment) and reports every mandatory
// rule that is missing or contradicted. Empty list = the configuration is complete.
QStringList auditSwitches(const QStringList &switches, quint16 proxyPort);

// Navigation policy: only http(s) pages are shown, plus about:blank. Everything else (file:, ftp:, chrome:, devtools:, view-source:,
// javascript: addresses, qrc:, data:/blob: typed by the user) is refused with a French reason.
struct NavigationVerdict {
    bool allowed = false;
    QString reason;   // user-facing when refused
};
// `topLevel` = the page itself is navigated; sub-resources are judged with the same list plus data: and blob: that pages build.
NavigationVerdict judgeUrl(const QUrl &url, bool topLevel);

// The proxy credentials may only ever be given to OUR gateway.
bool isOurProxy(const QString &host, int port, quint16 gatewayPort);

// Permissions a page can ask for (geolocation, camera, notifications, clipboard...) are all refused.
QString permissionRefusal();


// ── Android (system WebView = Chromium, docs/BROWSER.md "Android") ────────────────────────────────────────────────────────────────
// The WebView has no command line: its ONE way out is androidx.webkit.ProxyController.setProxyOverride (process-wide). The plan below is
// what the Java side applies; the pure rules live here so that the offline tests check them without a device.
struct AndroidProxyPlan {
    QString rule;               // "http://127.0.0.1:<port>": the only proxy rule
    QStringList bypass;         // {"<-loopback>"}: removes Chromium's implicit "never proxy localhost" (a page asking 127.0.0.1 reaches the SERVER)
    bool reverseBypass = false; // must stay false: true would proxy ONLY the bypass list
    bool allowDirect = false;   // must stay false: no addDirect() / no "direct://" rule
};
AndroidProxyPlan androidProxyPlan(quint16 gatewayPort);
// Every mandatory rule that is missing or contradicted (empty = complete), like auditSwitches().
QStringList auditAndroidProxyPlan(const AndroidProxyPlan &plan, quint16 gatewayPort);
// Why the system WebView cannot be used (French, user-facing); empty when everything needed is supported. Without ANY of the three the
// browser refuses to start: it never falls back to a direct route.
QString androidSupportRefusal(bool webViewInstalled, bool proxyOverride, bool documentStartScript);
// The proxy password goes to a 401/407 challenge ONLY when it comes from our loopback address AND repeats the secret realm of this
// gateway session (an origin server that answers 401 for host 127.0.0.1 cannot know the realm).
bool acceptProxyChallenge(const QString &host, const QString &realm, const QString &expectedRealm);
// A fresh unguessable realm for one gateway session.
QString newGatewayRealm();
// The WebRTC / WebTransport removal script run in every frame before any page script (shared by the desktop view and the WebView).
QString webRtcRemovalScript();
// The Android WebView has no forced WebRTC policy (the desktop engine's `disable_non_proxied_udp` keeps even a script-made about:blank
// iframe silent): its UDP would leave around the proxy. Same removal plus a best-effort closing of the known hole: the document-start script
// does not run in the initial empty document of an iframe, so reading `iframe.contentWindow` / `contentDocument` (iframe, frame, object,
// embed) strips the WebRTC/WebTransport API from the child first. `window.frames[n]` / `window[n]` cannot be hooked in JavaScript: that path stays a
// documented gap (docs/BROWSER.md, "Android").
QString androidWebRtcRemovalScript();

}  // namespace fortiq::engine
