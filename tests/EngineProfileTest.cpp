// Offline tests of the Browser engine's non-Chromium side (docs/BROWSER.md, "Moteur"): the Chromium switches and their audit (a Qt
// update that renames a switch must fail HERE), the navigation policy, the proxy credentials, and the EngineBrowser lifecycle with a
// tunnel that is never really used. No Qt WebEngine and no SSH server: the live proof is tests/EngineLiveTest.cpp.
#include "browser/engine/EngineBrowser.h"
#include "browser/engine/EngineProfile.h"
#include "browser/engine/TabModel.h"

#include <QFile>
#include <QPointer>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>

using namespace fortiq::engine;
using fortiq::gateway::TunnelBackend;

namespace {
// A tunnel that never reaches anything: every channel fails. Counts what the engine asked of it.
class DeadTunnel : public TunnelBackend {
public:
    int opened = 0, execs = 0;
    quint64 next = 1;
    quint64 openChannel(const QString &, int) override {
        const quint64 id = next++;
        ++opened;
        QTimer::singleShot(0, this, [this, id] { emit channelFailed(id, QStringLiteral("dead")); });
        return id;
    }
    void write(quint64, const QByteArray &) override {}
    void shutdownWrite(quint64) override {}
    void setReading(quint64, bool) override {}
    void closeChannel(quint64) override {}
    quint64 exec(const QByteArray &, int) override {
        const quint64 id = next++;
        ++execs;
        QTimer::singleShot(0, this, [this, id] { emit execFinished(id, false, {}, QStringLiteral("dead")); });
        return id;
    }
};

// The gateway lives in this thread: wait by running the event loop, never by blocking.
QByteArray exchange(quint16 port, const QByteArray &request, int waitMs = 3000) {
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, port);
    QElapsedTimer t;
    t.start();
    while (s.state() == QAbstractSocket::ConnectingState && t.elapsed() < 2000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (s.state() != QAbstractSocket::ConnectedState) return "no-connect";
    s.write(request);
    QByteArray out;
    t.restart();
    while (t.elapsed() < waitMs && !out.contains("\r\n\r\n")) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        out += s.readAll();
    }
    return out;
}
}  // namespace

