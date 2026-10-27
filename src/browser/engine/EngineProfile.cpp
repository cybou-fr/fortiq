#include "EngineProfile.h"

#include <QHostAddress>
#include <QRandomGenerator>

namespace fortiq::engine {

QStringList disabledFeatures() {
    return {
        // Name resolution, prediction and speculative work that could contact a host before the user asked.
        QStringLiteral("DnsOverHttps"), QStringLiteral("AsyncDns"), QStringLiteral("UseDnsHttpsSvcb"), QStringLiteral("NetworkPrediction"),
        QStringLiteral("NetworkQualityEstimator"), QStringLiteral("PrefetchProxy"), QStringLiteral("NoStatePrefetch"),
        QStringLiteral("Prerender2"), QStringLiteral("SpeculationRulesPrefetchFuture"), QStringLiteral("LinkPreview"),
        // Google services of the browser itself.
        QStringLiteral("SafeBrowsing"), QStringLiteral("SafeBrowsingEnhancedProtection"), QStringLiteral("OptimizationHints"),
        QStringLiteral("OptimizationGuideModelDownloading"), QStringLiteral("Translate"), QStringLiteral("MediaRouter"),
        QStringLiteral("NetworkTimeServiceQuerying"), QStringLiteral("AutofillServerCommunication"),
        QStringLiteral("CertificateTransparencyComponentUpdater"), QStringLiteral("InterestFeedContentSuggestions"),
        // mDNS answers on the local network (a multicast UDP socket) and ECH needs lookups that cannot go through an HTTP proxy.
        QStringLiteral("WebRtcHideLocalIpsWithMdns"), QStringLiteral("EncryptedClientHello"),
    };
}

QStringList chromiumSwitches(quint16 proxyPort) {
    const QString proxy = QStringLiteral("http://127.0.0.1:%1").arg(proxyPort);
    return {
        // ONE way out: the loopback gateway. "<-loopback>" removes Chromium's implicit "never proxy localhost", so a page asking for
        // 127.0.0.1 or localhost reaches the SERVER's loopback (and the address policy), never a service of this computer.
        QStringLiteral("--proxy-server=") + proxy,
        QStringLiteral("--proxy-bypass-list=<-loopback>"),
        // Belt and braces: every local name resolution fails except the proxy address. With the proxy the NAME is sent to the
        // server (CONNECT host:port) and resolved there; a request that skipped the proxy would find no address.
        QStringLiteral("--host-resolver-rules=MAP * ~NOTFOUND , EXCLUDE 127.0.0.1"),
        QStringLiteral("--disable-quic"),
        // WebRTC: with this policy no UDP socket is opened and no ICE candidate is produced (the policy is a Chromium preference; this
        // switch is the one that forces it). tests/EngineLiveTest.cpp watches the UDP table and the candidates of a real RTCPeerConnection.
        QStringLiteral("--force-webrtc-ip-handling-policy=disable_non_proxied_udp"),
        QStringLiteral("--enforce-webrtc-ip-permission-check"),
        QStringLiteral("--disable-features=") + disabledFeatures().join(QLatin1Char(',')),
        QStringLiteral("--disable-background-networking"),
        QStringLiteral("--disable-component-update"),
        QStringLiteral("--disable-sync"),
        QStringLiteral("--disable-domain-reliability"),
        QStringLiteral("--disable-client-side-phishing-detection"),
        QStringLiteral("--disable-default-apps"),
        QStringLiteral("--disable-breakpad"),
        QStringLiteral("--disable-reporting-api"),
        QStringLiteral("--no-pings"),
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        QStringLiteral("--metrics-recording-only"),
        QStringLiteral("--password-store=basic"),
    };
}

bool applyChromiumFlags(quint16 proxyPort, QString *problem) {
    const QByteArray existing = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS").trimmed();
    // Qt splits the variable on blanks and understands double quotes: a value with blanks (the resolver rules) is quoted.
    QStringList quoted;
    for (const QString &sw : chromiumSwitches(proxyPort)) {
        const qsizetype eq = sw.indexOf(QLatin1Char('='));
        quoted << (sw.contains(QLatin1Char(' ')) && eq > 0 ? sw.left(eq + 1) + QLatin1Char('"') + sw.mid(eq + 1) + QLatin1Char('"') : sw);
    }
    const QString wanted = quoted.join(QLatin1Char(' '));
    if (!existing.isEmpty() && QString::fromLocal8Bit(existing) != wanted) {
        if (problem) *problem = QStringLiteral("QTWEBENGINE_CHROMIUM_FLAGS contient déjà des options : le navigateur refuse de démarrer avec des options inconnues.");
        return false;
    }
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", wanted.toLocal8Bit());
    return true;
}

QStringList auditSwitches(const QStringList &switches, quint16 proxyPort) {
    QStringList problems;
    auto value = [&](const QString &name) {
        QString found;
        for (const QString &s : switches)
            if (s.startsWith(name + QLatin1Char('='))) found = s.mid(name.size() + 1);
        return found;
    };
    auto has = [&](const QString &name) { return switches.contains(name); };
    if (value(QStringLiteral("--proxy-server")) != QStringLiteral("http://127.0.0.1:%1").arg(proxyPort))
        problems << QStringLiteral("--proxy-server absent ou différent de la passerelle");
    if (value(QStringLiteral("--proxy-bypass-list")) != QLatin1String("<-loopback>"))
        problems << QStringLiteral("--proxy-bypass-list doit valoir <-loopback> (aucune exception)");
    if (!value(QStringLiteral("--host-resolver-rules")).contains(QLatin1String("MAP * ~NOTFOUND")))
        problems << QStringLiteral("--host-resolver-rules ne coupe pas la résolution locale");
    if (!has(QStringLiteral("--disable-quic"))) problems << QStringLiteral("QUIC n'est pas désactivé");
    if (value(QStringLiteral("--force-webrtc-ip-handling-policy")) != QLatin1String("disable_non_proxied_udp"))
        problems << QStringLiteral("la politique WebRTC n'est pas disable_non_proxied_udp");
    const QStringList features = value(QStringLiteral("--disable-features")).split(QLatin1Char(','));
    for (const QString &f : {QStringLiteral("DnsOverHttps"), QStringLiteral("AsyncDns"), QStringLiteral("NetworkPrediction"), QStringLiteral("SafeBrowsing")})
        if (!features.contains(f)) problems << QStringLiteral("fonction non désactivée : ") + f;
    for (const QString &s : {QStringLiteral("--no-pings"), QStringLiteral("--disable-background-networking"), QStringLiteral("--disable-component-update"), QStringLiteral("--disable-sync")})
        if (!has(s)) problems << QStringLiteral("option absente : ") + s;
    // Contradictions: a switch that turns a route back on.
    for (const QString &s : switches) {
        if (s.startsWith(QLatin1String("--no-proxy-server")) || s.startsWith(QLatin1String("--proxy-auto-detect")) || s.startsWith(QLatin1String("--proxy-pac-url")) ||
            s.startsWith(QLatin1String("--enable-quic")) || s.startsWith(QLatin1String("--enable-webrtc")) ||
            s.startsWith(QLatin1String("--disable-web-security")) || s.startsWith(QLatin1String("--allow-running-insecure")) || s.startsWith(QLatin1String("--no-sandbox")))
            problems << QStringLiteral("option interdite : ") + s;
    }
    return problems;
}

NavigationVerdict judgeUrl(const QUrl &url, bool topLevel) {
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        if (url.host().isEmpty()) return { false, QStringLiteral("Adresse sans nom d'hôte.") };
        if (!url.userInfo().isEmpty()) return { false, QStringLiteral("Les identifiants dans l'adresse (utilisateur:mot de passe@) sont refusés.") };
        return { true, {} };
    }
    if (url.toString() == QLatin1String("about:blank")) return { true, {} };
    if (!topLevel && (scheme == QLatin1String("data") || scheme == QLatin1String("blob") || scheme == QLatin1String("about")))
        return { true, {} };
    const QString what = scheme.isEmpty() ? QStringLiteral("cette adresse") : QStringLiteral("« %1: »").arg(scheme);
    return { false, QStringLiteral("FORTIQ n'ouvre que des pages http et https : %1 est refusé.").arg(what) };
}

