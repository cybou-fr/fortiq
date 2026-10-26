// Live check of the Browser's SSH gateway (docs/BROWSER.md, "Passerelle SSH") against the disposable fixture: the REAL SshTunnel
// (libssh2, direct-tcpip channels, pinned host key) behind the ProxyGateway, driven by curl.exe as a stand-in for the engine.
// Skipped unless FORTIQ_IT_HOST/PORT/USER/FINGERPRINT and (FORTIQ_IT_KEY or FORTIQ_IT_PASSWORD) are set.
// Needs FORTIQ_IT_WEB_BASE + FORTIQ_IT_POLICY (tests/fixtures/web_fixture.py on 0.0.0.0:8099 and the loopback aliases of
// tool/setup_browser_policy_fixture.sh), FORTIQ_IT_REBIND (tool/setup_dns_rebind_fixture.sh), FORTIQ_IT_GATEWAY_TLS
// (tool/setup_gateway_fixture.sh) and the second host FORTIQ_IT_*_B; each missing piece SKIPS only its own test (and the live run
// fails on an unwaived skip). Secrets come from the environment and are never printed.
#include "browser/gateway/ProxyGateway.h"
#include "browser/gateway/SshTunnel.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <iphlpapi.h>
#include <windows.h>
#endif

using namespace fortiq;
using namespace fortiq::gateway;

namespace {
struct Curl {
    int exitCode = -1;
    QByteArray out;
    QByteArray err;
    int http = 0;       // %{http_code}
    int connectCode = 0; // %{http_connect}
    QByteArray body() const { const qsizetype i = out.lastIndexOf("\n@@"); return i < 0 ? out : out.left(i); }
};

QString env(const char *name) { return qEnvironmentVariable(name); }
}  // namespace

class GatewayLiveTest : public QObject {
    Q_OBJECT

    ConnectionRequest reqA, reqB;
    bool haveB = false;
    QString curlExe;
    std::unique_ptr<SshTunnel> tunnel;
    std::unique_ptr<ProxyGateway> gateway;
    int asked = 0;
    bool answer = false;
    QTemporaryDir tmp;
    QString caFile;

    static ConnectionRequest request(const char *h, const char *p, const char *u, const char *fp) {
        ConnectionRequest r;
        r.host = env(h);
        r.port = env(p).toInt();
        r.user = env(u);
        r.expectedHostFingerprint = env(fp);
        if (!env("FORTIQ_IT_KEY").isEmpty()) r.keyPath = env("FORTIQ_IT_KEY");
        else r.password = env("FORTIQ_IT_PASSWORD");
        return r;
    }

    void startGateway(TunnelBackend *t, std::unique_ptr<ProxyGateway> *slot, const GatewayOptions &o = {}) {
        *slot = std::make_unique<ProxyGateway>(t);
        QVERIFY(slot->get()->start(o));
        slot->get()->setConfirmHandler([this](const InternalAccessRequest &, std::function<void(bool)> reply) { ++asked; reply(answer); });
    }
    void freshGateway(const GatewayOptions &o = {}) {
        gateway.reset();
        startGateway(tunnel.get(), &gateway, o);
        asked = 0;
        answer = false;
    }

