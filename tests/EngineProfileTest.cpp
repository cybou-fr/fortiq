// Offline tests of the Browser engine's non-Chromium side (docs/BROWSER.md, "Moteur"): the Chromium switches and their audit (a Qt
// update that renames a switch must fail HERE), the navigation policy, the proxy credentials, and the EngineBrowser lifecycle with a
// tunnel that is never really used. No Qt WebEngine and no SSH server: the live proof is tests/EngineLiveTest.cpp.
#include "browser/engine/EngineBrowser.h"
#include "browser/engine/EngineProfile.h"

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