bool isOurProxy(const QString &host, int port, quint16 gatewayPort) {
    if (gatewayPort == 0 || port != gatewayPort) return false;
    QHostAddress a;
    if (!a.setAddress(host)) return false;
    return a == QHostAddress::LocalHost;
}

QString permissionRefusal() {
    return QStringLiteral("Autorisation refusée : FORTIQ ne donne aucun accès (position, caméra, micro, notifications, presse-papiers) aux pages web.");
}


AndroidProxyPlan androidProxyPlan(quint16 gatewayPort) {
    AndroidProxyPlan plan;
    plan.rule = QStringLiteral("http://127.0.0.1:%1").arg(gatewayPort);
    plan.bypass = { QStringLiteral("<-loopback>") };
    return plan;
}

QStringList auditAndroidProxyPlan(const AndroidProxyPlan &plan, quint16 gatewayPort) {
    QStringList problems;
    if (gatewayPort == 0) problems << QStringLiteral("port de la passerelle inconnu");
    if (plan.rule != QStringLiteral("http://127.0.0.1:%1").arg(gatewayPort)) problems << QStringLiteral("la règle de proxy n'est pas la passerelle");
    if (plan.bypass != QStringList{ QStringLiteral("<-loopback>") }) problems << QStringLiteral("la liste d'exceptions doit valoir exactement <-loopback>");
    if (plan.reverseBypass) problems << QStringLiteral("exceptions inversées interdites");
    if (plan.allowDirect) problems << QStringLiteral("accès direct interdit");
    return problems;
}