    // curl as the engine: proxy credentials come through a config on stdin (never on the command line).
    Curl curl(ProxyGateway *g, const QStringList &args, int timeoutMs = 30000, const QString &token = {}) {
        Curl r;
        QProcess p;
        QStringList a = { "--silent", "--show-error", "--noproxy", "", "--proxy", QStringLiteral("http://127.0.0.1:%1").arg(g->port()),
                          "--proxy-basic", "-K", "-", "--max-time", QString::number(timeoutMs / 1000), "--ssl-no-revoke",
                          "-w", "\n@@%{http_code} %{http_connect}\n" };
        a += args;
        p.start(curlExe, a);
        if (!p.waitForStarted(5000)) return r;
        p.write(QStringLiteral("proxy-user = \"fortiq:%1\"\n").arg(token.isEmpty() ? g->token() : token).toUtf8());
        p.closeWriteChannel();
        QElapsedTimer t;
        t.start();
        while (p.state() != QProcess::NotRunning && t.elapsed() < timeoutMs + 5000) QTest::qWait(10);   // the gateway lives in THIS thread
        if (p.state() != QProcess::NotRunning) p.kill();
        r.exitCode = p.exitCode();
        r.out = p.readAllStandardOutput();
        r.err = p.readAllStandardError();
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("@@(\\d+) (\\d+)\\s*$")).match(QString::fromLatin1(r.out));
        if (m.hasMatch()) { r.http = m.captured(1).toInt(); r.connectCode = m.captured(2).toInt(); }
        return r;
    }
    Curl curl(const QStringList &args, int timeoutMs = 30000) { return curl(gateway.get(), args, timeoutMs); }

    // A raw tunnel through the gateway to host:port (a CONNECT): returns a connected socket after the 200, or null.
    std::unique_ptr<QTcpSocket> rawTunnel(ProxyGateway *g, const QString &target, QByteArray *status = nullptr) {
        auto s = std::make_unique<QTcpSocket>();
        s->connectToHost(QHostAddress::LocalHost, g->port());
        if (!s->waitForConnected(3000)) return nullptr;
        const QByteArray auth = basicAuthValue("fortiq", g->token().toLatin1());
        s->write("CONNECT " + target.toLatin1() + " HTTP/1.1\r\nHost: " + target.toLatin1() + "\r\nProxy-Authorization: " + auth + "\r\n\r\n");
        QByteArray got;
        QElapsedTimer t;
        t.start();
        while (!got.contains("\r\n\r\n") && t.elapsed() < 20000) {
            if (s->bytesAvailable() == 0) QTest::qWait(5);
            got += s->readAll();
            if (s->state() != QAbstractSocket::ConnectedState && s->bytesAvailable() == 0) break;
        }
        if (status) *status = got.left(got.indexOf("\r\n"));
        if (!got.startsWith("HTTP/1.1 200")) return nullptr;
        return s;
    }

    // Exec through the tunnel (the same facility that resolves names), synchronous for the test.
    QByteArray execOut(TunnelBackend *t, const QByteArray &script, bool *ok = nullptr, int timeoutMs = 20000) {
        QSignalSpy spy(t, &TunnelBackend::execFinished);
        const quint64 id = t->exec(script, 65536);
        QElapsedTimer tm;
        tm.start();
        while (tm.elapsed() < timeoutMs) {
            for (const auto &row : spy)
                if (row.at(0).toULongLong() == id) { if (ok) *ok = row.at(1).toBool(); return row.at(2).toByteArray(); }
            spy.wait(50);
        }
        if (ok) *ok = false;
        return {};
    }

    static QStringList ownTcpAnomalies(quint16 gatewayPort, const QList<quint16> &sshPorts, QStringList *all = nullptr) {
        QStringList bad;
#ifdef Q_OS_WIN
        ULONG size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        QByteArray buf(static_cast<qsizetype>(size), 0);
        if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) { bad << QStringLiteral("GetExtendedTcpTable failed"); return bad; }
        const auto *table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(buf.constData());
        const DWORD me = GetCurrentProcessId();
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_TCPROW_OWNER_PID &row = table->table[i];
            if (row.dwOwningPid != me) continue;
            const quint16 lport = ntohs(static_cast<u_short>(row.dwLocalPort));
            const quint16 rport = ntohs(static_cast<u_short>(row.dwRemotePort));
            const QString line = QStringLiteral("state %1 local %2 remote %3.%4.%5.%6:%7").arg(row.dwState).arg(lport)
                .arg(row.dwRemoteAddr & 0xff).arg((row.dwRemoteAddr >> 8) & 0xff).arg((row.dwRemoteAddr >> 16) & 0xff).arg((row.dwRemoteAddr >> 24) & 0xff).arg(rport);
            if (all) *all << line;
            if (row.dwState == MIB_TCP_STATE_LISTEN) { if (lport != gatewayPort) bad << line; continue; }
            if (lport == gatewayPort || rport == gatewayPort) continue;        // the local side of the gateway (accepted / test clients)
            if (sshPorts.contains(rport)) continue;                            // the SSH connection(s) to the chosen server(s)
            if (row.dwState == MIB_TCP_STATE_TIME_WAIT || row.dwState == MIB_TCP_STATE_CLOSED) continue;
            bad << line;
        }