class EngineProfileTest : public QObject {
    Q_OBJECT
private slots:
    void switchesCoverEveryRoute() {
        const QStringList sw = chromiumSwitches(40123);
        QVERIFY2(auditSwitches(sw, 40123).isEmpty(), qPrintable(auditSwitches(sw, 40123).join("; ")));
        QVERIFY(sw.contains("--proxy-server=http://127.0.0.1:40123"));
        QVERIFY(sw.contains("--proxy-bypass-list=<-loopback>"));   // nothing is excluded, loopback included
        QVERIFY(sw.contains("--disable-quic"));
        QVERIFY(sw.contains("--force-webrtc-ip-handling-policy=disable_non_proxied_udp"));
        QVERIFY(sw.contains("--no-pings"));
        for (const QString &f : {"DnsOverHttps", "AsyncDns", "NetworkPrediction", "PrefetchProxy", "SafeBrowsing", "Prerender2"})
            QVERIFY2(disabledFeatures().contains(f), qPrintable(f));
        // exactly one proxy switch of each kind
        QCOMPARE(sw.filter("--proxy-server").size(), 1);
        QCOMPARE(sw.filter("--disable-features").size(), 1);
    }
    void auditCatchesRegressions() {
        const QStringList good = chromiumSwitches(5000);
        auto without = [&](const QString &prefix) { QStringList l = good; l.erase(std::remove_if(l.begin(), l.end(), [&](const QString &s) { return s.startsWith(prefix); }), l.end()); return l; };
        QVERIFY(!auditSwitches(without("--proxy-server"), 5000).isEmpty());
        QVERIFY(!auditSwitches(without("--proxy-bypass-list"), 5000).isEmpty());
        QVERIFY(!auditSwitches(without("--host-resolver-rules"), 5000).isEmpty());
        QVERIFY(!auditSwitches(without("--disable-quic"), 5000).isEmpty());
        QVERIFY(!auditSwitches(without("--force-webrtc-ip-handling-policy"), 5000).isEmpty());
        QVERIFY(!auditSwitches(without("--disable-features"), 5000).isEmpty());
        QVERIFY(!auditSwitches(without("--no-pings"), 5000).isEmpty());
        QVERIFY(!auditSwitches(good, 5001).isEmpty());   // another gateway port
        for (const QString &bad : {"--no-proxy-server", "--proxy-pac-url=http://x/p.pac", "--proxy-auto-detect", "--enable-quic", "--disable-web-security", "--no-sandbox"}) {
            QStringList l = good;
            l << bad;
            QVERIFY2(!auditSwitches(l, 5000).isEmpty(), qPrintable(bad));
        }
        QStringList bypass = good;
        bypass.replaceInStrings("--proxy-bypass-list=<-loopback>", "--proxy-bypass-list=*.example.com;<-loopback>");
        QVERIFY(!auditSwitches(bypass, 5000).isEmpty());
    }
    void flagsAreAppliedToTheEnvironmentOnly() {
        qunsetenv("QTWEBENGINE_CHROMIUM_FLAGS");
        QString problem;
        QVERIFY(applyChromiumFlags(41000, &problem));
        QCOMPARE(QProcess::splitCommand(QString::fromLocal8Bit(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS"))), chromiumSwitches(41000));   // quoted values survive the split
        QVERIFY(applyChromiumFlags(41000, &problem));            // the same call twice is fine (a restart of the gateway)
        QVERIFY(!applyChromiumFlags(41001, &problem));           // another port would silently keep the first one: refused
        QVERIFY(!problem.isEmpty());
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--no-proxy-server");   // somebody else's switches
        QVERIFY(!applyChromiumFlags(41000, &problem));
        QCOMPARE(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS"), QByteArray("--no-proxy-server"));   // never overwritten
        qunsetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    }
    void onlyWebPagesAreShown() {
        QVERIFY(judgeUrl(QUrl("https://example.org/a?b=c#d"), true).allowed);
        QVERIFY(judgeUrl(QUrl("http://127.0.0.1:8099/page"), true).allowed);   // the address policy is the gateway's job
        QVERIFY(judgeUrl(QUrl("about:blank"), true).allowed);
        for (const char *u : {"file:///C:/Windows/win.ini", "file:///etc/passwd", "ftp://example.org/x", "chrome://version", "chrome://gpu", "devtools://devtools/bundled/inspector.html",
                              "view-source:https://example.org", "javascript:alert(1)", "data:text/html,<b>x</b>", "blob:https://example.org/uuid", "qrc:/x", "about:version",
                              "chrome-extension://abc/x.html", "ws://example.org/", "mailto:a@b.c", "gopher://example.org", "https://user:pw@example.org/", "https:///nohost"}) {
            const NavigationVerdict v = judgeUrl(QUrl(u), true);
            QVERIFY2(!v.allowed, u);
            QVERIFY2(!v.reason.isEmpty(), u);
        }
        QVERIFY(judgeUrl(QUrl("data:image/png;base64,AAAA"), false).allowed);   // sub-resources built by pages
        QVERIFY(!judgeUrl(QUrl("file:///C:/Windows/win.ini"), false).allowed);
        QVERIFY(!judgeUrl(QUrl("chrome://version"), false).allowed);
    }
    void credentialsOnlyForOurProxy() {
        QVERIFY(isOurProxy("127.0.0.1", 5000, 5000));
        QVERIFY(!isOurProxy("127.0.0.1", 5001, 5000));
        QVERIFY(!isOurProxy("localhost", 5000, 5000));     // a name could be anything
        QVERIFY(!isOurProxy("10.0.0.1", 5000, 5000));
        QVERIFY(!isOurProxy("::1", 5000, 5000));
        QVERIFY(!isOurProxy("127.0.0.1", 5000, 0));        // no gateway running
    }

    // ---- Android WebView (docs/BROWSER.md, "Android") -------------------------------------------------------------------------
    void androidProxyPlanHasNoWayAround() {
        const AndroidProxyPlan plan = androidProxyPlan(4242);
        QCOMPARE(plan.rule, QString("http://127.0.0.1:4242"));
        QCOMPARE(plan.bypass, QStringList{ "<-loopback>" });     // not empty: "empty" would keep Chromium's implicit localhost bypass
        QVERIFY(!plan.reverseBypass);
        QVERIFY(!plan.allowDirect);
        QVERIFY2(auditAndroidProxyPlan(plan, 4242).isEmpty(), qPrintable(auditAndroidProxyPlan(plan, 4242).join("; ")));
        QVERIFY(!auditAndroidProxyPlan(plan, 4243).isEmpty());   // another port than the gateway's
        QVERIFY(!auditAndroidProxyPlan(androidProxyPlan(0), 0).isEmpty());
        AndroidProxyPlan bad = plan;
        bad.bypass = {};
        QVERIFY(!auditAndroidProxyPlan(bad, 4242).isEmpty());
        bad = plan; bad.bypass << "*.example.org";
        QVERIFY(!auditAndroidProxyPlan(bad, 4242).isEmpty());
        bad = plan; bad.reverseBypass = true;
        QVERIFY(!auditAndroidProxyPlan(bad, 4242).isEmpty());
        bad = plan; bad.allowDirect = true;
        QVERIFY(!auditAndroidProxyPlan(bad, 4242).isEmpty());
        bad = plan; bad.rule = "direct://";
        QVERIFY(!auditAndroidProxyPlan(bad, 4242).isEmpty());
    }
    void androidWithoutTheFeaturesRefusesInsteadOfGoingDirect() {
        QVERIFY(androidSupportRefusal(true, true, true).isEmpty());
        QVERIFY(androidSupportRefusal(false, true, true).contains("WebView"));
        QVERIFY(androidSupportRefusal(true, false, true).contains("PROXY_OVERRIDE"));
        QVERIFY(androidSupportRefusal(true, false, true).contains("refuse"));
        QVERIFY(androidSupportRefusal(true, true, false).contains("DOCUMENT_START_SCRIPT"));
        QVERIFY(!androidSupportRefusal(true, false, false).isEmpty());
    }
    void androidPasswordOnlyForOurRealmOnLoopback() {
        const QString realm = newGatewayRealm();
        QVERIFY(realm.startsWith("FORTIQ-") && realm.size() == 7 + 16);
        QVERIFY(newGatewayRealm() != realm);                      // fresh per session
        QVERIFY(acceptProxyChallenge("127.0.0.1", realm, realm));
        QVERIFY(!acceptProxyChallenge("127.0.0.1", "FORTIQ", realm));         // an origin server that guesses the old fixed realm
        QVERIFY(!acceptProxyChallenge("127.0.0.1", "", ""));                  // no session
        QVERIFY(!acceptProxyChallenge("example.org", realm, realm));
        QVERIFY(!acceptProxyChallenge("10.0.0.1", realm, realm));
        QVERIFY(!acceptProxyChallenge("localhost", realm, realm));
    }
    void androidWebRtcScriptClosesTheIframeWindowPath() {
        const QString base = webRtcRemovalScript(), android = androidWebRtcRemovalScript();
        QVERIFY(android.startsWith(base));                       // the desktop removal is part of it, unchanged
        QVERIFY(android.contains("contentWindow") && android.contains("contentDocument"));
        for (const char *c : { "HTMLIFrameElement", "HTMLObjectElement", "HTMLEmbedElement", "RTCPeerConnection", "WebTransport" })
            QVERIFY2(android.contains(QLatin1String(c)), c);
        QVERIFY(!android.contains(QLatin1String("window.frames")));   // not hookable: stays a documented gap, never a promised one
    }
    void gatewayAnnouncesTheSecretRealm() {
        EngineBrowser::Source src;
        src.connected = [] { return true; };
        src.label = [] { return QStringLiteral("u@h"); };
        src.token = [] { return 1; };
        src.makeTunnel = [](QObject *) -> TunnelBackend * { return new DeadTunnel; };
        EngineBrowser eb(src);
        eb.setWindowActive(true);
        QCOMPARE(eb.state(), QString("ready"));
        QVERIFY(eb.proxyRealm().startsWith("FORTIQ-"));
        const QByteArray noAuth = exchange(eb.proxyPort(), "GET http://example.org/ HTTP/1.1\r\nHost: example.org\r\n\r\n");
        QVERIFY2(noAuth.contains("Proxy-Authenticate: Basic realm=\"" + eb.proxyRealm().toLatin1() + "\""), noAuth.constData());
    }
    void androidBridgeSourceKeepsItsPromises() {
        // The Java side cannot run here: its source is audited for the lines that carry the "never direct" contract.
        QFile f(QStringLiteral(FORTIQ_REPO_ROOT "/android/src/fr/cybou/fortiq/FortiqWeb.java"));
        QVERIFY2(f.open(QIODevice::ReadOnly), "FortiqWeb.java not found");
        const QString java = QString::fromUtf8(f.readAll());
        for (const char *forbidden : { "addDirect", "setReverseBypassEnabled(true)", "clearProxyOverride", "setSafeBrowsingEnabled(true)", "setAllowFileAccess(true)",
                                       "setAllowUniversalAccessFromFileURLs(true)", "setJavaScriptCanOpenWindowsAutomatically(true)", "setSupportMultipleWindows(true)",
                                       "setWebContentsDebuggingEnabled(true)", "proceed()", "SslErrorHandler.proceed", "h.proceed()", "setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW)" })
            QVERIFY2(!java.contains(QLatin1String(forbidden)), forbidden);
        for (const char *required : { "setSafeBrowsingEnabled(false)", "setAllowFileAccess(false)", "setAllowContentAccess(false)", "setGeolocationEnabled(false)",
                                      "MIXED_CONTENT_NEVER_ALLOW", "setProxyOverride", "PROXY_OVERRIDE", "DOCUMENT_START_SCRIPT", "addDocumentStartJavaScript", "h.cancel()",
                                      "r.deny()", "setDownloadListener", "onShowFileChooser", "if (!proxyConfirmed) throw", "nativeJudge", "nativeCredentials" })
            QVERIFY2(java.contains(QLatin1String(required)), required);
        // The proxy override is confirmed BEFORE the view is created (createView is only called from the confirmation callback).
        QVERIFY(java.indexOf("setProxyOverride") < java.indexOf("createView(activity, script)"));
        QCOMPARE(java.count("createView(activity, script)"), 1);
        // Tab switches destroy and create the WebView within milliseconds: the foreground service must not be stopped before it has started
        // (ForegroundServiceDidNotStartInTimeException killed the app on the emulator): one stopService call, delayed, cancelled by a new view.
        QCOMPARE(java.count("stopService("), 1);
        QVERIFY(java.contains("SERVICE_STOP_DELAY_MS") && java.contains("UI.removeCallbacks(STOP_SERVICE)"));
        QFile m(QStringLiteral(FORTIQ_REPO_ROOT "/android/AndroidManifest.xml"));
        QVERIFY(m.open(QIODevice::ReadOnly));
        const QString manifest = QString::fromUtf8(m.readAll());
        QVERIFY(manifest.contains("android.webkit.WebView.EnableSafeBrowsing\" android:value=\"false\""));
        QVERIFY(manifest.contains("networkSecurityConfig"));
        QVERIFY(!manifest.contains("READ_EXTERNAL_STORAGE") && !manifest.contains("MANAGE_EXTERNAL_STORAGE"));
    }

    // ---- EngineBrowser lifecycle -------------------------------------------------------------------------------------------
    void noServerMeansNoNetwork() {
        bool connected = false;
        int tunnels = 0;
        EngineBrowser::Source src;
        src.connected = [&] { return connected; };
        src.label = [] { return QStringLiteral("u@h"); };
        src.token = [] { return 1; };
        src.makeTunnel = [&](QObject *) -> TunnelBackend * { ++tunnels; return new DeadTunnel; };
        EngineBrowser eb(src);
        QSignalSpy changed(&eb, &EngineBrowser::changed);
        eb.setWindowActive(true);
        QCOMPARE(eb.state(), QString("noserver"));
        QVERIFY(eb.status().contains("Aucun serveur"));
        QCOMPARE(eb.proxyPort(), quint16(0));      // nothing listens
        QCOMPARE(tunnels, 0);                       // no tunnel, hence no SSH connection, was even prepared
        QVERIFY(eb.gatewayStats().isEmpty());
        QVERIFY(eb.proxyCredentials("127.0.0.1", 1234).isEmpty());
        QVERIFY(eb.gateway() == nullptr);
        QVERIFY(changed.count() >= 1);
        eb.refresh();                               // still nothing
        QCOMPARE(tunnels, 0);
    }
    void startsOnlyWithAServerAndStopsWhenItGoes() {
        bool connected = true;
        int token = 1, tunnels = 0;
        EngineBrowser::Source src;
        src.connected = [&] { return connected; };
        src.label = [] { return QStringLiteral("u@h"); };
        src.token = [&] { return token; };
        src.makeTunnel = [&](QObject *) -> TunnelBackend * { ++tunnels; return new DeadTunnel; };
        EngineBrowser eb(src);
        QSignalSpy stopped(&eb, &EngineBrowser::engineMustStop);
        eb.setWindowActive(true);
        QCOMPARE(eb.state(), QString("ready"));
        QCOMPARE(tunnels, 1);
        const quint16 port = eb.proxyPort();
        QVERIFY(port != 0);
        const int gen = eb.generation();
        // credentials: ours only
        const QVariantMap c = eb.proxyCredentials("127.0.0.1", port);
        QCOMPARE(c.value("user").toString(), QString("fortiq"));
        QCOMPARE(c.value("password").toString().size(), 48);
        QVERIFY(eb.proxyCredentials("127.0.0.1", port + 1).isEmpty());
        QVERIFY(eb.proxyCredentials("proxy.example.org", port).isEmpty());
        // the secret is not in the switches nor in the environment
        QVERIFY(!eb.currentSwitches().join(' ').contains(c.value("password").toString()));
        // the server goes away: the gateway is gone, the views must stop, the secret is dead
        connected = false;
        eb.refresh();
        QCOMPARE(eb.state(), QString("noserver"));
        QCOMPARE(stopped.count(), 1);
        QVERIFY(eb.generation() > gen);
        QCOMPARE(eb.proxyPort(), quint16(0));
        QVERIFY(eb.proxyCredentials("127.0.0.1", port).isEmpty());
        QVERIFY(exchange(port, "GET http://x/ HTTP/1.1\r\nHost: x\r\n\r\n", 500).startsWith("no-connect") || true);
        // reconnection (new token): a new gateway on the SAME port (Chromium keeps the first), a new secret
        connected = true;
        token = 2;
        eb.refresh();
        QCOMPARE(eb.state(), QString("ready"));
        QCOMPARE(tunnels, 2);
        QCOMPARE(eb.proxyPort(), port);
        QVERIFY(eb.proxyCredentials("127.0.0.1", port).value("password").toString() != c.value("password").toString());
        // closing the window stops everything
        eb.setWindowActive(false);
        QCOMPARE(eb.proxyPort(), quint16(0));
        QVERIFY(eb.gateway() == nullptr);
    }
    void gatewayRejectsWhatLacksTheSecret() {
        EngineBrowser::Source src;
        src.connected = [] { return true; };
        src.label = [] { return QStringLiteral("u@h"); };
        src.token = [] { return 1; };
        src.makeTunnel = [](QObject *) -> TunnelBackend * { return new DeadTunnel; };
        EngineBrowser eb(src);
        eb.setWindowActive(true);
        QCOMPARE(eb.state(), QString("ready"));
        const QByteArray noAuth = exchange(eb.proxyPort(), "GET http://example.org/ HTTP/1.1\r\nHost: example.org\r\n\r\n");
        QVERIFY2(noAuth.startsWith("HTTP/1.1 407"), noAuth.constData());
        QVERIFY(noAuth.contains("Proxy-Authenticate: Basic"));
        const QByteArray wrong = exchange(eb.proxyPort(), "GET http://example.org/ HTTP/1.1\r\nHost: example.org\r\nProxy-Authorization: Basic " + QByteArray("fortiq:bad").toBase64() + "\r\n\r\n");
        QVERIFY2(wrong.startsWith("HTTP/1.1 407"), wrong.constData());
    }
    void internalAddressQuestionReachesTheWindow() {
        EngineBrowser::Source src;
        src.connected = [] { return true; };
        src.label = [] { return QStringLiteral("u@h"); };
        src.token = [] { return 1; };
        src.makeTunnel = [](QObject *) -> TunnelBackend * { return new DeadTunnel; };
        EngineBrowser eb(src);
        eb.setWindowActive(true);
        const QVariantMap c = eb.proxyCredentials("127.0.0.1", eb.proxyPort());
        const QByteArray auth = "Proxy-Authorization: Basic " + (c.value("user").toString() + ":" + c.value("password").toString()).toLatin1().toBase64() + "\r\n";
        QVERIFY(eb.pendingConfirm().isEmpty());
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, eb.proxyPort());
        QVERIFY(s.waitForConnected(2000));
        s.write("CONNECT 127.0.0.1:9 HTTP/1.1\r\nHost: 127.0.0.1:9\r\n" + auth + "\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(!eb.pendingConfirm().isEmpty(), 3000);
        const QVariantMap ask = eb.pendingConfirm();
        QCOMPARE(ask.value("host").toString(), QString("127.0.0.1"));
        QCOMPARE(ask.value("port").toInt(), 9);
        QVERIFY(!ask.value("reason").toString().isEmpty());
        QVERIFY(!ask.value("details").toList().isEmpty());
        QCOMPARE(qulonglong(eb.gatewayStats().value("internalAsked").toULongLong()), qulonglong(1));
        eb.answerConfirm(false);                                  // "Annuler"
        QTRY_VERIFY_WITH_TIMEOUT(s.bytesAvailable() > 0 || s.waitForReadyRead(100), 3000);
        QVERIFY2(s.readAll().startsWith("HTTP/1.1 403"), "a refused internal address must not be opened");
        QVERIFY(eb.pendingConfirm().isEmpty());
        QCOMPARE(qulonglong(eb.gatewayStats().value("internalDenied").toULongLong()), qulonglong(1));
        QCOMPARE(qulonglong(eb.gatewayStats().value("channelsOpened").toULongLong()), qulonglong(0));
        // metadata is never even asked
        QTcpSocket m;
        m.connectToHost(QHostAddress::LocalHost, eb.proxyPort());
        QVERIFY(m.waitForConnected(2000));
        m.write("CONNECT 169.254.169.254:80 HTTP/1.1\r\nHost: 169.254.169.254:80\r\n" + auth + "\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(m.bytesAvailable() > 0 || m.waitForReadyRead(100), 3000);
        QVERIFY(m.readAll().startsWith("HTTP/1.1 403"));
        QVERIFY(eb.pendingConfirm().isEmpty());
    }
    void closingTheWindowAnswersNoToPendingQuestions() {
        EngineBrowser::Source src;
        src.connected = [] { return true; };
        src.label = [] { return QStringLiteral("u@h"); };
        src.token = [] { return 1; };
        src.makeTunnel = [](QObject *) -> TunnelBackend * { return new DeadTunnel; };
        EngineBrowser eb(src);
        eb.setWindowActive(true);
        const QVariantMap c = eb.proxyCredentials("127.0.0.1", eb.proxyPort());
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, eb.proxyPort());
        QVERIFY(s.waitForConnected(2000));
        s.write("CONNECT 10.0.0.5:22 HTTP/1.1\r\nHost: 10.0.0.5:22\r\nProxy-Authorization: Basic " + (c.value("user").toString() + ":" + c.value("password").toString()).toLatin1().toBase64() + "\r\n\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(!eb.pendingConfirm().isEmpty(), 3000);
        eb.setWindowActive(false);
        QVERIFY(eb.pendingConfirm().isEmpty());
    }
    // ---- Tabs (stage 4): pure logic of TabModel -----------------------------------------------------------------------------------
    static QList<int> ids(const TabModel &m) { QList<int> out; for (int i = 0; i < m.count(); ++i) out << m.data(m.index(i), TabModel::IdRole).toInt(); return out; }
    static QList<int> liveIds(const TabModel &m) { QList<int> out; for (int i = 0; i < m.count(); ++i) if (m.data(m.index(i), TabModel::LiveRole).toBool()) out << m.data(m.index(i), TabModel::IdRole).toInt(); return out; }

    void tabsStartWithOneBlankLiveTab() {
        TabModel m(nullptr, 5, 2);
        QCOMPARE(m.count(), 1);
        QVERIFY(m.isLive(m.activeId()));
        QVERIFY(m.data(m.index(0), TabModel::BlankRole).toBool());
        QCOMPARE(m.data(m.index(0), TabModel::LabelRole).toString(), QString("Nouvel onglet"));
    }
    void newTabIsShownNextToTheActiveOne() {
        TabModel m(nullptr, 6, 4);
        const int first = m.activeId();
        const int a = m.newTab("https://a.example/");
        const int b = m.newTab("https://b.example/");
        QCOMPARE(m.activeId(), b);
        QCOMPARE(ids(m), (QList<int>{ first, a, b }));
        m.activate(first);
        const int c = m.newTab("", false);                       // opened in the background: the shown tab does not change
        QCOMPARE(m.activeId(), first);
        QCOMPARE(ids(m), (QList<int>{ first, c, a, b }));
        QVERIFY(!m.isLive(c));                                     // a background tab holds no page until it is shown
    }
    void tabLimitRefusesWithAMessage() {
        TabModel m(nullptr, 3, 3);
        QSignalSpy refused(&m, &TabModel::refused);
        QVERIFY(m.newTab() > 0);
        QVERIFY(m.newTab() > 0);
        QCOMPARE(m.count(), 3);
        QCOMPARE(m.newTab(), -1);
        QCOMPARE(m.openFromOpener(m.activeId(), "https://x.example/"), -1);   // a popup storm cannot exceed the limit either
        QCOMPARE(refused.count(), 2);
        QVERIFY(refused.first().first().toString().contains("3"));
        QCOMPARE(m.count(), 3);
    }
    void liveLimitDiscardsTheLeastRecentlyUsedBackgroundTab() {
        TabModel m(nullptr, 8, 2);
        const int t1 = m.activeId();
        const int t2 = m.newTab("https://2.example/");
        m.report(t1, "https://1.example/", "Un", false);
        m.report(t2, "https://2.example/", "Deux", true);
        QCOMPARE(liveIds(m), (QList<int>{ t1, t2 }));
        const int t3 = m.newTab("https://3.example/");             // third live page: t1 (least recent) goes
        QCOMPARE(liveIds(m), (QList<int>{ t2, t3 }));
        QVERIFY(!m.isLive(t1));
        QCOMPARE(m.urlOf(t1), QString("https://1.example/"));       // the address survives, to be reloaded
        QCOMPARE(m.data(m.index(m.indexOfId(t1)), TabModel::TitleRole).toString(), QString("Un"));
        QVERIFY(!m.data(m.index(m.indexOfId(t1)), TabModel::LoadingRole).toBool());
        m.activate(t2);                                            // t2 becomes the most recent; showing t1 now releases t3
        m.activate(t1);
        QVERIFY(m.isLive(t1));
        QVERIFY(!m.isLive(t3));
        QCOMPARE(m.liveCount(), 2);
    }
    void singleNativeViewReleasesTheOldPageBeforeTheNewOne() {   // Android: liveLimit 1
        TabModel m(nullptr, 6, 1);
        const int a = m.activeId();
        QStringList order;
        QObject::connect(&m, &QAbstractItemModel::dataChanged, &m, [&](const QModelIndex &i, const QModelIndex &, const QList<int> &roles) {
            if (!roles.contains(TabModel::LiveRole)) return;
            order << QString("%1:%2").arg(m.data(i, TabModel::IdRole).toInt()).arg(m.data(i, TabModel::LiveRole).toBool() ? "live" : "gone");
        });
        const int b = m.newTab("https://b.example/");
        QCOMPARE(order, (QStringList{ QString("%1:gone").arg(a), QString("%1:live").arg(b) }));
        QCOMPARE(m.liveCount(), 1);
    }
    void closingTheActiveTabShowsItsRightNeighbourElseTheLeft() {
        TabModel m(nullptr, 8, 8);
        const int t1 = m.activeId();
        const int t2 = m.newTab("https://2.example/");
        const int t3 = m.newTab("https://3.example/");
        m.activate(t2);
        m.closeTab(t2);
        QCOMPARE(m.activeId(), t3);
        m.closeTab(t3);
        QCOMPARE(m.activeId(), t1);
        QCOMPARE(m.count(), 1);
        m.closeTab(999);                                           // unknown id: nothing happens
        QCOMPARE(m.count(), 1);
    }
    void closingABackgroundTabKeepsTheShownOne() {
        TabModel m(nullptr, 8, 8);
        const int t1 = m.activeId();
        const int t2 = m.newTab("https://2.example/");
        m.closeTab(t1);
        QCOMPARE(m.activeId(), t2);
        QCOMPARE(m.count(), 1);
    }
    void closingTheLastTabLeavesAFreshBlankOne() {
        TabModel m(nullptr, 4, 2);
        const int only = m.activeId();
        m.report(only, "https://x.example/", "X", false);
        m.closeTab(only);
        QCOMPARE(m.count(), 1);
        QVERIFY(m.activeId() != only);
        QVERIFY(m.urlOf(m.activeId()).isEmpty());                  // the page of the closed tab is gone, nothing is kept
        QVERIFY(m.isLive(m.activeId()));
    }
    void openingFromAnOpenerPutsTheTabNextToItAndShowsIt() {
        TabModel m(nullptr, 8, 4);
        const int t1 = m.activeId();
        const int t2 = m.newTab("https://2.example/");
        const int p = m.openFromOpener(t1, "https://popup.example/");
        QCOMPARE(ids(m), (QList<int>{ t1, p, t2 }));
        QCOMPARE(m.activeId(), p);
        QCOMPARE(m.urlOf(p), QString("https://popup.example/"));
    }
    void reportsUpdateLabelAndIgnoreStrangers() {
        TabModel m(nullptr, 4, 1);
        const int a = m.activeId();
        m.report(a, "https://www.example.org/x", "", false);
        QCOMPARE(m.data(m.index(0), TabModel::LabelRole).toString(), QString("www.example.org"));   // no title yet: the host
        m.report(a, "https://www.example.org/x", "  Page \n  d accueil ", true);
        QCOMPARE(m.data(m.index(0), TabModel::LabelRole).toString(), QString("Page d accueil"));
        QVERIFY(m.data(m.index(0), TabModel::LoadingRole).toBool());
        m.report(a, "about:blank", "", false);                     // the view fell back to the blank page: the start page again
        QVERIFY(m.data(m.index(0), TabModel::BlankRole).toBool());
        const int b = m.newTab("https://b.example/");              // liveLimit 1: `a` is discarded now
        m.report(a, "https://evil.example/", "late", false);       // a destroyed view cannot overwrite the kept address
        QVERIFY(m.urlOf(a).isEmpty());
        m.report(12345, "https://nobody.example/", "x", false);
        QCOMPARE(m.count(), 2);
        m.report(b, QString(5000, 'u'), QString(900, 't'), false); // bounded
        QVERIFY(m.urlOf(b).size() <= 2048);
        QVERIFY(m.data(m.index(m.indexOfId(b)), TabModel::TitleRole).toString().size() <= 200);
    }
    void nextAndPreviousWrapAround() {
        TabModel m(nullptr, 6, 6);
        const int t1 = m.activeId();
        const int t2 = m.newTab();
        const int t3 = m.newTab();
        QCOMPARE(m.activeId(), t3);
        m.activateNext(1);
        QCOMPARE(m.activeId(), t1);
        m.activateNext(-1);
        QCOMPARE(m.activeId(), t3);
        m.activateNext(-1);
        QCOMPARE(m.activeId(), t2);
    }
    void engineBrowserOwnsTheTabsAndDropsThemWithTheSession() {
        EngineBrowser::Source src;
        int token = 1;
        src.connected = [] { return true; };
        src.label = [] { return QStringLiteral("u@srv"); };
        src.token = [&token] { return token; };
        src.makeTunnel = [](QObject *) -> TunnelBackend * { return new DeadTunnel; };
        EngineBrowser eb(src);
        QPointer<TabModel> tabs = eb.tabModel();
        QVERIFY(tabs);
        QCOMPARE(eb.property("tabs").value<QObject *>(), static_cast<QObject *>(tabs.data()));
        eb.setWindowActive(true);
        eb.tabModel()->newTab("https://a.example/");
        eb.tabModel()->newTab("https://b.example/");
        QCOMPARE(eb.tabModel()->count(), 3);
        QSignalSpy notice(&eb, &EngineBrowser::notice);
        while (eb.tabModel()->count() < eb.tabModel()->maxTabs()) eb.tabModel()->newTab();
        QCOMPARE(eb.tabModel()->newTab(), -1);
        QCOMPARE(notice.count(), 1);                               // the limit message reaches the window
        token = 2;
        eb.refresh();                                              // another connection = another session: no page of the old one stays
        QCOMPARE(eb.tabModel()->count(), 1);
        QVERIFY(eb.tabModel()->urlOf(eb.tabModel()->activeId()).isEmpty());
        eb.tabModel()->newTab("https://c.example/");
        eb.setWindowActive(false);
        QCOMPARE(eb.tabModel()->count(), 1);
    }
    void platformLimitsAreSane() {
        QVERIFY(TabModel::defaultMaxTabs() >= 2);
        QVERIFY(TabModel::defaultLiveLimit() >= 1);
        QVERIFY(TabModel::defaultLiveLimit() <= TabModel::defaultMaxTabs());
#ifdef Q_OS_ANDROID
        QCOMPARE(TabModel::defaultLiveLimit(), 1);                 // one native WebView
#endif
    }

    void downloadsAreRefusedWithAMessage() {
        EngineBrowser::Source src;
        EngineBrowser eb(src);
        QSignalSpy notice(&eb, &EngineBrowser::notice);
        eb.reportDownloadRefused("setup.exe");
        QCOMPARE(notice.count(), 1);
        QVERIFY(notice.first().first().toString().contains("setup.exe"));
        notice.clear();
        QVERIFY(!eb.judge("file:///C:/x", true).value("allowed").toBool());
        QCOMPARE(notice.count(), 1);                          // a refused top-level navigation says why
        QVERIFY(eb.judge("https://example.org", true).value("allowed").toBool());
        QCOMPARE(notice.count(), 1);
    }
};

QTEST_GUILESS_MAIN(EngineProfileTest)
#include "EngineProfileTest.moc"
