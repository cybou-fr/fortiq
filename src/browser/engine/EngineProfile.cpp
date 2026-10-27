#include "EngineProfile.h"

#include <QHostAddress>

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

}  // namespace fortiq::engine