#else
        Q_UNUSED(gatewayPort); Q_UNUSED(sshPorts); Q_UNUSED(all);
#endif
        return bad;
    }

private slots:
    void initTestCase() {
        if (env("FORTIQ_IT_HOST").isEmpty() || env("FORTIQ_IT_FINGERPRINT").isEmpty()) QSKIP("FORTIQ_IT_* not set");
        if (env("FORTIQ_IT_KEY").isEmpty() && env("FORTIQ_IT_PASSWORD").isEmpty()) QSKIP("FORTIQ_IT_KEY or FORTIQ_IT_PASSWORD needed");
        reqA = request("FORTIQ_IT_HOST", "FORTIQ_IT_PORT", "FORTIQ_IT_USER", "FORTIQ_IT_FINGERPRINT");
        if (!env("FORTIQ_IT_PORT_B").isEmpty() && !env("FORTIQ_IT_USER_B").isEmpty() && !env("FORTIQ_IT_FINGERPRINT_B").isEmpty()) {
            reqB = request("FORTIQ_IT_HOST", "FORTIQ_IT_PORT_B", "FORTIQ_IT_USER_B", "FORTIQ_IT_FINGERPRINT_B");
            if (!env("FORTIQ_IT_HOST_B").isEmpty()) reqB.host = env("FORTIQ_IT_HOST_B");
            haveB = true;
        }
        curlExe = QStandardPaths::findExecutable(QStringLiteral("curl"));
        QVERIFY2(!curlExe.isEmpty(), "curl is needed (Windows 10+ ships curl.exe)");
        QVERIFY(tmp.isValid());
        tunnel = std::make_unique<SshTunnel>(reqA);
        startGateway(tunnel.get(), &gateway);
    }
    void cleanupTestCase() {
        gateway.reset();
        tunnel.reset();
    }

    void tunnelResolvesAndIdentifiesTheServer() {
        bool ok = false;
        const QByteArray out = execOut(tunnel.get(), "id -un; getent ahosts public-fixture.test | head -n 1 | awk '{print $1}'", &ok);
        QVERIFY2(ok, "exec through the tunnel");
        QVERIFY2(out.startsWith(reqA.user.toUtf8() + "\n"), qPrintable(QString::fromUtf8(out)));
        QCOMPARE(tunnel->state(), TunnelBackend::State::Connected);
        QCOMPARE(tunnel->sessionsEstablished(), 1);
    }

    void httpPageThroughTheTunnel() {
        if (env("FORTIQ_IT_WEB_BASE").isEmpty() || env("FORTIQ_IT_POLICY").isEmpty()) QSKIP("FORTIQ_IT_WEB_BASE and FORTIQ_IT_POLICY are needed");
        freshGateway();
        const Curl r = curl({ "http://public-fixture.test:8099/page" });
        QVERIFY2(r.exitCode == 0, r.err.constData());
        QCOMPARE(r.http, 200);
        QVERIFY(r.body().contains("Hello fixture"));
        QCOMPARE(asked, 0);   // a public address needs no question
        // the same page, fetched from the server's point of view, came from the public alias
        const Curl echo = curl({ "http://public-fixture.test:8099/echo" });
        QCOMPARE(echo.http, 200);
        QVERIFY2(echo.body().contains("local=93.184.216.34"), echo.body().constData());
        QVERIFY2(echo.body().contains("host=public-fixture.test:8099"), echo.body().constData());
    }

    void keepAliveAndSeveralUrlsInOneCurl() {
        if (env("FORTIQ_IT_WEB_BASE").isEmpty() || env("FORTIQ_IT_POLICY").isEmpty()) QSKIP("FORTIQ_IT_WEB_BASE and FORTIQ_IT_POLICY are needed");
        freshGateway();
        const Curl r = curl({ "-w", "[%{num_connects}]", "http://public-fixture.test:8099/page2", "http://public-fixture.test:8099/json", "http://public-fixture.test:8099/redir", "-L" });
        QVERIFY2(r.exitCode == 0, r.err.constData());
        QVERIFY(r.out.contains("Second page"));
        QVERIFY(r.out.contains("\"fortiq\""));
        QVERIFY(r.out.contains("Hello fixture"));
        QVERIFY2(r.out.contains("[0]"), "the second transfer must reuse the connection to the gateway (keep-alive)");
    }

    void httpsThroughTheTunnelIsEndToEnd() {
        if (env("FORTIQ_IT_GATEWAY_TLS").isEmpty() || env("FORTIQ_IT_POLICY").isEmpty()) QSKIP("FORTIQ_IT_GATEWAY_TLS (tool/setup_gateway_fixture.sh) and FORTIQ_IT_POLICY are needed");
        freshGateway();
        bool ok = false;
        const QByteArray cert = execOut(tunnel.get(), "cat /tmp/fortiq-gw-tls/cert.pem", &ok);
        QVERIFY2(ok && cert.contains("BEGIN CERTIFICATE"), "certificate fetched through an exec channel");
        caFile = tmp.filePath(QStringLiteral("ca.pem"));
        QFile f(caFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(cert);
        f.close();
        const Curl r = curl({ "--cacert", caFile, "https://public-fixture.test:8443/" });
        QVERIFY2(r.exitCode == 0, r.err.constData());
        QCOMPARE(r.http, 200);
        QCOMPARE(r.connectCode, 200);   // CONNECT answered by the gateway
        QCOMPARE(r.body(), QByteArray("tls ok"));
        // The server name the TLS server saw proves the handshake was with the real server through the tunnel.
        const Curl sni = curl({ "--cacert", caFile, "https://public-fixture.test:8443/sni" });
        QVERIFY2(sni.body().contains("sni=public-fixture.test"), sni.body().constData());
        // A certificate the client does not trust must fail: the gateway does not terminate TLS (no interception).
        const Curl untrusted = curl({ "https://public-fixture.test:8443/" });
        QVERIFY2(untrusted.exitCode != 0 && untrusted.http == 0, "self-signed certificate must be refused by the client itself");
        const Curl big = curl({ "--cacert", caFile, "-o", tmp.filePath(QStringLiteral("big.bin")), "https://public-fixture.test:8443/big" }, 60000);
        QCOMPARE(big.exitCode, 0);
        QCOMPARE(QFileInfo(tmp.filePath(QStringLiteral("big.bin"))).size(), qint64(4 * 1024 * 1024));
    }

    void internalAddressNeedsConfirmation() {
        if (env("FORTIQ_IT_WEB_BASE").isEmpty() || env("FORTIQ_IT_POLICY").isEmpty()) QSKIP("FORTIQ_IT_WEB_BASE and FORTIQ_IT_POLICY are needed");
        freshGateway();
        const Curl hitsBefore = curl({ "http://public-fixture.test:8099/_hits" });
        const QJsonObject before = QJsonDocument::fromJson(hitsBefore.body()).object();
        QVERIFY(!before.contains("/echo") || before.value("/echo").toInt() >= 0);
        answer = false;
        const Curl denied = curl({ "http://127.0.0.1:8099/echo" });
        QCOMPARE(denied.http, 403);
        QCOMPARE(asked, 1);
        const Curl hitsAfter = curl({ "http://public-fixture.test:8099/_hits" });
        QCOMPARE(QJsonDocument::fromJson(hitsAfter.body()).object().value("/echo").toInt(), before.value("/echo").toInt());   // nothing reached the server
        const Curl deniedName = curl({ "http://internal-fixture.test:8099/echo" });
        QCOMPARE(deniedName.http, 403);
        answer = true;
        const Curl allowed = curl({ "http://127.0.0.1:8099/echo" });
        QCOMPARE(allowed.http, 200);
        QVERIFY2(allowed.body().contains("local=127.0.0.1"), allowed.body().constData());   // it really came from the server's loopback
        const int askedNow = asked;
        const Curl again = curl({ "http://127.0.0.1:8099/echo" });
        QCOMPARE(again.http, 200);
        QCOMPARE(asked, askedNow);   // asked once for this host:port
        const Curl mixed = curl({ "http://mixed-fixture.test:8099/echo" });   // public + loopback behind one name
        QCOMPARE(mixed.http, 200);
        QVERIFY2(mixed.body().contains("local=127.0.0.1"), "the pinned address of a mixed name is the INTERNAL one the user confirmed");
        QVERIFY(asked > askedNow);
        const Curl privateNet = curl({ "http://private-fixture.test:8099/echo" }, 15000);   // 10.255.255.1: confirmed, then unreachable
        QVERIFY2(privateNet.http == 502 || privateNet.http == 504 || privateNet.exitCode != 0, "confirmed but not reachable: an honest error");
    }

    void cloudMetadataIsAlwaysRefused() {
        if (env("FORTIQ_IT_POLICY").isEmpty()) QSKIP("FORTIQ_IT_POLICY is needed");
        freshGateway();
        answer = true;   // even a user who says yes cannot open it
        for (const QString &url : { QStringLiteral("http://169.254.169.254/latest/meta-data/"), QStringLiteral("http://meta-fixture.test/latest/meta-data/"),
                                    QStringLiteral("http://metadata.google.internal/"), QStringLiteral("http://[fd00:ec2::254]/"),
                                    QStringLiteral("http://2852039166/"), QStringLiteral("http://0xa9fea9fe/") }) {
            const Curl r = curl({ url }, 15000);
            QVERIFY2(r.http == 403 || r.exitCode != 0, qPrintable(url));
            QVERIFY2(r.http != 200, qPrintable(url));
        }
        QCOMPARE(asked, 0);
        const Curl https = curl({ "https://169.254.169.254/" }, 15000);
        QVERIFY2(https.connectCode == 403, "CONNECT to the metadata address is refused by the gateway");
    }

    void dnsRebindingNeverReachesTheLoopback() {
        if (env("FORTIQ_IT_WEB_BASE").isEmpty() || env("FORTIQ_IT_REBIND").isEmpty() || env("FORTIQ_IT_POLICY").isEmpty())
            QSKIP("FORTIQ_IT_WEB_BASE, FORTIQ_IT_POLICY and FORTIQ_IT_REBIND (tool/setup_dns_rebind_fixture.sh) are needed");
        GatewayOptions o;
        o.resolveCacheMs = 0;   // every connection resolves again, like an attacker's short-TTL DNS
        freshGateway(o);
        int ok = 0, refused = 0, bad = 0;
        for (int i = 0; i < 60; ++i) {
            const Curl r = curl({ "http://flip-fixture.test:8099/echo" }, 15000);
            if (r.http == 200) {
                if (r.body().contains("local=93.184.216.34")) ++ok;
                else ++bad;   // reached 127.0.0.1 without a "yes": the defence failed
            } else if (r.http == 403) ++refused;
        }
        QCOMPARE(bad, 0);
        QVERIFY2(ok > 0, "the public answers must still work");
        QVERIFY2(refused + asked > 0, "the internal answers must have been caught");
        qInfo() << "rebinding: public fetches" << ok << "internal refused" << refused << "asked" << asked;
    }

    void hundredParallelConnectionsAndBigTransfer() {
        if (env("FORTIQ_IT_WEB_BASE").isEmpty() || env("FORTIQ_IT_POLICY").isEmpty()) QSKIP("FORTIQ_IT_WEB_BASE and FORTIQ_IT_POLICY are needed");
        freshGateway();
        constexpr int N = 100;
        struct Sock { std::unique_ptr<QTcpSocket> s; QByteArray got; };
        std::vector<Sock> socks(N);
        const QByteArray auth = basicAuthValue("fortiq", gateway->token().toLatin1());
        for (int i = 0; i < N; ++i) {
            socks[i].s = std::make_unique<QTcpSocket>();
            QTcpSocket *s = socks[i].s.get();
            QByteArray *got = &socks[i].got;
            connect(s, &QTcpSocket::connected, s, [s, auth] {
                s->write("CONNECT 93.184.216.34:8099 HTTP/1.1\r\nProxy-Authorization: " + auth + "\r\n\r\n");
            });
            connect(s, &QTcpSocket::readyRead, s, [s, got] {
                const bool had200 = got->contains("\r\n\r\n");
                *got += s->readAll();
                if (!had200 && got->contains("\r\n\r\n")) s->write("GET /echo HTTP/1.0\r\nHost: 93.184.216.34\r\n\r\n");   // after the 200 of the gateway
            });
            s->connectToHost(QHostAddress::LocalHost, gateway->port());
        }
        QElapsedTimer t;
        t.start();
        QTRY_VERIFY_WITH_TIMEOUT(std::all_of(socks.begin(), socks.end(), [](const Sock &x) { return x.got.contains("local=93.184.216.34"); }), 60000);
        qInfo() << "100 parallel tunnels answered in" << t.elapsed() << "ms; channels opened" << gateway->stats().channelsOpened;
        QVERIFY(gateway->stats().channelsOpened >= N);
        socks.clear();
        QTRY_COMPARE_WITH_TIMEOUT(gateway->stats().active, 0, 10000);
        // big transfer through the tunnel with a reader that is slower than the network for a while
        const Curl big = curl({ "-o", tmp.filePath(QStringLiteral("big6.bin")), "http://public-fixture.test:8099/big" }, 90000);
        QCOMPARE(big.exitCode, 0);
        QCOMPARE(QFileInfo(tmp.filePath(QStringLiteral("big6.bin"))).size(), qint64(6 * 1024 * 1024));
    }

    void tunnelLostDuringATransferThenReconnects() {
        if (env("FORTIQ_IT_WEB_BASE").isEmpty() || env("FORTIQ_IT_POLICY").isEmpty()) QSKIP("FORTIQ_IT_WEB_BASE and FORTIQ_IT_POLICY are needed");
        freshGateway();
        QSignalSpy states(tunnel.get(), &TunnelBackend::stateChanged);
        // 1. a transfer that cannot finish: the client does not read, so the 6 MiB stay on the server side
        auto s = rawTunnel(gateway.get(), QStringLiteral("93.184.216.34:8099"));
        QVERIFY(s);
        const int sessionsBefore = tunnel->sessionsEstablished();
        QVERIFY(sessionsBefore >= 1);
        s->setReadBufferSize(16 * 1024);
        s->write("GET /big HTTP/1.0\r\nHost: 93.184.216.34\r\n\r\n");
        QTest::qWait(1500);
        QVERIFY(s->state() == QAbstractSocket::ConnectedState);
        QVERIFY2(gateway->stats().bytesFromServer < 6 * 1024 * 1024, "backpressure: the whole transfer must not have been pulled into memory");
        // 2. the SERVER kills the SSH connection (the sshd process that serves it), as a real outage would
        bool ok = true;
        QElapsedTimer killTimer;
        killTimer.start();
        execOut(tunnel.get(), "p=$PPID; while [ \"$p\" -gt 1 ]; do case \"$(cat /proc/$p/comm)\" in sshd*) kill -9 $p; exit 0;; esac; p=$(awk '{print $4}' /proc/$p/stat); done; exit 9", &ok, 20000);
        qInfo() << "the exec channel ended after" << killTimer.elapsed() << "ms";
        QVERIFY2(!ok, "the exec channel itself dies with the connection");
        qint64 received = 0;
        QElapsedTimer cut;
        cut.start();
        while (s->state() == QAbstractSocket::ConnectedState && cut.elapsed() < 15000) {   // the client now reads on: it must hit a broken connection
            received += s->readAll().size();
            QTest::qWait(5);
        }
        received += s->readAll().size();
        qInfo() << "client saw the connection break after" << cut.elapsed() << "ms,"  << received << "bytes of 6291456 received";
        QVERIFY2(s->state() != QAbstractSocket::ConnectedState, "the client must see a broken connection, not a short but valid answer");
        QVERIFY(received < 6 * 1024 * 1024);
        QTRY_COMPARE_WITH_TIMEOUT(gateway->stats().active, 0, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(tunnel->state(), TunnelBackend::State::Disconnected, 5000);
        bool sawLoss = false;
        for (const auto &row : states) if (row.at(0).value<TunnelBackend::State>() == TunnelBackend::State::Disconnected && !row.at(1).toString().isEmpty()) sawLoss = true;
        QVERIFY2(sawLoss, "a Disconnected state with a reason is announced");
        // 3. the next request reconnects (host key pinned again) and works
        const Curl again = curl({ "http://public-fixture.test:8099/page" }, 40000);
        QVERIFY2(again.http == 200 && again.body().contains("Hello fixture"), again.err.constData());
        QCOMPARE(tunnel->sessionsEstablished(), sessionsBefore + 1);
        QCOMPARE(tunnel->state(), TunnelBackend::State::Connected);
    }

    void cableCutHookBreaksEveryChannel() {
        if (env("FORTIQ_IT_WEB_BASE").isEmpty() || env("FORTIQ_IT_POLICY").isEmpty()) QSKIP("FORTIQ_IT_WEB_BASE and FORTIQ_IT_POLICY are needed");
        freshGateway();
        std::vector<std::unique_ptr<QTcpSocket>> held;
        for (int i = 0; i < 5; ++i) { held.push_back(rawTunnel(gateway.get(), QStringLiteral("93.184.216.34:8099"))); QVERIFY(held.back()); }
        tunnel->simulateTransportLoss();
        for (auto &s : held) QTRY_VERIFY_WITH_TIMEOUT(s->state() != QAbstractSocket::ConnectedState, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(gateway->stats().active, 0, 5000);
        const Curl again = curl({ "http://public-fixture.test:8099/page2" }, 40000);   // a session that died idle is replaced transparently
        QCOMPARE(again.http, 200);
    }

    void wrongPinFailsClosedAndDoesNotRedial() {
        SshTunnel bad([&] { ConnectionRequest r = reqA; r.expectedHostFingerprint = QStringLiteral("SHA256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"); return r; }());
        std::unique_ptr<ProxyGateway> g;
        startGateway(&bad, &g);
        const Curl r = curl(g.get(), { "http://public-fixture.test:8099/page" }, 20000);
        QCOMPARE(r.http, 502);
        QVERIFY2(r.body().contains("HOST KEY CHANGED"), r.body().constData());
        QCOMPARE(bad.sessionsEstablished(), 0);
        QCOMPARE(bad.connectCalls(), 1);
        const Curl second = curl(g.get(), { "http://public-fixture.test:8099/page" }, 20000);
        QCOMPARE(second.http, 502);
        QCOMPARE(bad.connectCalls(), 1);   // the failure is remembered for a moment: no hammering of the server
        const Curl tls = curl(g.get(), { "https://public-fixture.test:8443/" }, 20000);
        QCOMPARE(tls.connectCode, 502);
    }

    void unpinnedRequestIsRefusedBeforeConnecting() {
        ConnectionRequest r = reqA;
        r.expectedHostFingerprint.clear();
        SshTunnel t(r);
        std::unique_ptr<ProxyGateway> g;
        startGateway(&t, &g);
        const Curl c = curl(g.get(), { "http://public-fixture.test:8099/page" }, 20000);
        QCOMPARE(c.http, 502);
        QCOMPARE(t.connectCalls(), 0);   // a server the user has not verified is never dialled by the Browser
    }

    void tokenOfAnotherGatewayIsRejected() {
        if (env("FORTIQ_IT_POLICY").isEmpty()) QSKIP("FORTIQ_IT_POLICY is needed");
        freshGateway();
        std::unique_ptr<ProxyGateway> other;
        startGateway(tunnel.get(), &other);
        QVERIFY(other->token() != gateway->token() && other->port() != gateway->port());
        const Curl r = curl(gateway.get(), { "http://public-fixture.test:8099/page" }, 15000, other->token());
        QCOMPARE(r.http, 407);
        const Curl none = [&] {   // no credential at all
            Curl x;
            QTcpSocket s;
            s.connectToHost(QHostAddress::LocalHost, gateway->port());
            s.waitForConnected(3000);
            s.write("GET http://public-fixture.test:8099/page HTTP/1.1\r\nHost: public-fixture.test\r\n\r\n");
            QByteArray got;
            for (QElapsedTimer t; t.elapsed() < 5000 && !got.contains("\r\n\r\n");) { QTest::qWait(10); got += s.readAll(); }
            x.http = got.mid(9, 3).toInt();
            return x;
        }();
        QCOMPARE(none.http, 407);
    }

    void secondServerNeverMixesWithTheFirst() {
        if (!haveB) QSKIP("second fixture not configured (FORTIQ_IT_PORT_B/USER_B/FINGERPRINT_B)");
        if (env("FORTIQ_IT_POLICY").isEmpty()) QSKIP("FORTIQ_IT_POLICY is needed");
        SshTunnel tb(reqB);
        std::unique_ptr<ProxyGateway> gb;
        startGateway(&tb, &gb);
        freshGateway();
        // each tunnel is its own SSH session as its own user
        bool okA = false, okB = false;
        const QByteArray ida = execOut(tunnel.get(), "id -un", &okA);
        const QByteArray idb = execOut(&tb, "id -un", &okB);
        QVERIFY(okA && okB);
        QCOMPARE(ida.trimmed(), reqA.user.toUtf8());
        QCOMPARE(idb.trimmed(), reqB.user.toUtf8());
        QVERIFY(ida != idb);
        // credentials and ports are per gateway
        QVERIFY(gb->token() != gateway->token() && gb->port() != gateway->port());
        const Curl cross = curl(gb.get(), { "http://public-fixture.test:8099/page" }, 15000, gateway->token());
        QCOMPARE(cross.http, 407);
        // a confirmation given on A is not valid on B, and the other way round
        answer = true;
        const Curl a1 = curl(gateway.get(), { "http://127.0.0.1:8099/echo" });
        QCOMPARE(a1.http, 200);
        QCOMPARE(asked, 1);
        const Curl b1 = curl(gb.get(), { "http://127.0.0.1:8099/echo" });
        QCOMPARE(b1.http, 200);
        QCOMPARE(asked, 2);   // B asked on its own
        // the pin of A does not open B
        ConnectionRequest crossPin = reqB;
        crossPin.expectedHostFingerprint = reqA.expectedHostFingerprint;
        SshTunnel mixed(crossPin);
        std::unique_ptr<ProxyGateway> gm;
        startGateway(&mixed, &gm);
        const Curl m = curl(gm.get(), { "http://public-fixture.test:8099/page" }, 20000);
        QCOMPARE(m.http, 502);
        QVERIFY(m.body().contains("HOST KEY CHANGED"));
        QCOMPARE(mixed.sessionsEstablished(), 0);
    }

    void gatewayMadeNoOtherOutgoingConnection() {
        // Everything above ran in this process: its TCP table may only hold SSH sessions to the chosen server(s) and the local gateway
        // sockets. A page fetched directly (not through SSH) would show up as a connection to 127.0.0.1:8099 / 93.184.216.34.
        freshGateway();
        std::vector<std::unique_ptr<QTcpSocket>> held;
        if (!env("FORTIQ_IT_POLICY").isEmpty()) for (int i = 0; i < 3; ++i) held.push_back(rawTunnel(gateway.get(), QStringLiteral("93.184.216.34:8099")));
        QList<quint16> ssh = { static_cast<quint16>(reqA.port) };
        if (haveB) ssh << static_cast<quint16>(reqB.port);
        QStringList all;
        const QStringList bad = ownTcpAnomalies(gateway->port(), ssh, &all);
        qInfo().noquote() << "TCP rows of this process:\n" << all.join('\n');
        QVERIFY2(bad.isEmpty(), qPrintable(bad.join('\n')));
        // Positive control: the same check DOES see a direct connection (so an empty result above means something).
#ifdef Q_OS_WIN
        QTcpSocket direct;
        direct.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(reqA.port == 8099 ? 2222 : 8099));   // any port that is not the SSH one
        if (direct.waitForConnected(3000)) {
            QVERIFY2(!ownTcpAnomalies(gateway->port(), ssh).isEmpty(), "a direct connection must be reported by the check");
            direct.abort();
        }
#endif
        // the tunnel dialled the server once per session, nothing else
        QCOMPARE(tunnel->connectCalls(), tunnel->sessionsEstablished());
    }
};

QTEST_MAIN(GatewayLiveTest)
#include "GatewayLiveTest.moc"