QString androidSupportRefusal(bool webViewInstalled, bool proxyOverride, bool documentStartScript) {
    if (!webViewInstalled) return QStringLiteral("Android System WebView n'est pas disponible sur cet appareil : le navigateur ne démarre pas.");
    if (!proxyOverride)
        return QStringLiteral("Ce WebView ne sait pas forcer un proxy (PROXY_OVERRIDE) : le navigateur refuse de démarrer plutôt que d'accéder au réseau directement. Mettez à jour « Android System WebView » ou utilisez le lecteur simple.");
    if (!documentStartScript)
        return QStringLiteral("Ce WebView ne sait pas exécuter un script avant la page (DOCUMENT_START_SCRIPT), nécessaire pour retirer WebRTC : le navigateur refuse de démarrer. Mettez à jour « Android System WebView » ou utilisez le lecteur simple.");
    return {};
}

bool acceptProxyChallenge(const QString &host, const QString &realm, const QString &expectedRealm) {
    if (expectedRealm.isEmpty() || realm != expectedRealm) return false;
    QHostAddress a;
    return a.setAddress(host) && a == QHostAddress::LocalHost;
}

QString newGatewayRealm() {
    static const char hex[] = "0123456789abcdef";
    QString r = QStringLiteral("FORTIQ-");
    for (int i = 0; i < 16; ++i) r += QLatin1Char(hex[QRandomGenerator::system()->bounded(16)]);
    return r;
}

QString webRtcRemovalScript() {
    return QStringLiteral("(function(){var n=['RTCPeerConnection','webkitRTCPeerConnection','RTCDataChannel','RTCSessionDescription','RTCIceCandidate','RTCPeerConnectionIceEvent','RTCRtpSender','RTCRtpReceiver','RTCRtpTransceiver','RTCDtlsTransport','RTCIceTransport','RTCSctpTransport','RTCCertificate','RTCDTMFSender','RTCStatsReport','RTCError','RTCErrorEvent','RTCTrackEvent','RTCDataChannelEvent','WebTransport','WebTransportBidirectionalStream','WebTransportDatagramDuplexStream','WebTransportError'];for(var i=0;i<n.length;i++){try{delete window[n[i]];Object.defineProperty(window,n[i],{value:undefined,configurable:false,writable:false});}catch(e){}}})();");
}

QString androidWebRtcRemovalScript() {
    return webRtcRemovalScript() + QStringLiteral(
        "(function(){var n=['RTCPeerConnection','webkitRTCPeerConnection','RTCDataChannel','RTCSessionDescription','RTCIceCandidate','RTCPeerConnectionIceEvent','RTCRtpSender','RTCRtpReceiver','RTCRtpTransceiver','RTCDtlsTransport','RTCIceTransport','RTCSctpTransport','WebTransport'];"
        "function strip(w){try{if(!w)return w;for(var i=0;i<n.length;i++){try{delete w[n[i]];Object.defineProperty(w,n[i],{value:undefined,configurable:false,writable:false});}catch(e){}}}catch(e){}return w;}"
        "['HTMLIFrameElement','HTMLFrameElement','HTMLObjectElement','HTMLEmbedElement'].forEach(function(c){var P=window[c]&&window[c].prototype;if(!P)return;"
        "['contentWindow','contentDocument'].forEach(function(p){var d=Object.getOwnPropertyDescriptor(P,p);if(!d||!d.get)return;"
        "Object.defineProperty(P,p,{configurable:true,enumerable:d.enumerable,get:function(){var v=d.get.call(this);if(p==='contentWindow')strip(v);else if(v)strip(v.defaultView);return v;}});});});})();");
}

}  // namespace fortiq::engine
