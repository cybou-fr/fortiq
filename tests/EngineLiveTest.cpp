// Live test of the Browser engine (docs/BROWSER.md, "Moteur" and "Test d'absence de fuite"): the REAL Qt WebEngine (Chromium) with the REAL
// view/profile of the application (qml/engine/EngineView.qml), the REAL gateway and SSH tunnel (libssh2, pinned host key), against the
// disposable fixture (tool/prepare_live_fixture.py). It proves, on a page that runs JavaScript, that EVERY network access of the engine
// goes through the gateway:
//   - javascriptRunsThroughTheGateway: a page whose text exists only after JavaScript ran, fetched through the tunnel (gateway counters);
//   - noDirectConnectionsWhateverThePageTries: a canary page tries fetch/XHR/WebSocket/EventSource/WebTransport/WebRTC (STUN)/sendBeacon/
//     <img>/<script>/<iframe>/CSS/dns-prefetch/preconnect/prefetch/prerender/ping/geolocation/localhost of this computer ... while this test
//     samples the socket tables of the whole process tree (GetExtendedTcpTable / GetExtendedUdpTable every 15 ms): the only connections
//     tolerated are loopback -> the gateway port and the SSH connection of the tunnel; a listener on this computer must see nothing.
// Skipped unless FORTIQ_IT_HOST/PORT/USER/FINGERPRINT, (FORTIQ_IT_KEY or FORTIQ_IT_PASSWORD) and FORTIQ_IT_WEB_BASE are set
// (tool/run_live_tests.py sets them). Secrets come from the environment and are never printed.
#include "browser/engine/EngineBrowser.h"
#include "browser/engine/EngineProfile.h"
#include "browser/gateway/SshTunnel.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <QtWebEngineQuick/QtWebEngineQuick>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <windows.h>
#endif

Q_IMPORT_QML_PLUGIN(Fortiq_EnginePlugin)
Q_IMPORT_QML_PLUGIN(Fortiq_ShellPlugin)

using namespace fortiq;
using namespace fortiq::engine;

namespace {

QString env(const char *name) { return qEnvironmentVariable(name); }

#ifdef Q_OS_WIN
// ---------------------------------------------------------------------------------------------------------------------------------
// Samples the TCP and UDP tables of this process and ALL its descendants (QtWebEngineProcess: renderer, GPU, utility...).
class SocketWatch {
public:
    struct Allowed {
        quint16 gatewayPort = 0;
        quint16 sshPort = 0;
        QString sshHost;     // the address of the SSH server as written (an IP literal for the fixture)
    };
    explicit SocketWatch(const Allowed &allowed) : m_allowed(allowed) {}
    ~SocketWatch() { stop(); }
    void start() {
        m_run = true;
        m_thread = std::thread([this] { loop(); });
    }
    void stop() {
        m_run = false;
        if (m_thread.joinable()) m_thread.join();
    }
    QStringList violations() const { std::lock_guard<std::mutex> l(m_lock); return m_violations; }
    // UDP sockets that exist (bound, not connected to anything: the tables show no remote). A bound socket is not a connection, but a page
    // that needs none must not cause any, so the canary test asserts there are none and the WebRTC-gap test measures what they carry.
    QStringList udpBound() const { std::lock_guard<std::mutex> l(m_lock); return m_udp; }
    QStringList tolerated() const { std::lock_guard<std::mutex> l(m_lock); return QStringList(m_tolerated.begin(), m_tolerated.end()); }
    int polls() const { return m_polls; }
    int maxProcesses() const { return m_maxProcesses; }

private:
    static quint16 port(DWORD networkOrder) { return quint16(ntohs(u_short(networkOrder & 0xFFFF))); }
    static QString v4(DWORD a) {
        const quint8 *b = reinterpret_cast<const quint8 *>(&a);
        return QStringLiteral("%1.%2.%3.%4").arg(b[0]).arg(b[1]).arg(b[2]).arg(b[3]);
    }
    static QString v6(const UCHAR *a) {
        QStringList g;
        for (int i = 0; i < 16; i += 2) g << QString::number((a[i] << 8) | a[i + 1], 16);
        return g.join(QLatin1Char(':'));
    }
    static bool loopback(const QString &a) { return a == QLatin1String("127.0.0.1") || a == QLatin1String("0:0:0:0:0:0:0:1"); }

    QHash<DWORD, QString> descendants() {
        QHash<DWORD, DWORD> parent;
        QHash<DWORD, QString> name;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W e;
            e.dwSize = sizeof(e);
            for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) {
                parent[e.th32ProcessID] = e.th32ParentProcessID;
                name[e.th32ProcessID] = QString::fromWCharArray(e.szExeFile);
            }
            CloseHandle(snap);
        }
        const DWORD self = GetCurrentProcessId();
        QHash<DWORD, QString> tree;
        tree[self] = QStringLiteral("main");
        bool grew = true;
        while (grew) {
            grew = false;
            for (auto it = parent.begin(); it != parent.end(); ++it)
                if (!tree.contains(it.key()) && tree.contains(it.value()) && it.key() != it.value()) { tree[it.key()] = name.value(it.key()); grew = true; }
        }
        return tree;
    }
    void reportUdp(const QString &text) {
        std::lock_guard<std::mutex> l(m_lock);
        if (!m_udp.contains(text)) m_udp << text;
    }
    void report(const QString &text, bool violation) {
        std::lock_guard<std::mutex> l(m_lock);
        if (violation) { if (!m_violations.contains(text)) m_violations << text; }
        else m_tolerated.insert(text);
    }
    void judgeTcp(const QString &proc, const QString &la, quint16 lp, const QString &ra, quint16 rp, DWORD state) {
        if (state == MIB_TCP_STATE_LISTEN || (rp == 0 && (ra == QLatin1String("0.0.0.0") || ra.startsWith(QLatin1String("0:0:0"))))) return;
        const QString line = QStringLiteral("%1 tcp %2:%3 -> %4:%5 (state %6)").arg(proc, la).arg(lp).arg(ra).arg(rp).arg(state);
        if (la == QLatin1String("127.0.0.1") && lp == m_allowed.gatewayPort) { report(QStringLiteral("%1 serves the gateway (accepted socket)").arg(proc), false); return; }
        if (ra == QLatin1String("127.0.0.1") && rp == m_allowed.gatewayPort) { report(QStringLiteral("%1 -> gateway 127.0.0.1:gateway").arg(proc), false); return; }
        if (rp == m_allowed.sshPort && (ra == m_allowed.sshHost || (m_allowed.sshHost == QLatin1String("localhost") && ra == QLatin1String("127.0.0.1")))) {
            if (proc == QLatin1String("main")) { report(QStringLiteral("main -> SSH server (the tunnel)"), false); return; }
            report(line + QStringLiteral("  <- a child process talks to the SSH server"), true);
            return;
        }
        if (lp == m_allowed.sshPort && la == m_allowed.sshHost) { report(QStringLiteral("SSH server side of the tunnel (fixture on this machine)"), false); return; }   // sshd of WSL relayed on the loopback
        report(line, true);
    }
    void loop() {
        while (m_run) {
            const QHash<DWORD, QString> tree = descendants();
            m_maxProcesses = std::max<int>(m_maxProcesses, int(tree.size()));
            std::vector<char> buf;
            auto fetch = [&](ULONG family, bool tcp) -> bool {
                for (int attempt = 0; attempt < 4; ++attempt) {
                    DWORD size = DWORD(buf.size());
                    const DWORD rc = tcp ? GetExtendedTcpTable(buf.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0)
                                         : GetExtendedUdpTable(buf.data(), &size, FALSE, family, UDP_TABLE_OWNER_PID, 0);
                    if (rc == NO_ERROR) return true;
                    if (rc != ERROR_INSUFFICIENT_BUFFER) return false;
                    buf.resize(size + 4096);
                }
                return false;
            };
            if (fetch(AF_INET, true)) {
                auto *t = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(buf.data());
                for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                    const auto &r = t->table[i];
                    if (tree.contains(r.dwOwningPid)) judgeTcp(tree.value(r.dwOwningPid), v4(r.dwLocalAddr), port(r.dwLocalPort), v4(r.dwRemoteAddr), port(r.dwRemotePort), r.dwState);
                }
            }
            if (fetch(AF_INET6, true)) {
                auto *t = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID *>(buf.data());
                for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                    const auto &r = t->table[i];
                    if (tree.contains(r.dwOwningPid)) judgeTcp(tree.value(r.dwOwningPid) + QStringLiteral("[v6]"), v6(r.ucLocalAddr), port(r.dwLocalPort), v6(r.ucRemoteAddr), port(r.dwRemotePort), r.dwState);
                }
            }
            if (fetch(AF_INET, false)) {
                auto *t = reinterpret_cast<MIB_UDPTABLE_OWNER_PID *>(buf.data());
                for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                    const auto &r = t->table[i];
                    if (tree.contains(r.dwOwningPid))
                        reportUdp(QStringLiteral("%1 udp socket bound on %2:%3").arg(tree.value(r.dwOwningPid), v4(r.dwLocalAddr)).arg(port(r.dwLocalPort)));
                }
            }
            if (fetch(AF_INET6, false)) {
                auto *t = reinterpret_cast<MIB_UDP6TABLE_OWNER_PID *>(buf.data());
                for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                    const auto &r = t->table[i];
                    if (tree.contains(r.dwOwningPid))
                        reportUdp(QStringLiteral("%1 udp6 socket bound on [%2]:%3").arg(tree.value(r.dwOwningPid), v6(r.ucLocalAddr)).arg(port(r.dwLocalPort)));
                }
            }
            ++m_polls;
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    }

    Allowed m_allowed;
    std::atomic<bool> m_run{false};
    std::thread m_thread;
    mutable std::mutex m_lock;
    QStringList m_violations;
    QStringList m_udp;
    QSet<QString> m_tolerated;
    std::atomic<int> m_polls{0};
    std::atomic<int> m_maxProcesses{0};
};
#endif

// The QML side lives in tests/fixtures/engine/: the application's own EngineView in a window, and a raw view for chrome://version.
QStringList treeFiles(const QString &root) {
    QStringList out;
    if (!QFileInfo::exists(root)) return out;
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); out << it.filePath() + QLatin1Char('|') + QString::number(it.fileInfo().size()); }
    out.sort();
    return out;
}
}  // namespace

class EngineLiveTest : public QObject {
    Q_OBJECT

    ConnectionRequest req;
    QString webBase;           // http://127.0.0.1:8099 as the server sees it
    QString fixtureHostPort;   // 127.0.0.1:8099
    bool configured = false;
    std::unique_ptr<EngineBrowser> eb;
    QQmlEngine qml;
    std::unique_ptr<QObject> window;
    QSet<QString> allowInternal;      // "host:port" the test lets the engine open
    bool manualAnswers = false;       // true: nobody answers the internal-address question but the test, through the window's buttons
    QStringList asked;                // every internal-address question that reached the window
    QStringList storageBefore;
    QStringList storageRoots;

    void answerQuestions() {
        connect(eb.get(), &EngineBrowser::changed, this, [this] {
            const QVariantMap ask = eb->pendingConfirm();
            if (ask.isEmpty() || manualAnswers) return;
            const QString key = QStringLiteral("%1:%2").arg(ask.value("host").toString()).arg(ask.value("port").toInt());
            QTimer::singleShot(0, this, [this, key] {
                if (eb->pendingConfirm().isEmpty()) return;
                asked << key;
                eb->answerConfirm(allowInternal.contains(key));
            });
        });
    }
    QObject *makeWindow(const QString &file) {
        QQmlComponent c(&qml);
        c.loadUrl(QUrl::fromLocalFile(QStringLiteral(FORTIQ_ENGINE_FIXTURES "/") + file));
        if (c.status() != QQmlComponent::Ready) { qWarning().noquote() << c.errorString(); return nullptr; }
        QObject *o = c.createWithInitialProperties({ { "engineObj", QVariant::fromValue<QObject *>(eb.get()) } });
        if (!o) qWarning().noquote() << c.errorString();
        return o;
    }
    QVariant evalJs(QObject *w, const QString &code, int timeoutMs = 8000) {
        const int before = w->property("jsCount").toInt();
        QMetaObject::invokeMethod(w, "js", Q_ARG(QVariant, code));
        QElapsedTimer t;
        t.start();
        while (w->property("jsCount").toInt() == before && t.elapsed() < timeoutMs) QTest::qWait(20);
        return w->property("lastJs");
    }
    bool waitTitle(QObject *w, const QString &title, int timeoutMs) {
        QElapsedTimer t;
        t.start();
        while (w->property("pageTitle").toString() != title && t.elapsed() < timeoutMs) QTest::qWait(50);
        return w->property("pageTitle").toString() == title;
    }
    static QString evidencePath() {
        const QString p = env("FORTIQ_ENGINE_REPORT");
        return p.isEmpty() ? QDir::current().absoluteFilePath(QStringLiteral("engine-leak-report.txt")) : p;
    }

private slots:
    void initTestCase() {
        req.host = env("FORTIQ_IT_HOST");
        req.port = env("FORTIQ_IT_PORT").toInt();
        req.user = env("FORTIQ_IT_USER");
        req.expectedHostFingerprint = env("FORTIQ_IT_FINGERPRINT");
        if (!env("FORTIQ_IT_KEY").isEmpty()) req.keyPath = env("FORTIQ_IT_KEY");
        else req.password = env("FORTIQ_IT_PASSWORD");
        webBase = env("FORTIQ_IT_WEB_BASE");
        configured = !req.host.isEmpty() && req.port > 0 && !req.user.isEmpty() && !req.expectedHostFingerprint.isEmpty() && (!req.keyPath.isEmpty() || !env("FORTIQ_IT_PASSWORD").isEmpty()) && !webBase.isEmpty();
        if (!configured) return;
        fixtureHostPort = QUrl(webBase).host() + QLatin1Char(':') + QString::number(QUrl(webBase).port(80));
        allowInternal.insert(fixtureHostPort);
        for (const QStandardPaths::StandardLocation loc : { QStandardPaths::AppLocalDataLocation, QStandardPaths::AppDataLocation, QStandardPaths::CacheLocation })
            storageRoots << QStandardPaths::writableLocation(loc);
        storageRoots.removeDuplicates();
        for (const QString &r : storageRoots) storageBefore << treeFiles(r);

        EngineBrowser::Source src;
        src.connected = [] { return true; };
        src.label = [this] { return req.user + QLatin1Char('@') + req.host; };
        src.token = [] { return 1; };
        src.makeTunnel = [this](QObject *parent) -> gateway::TunnelBackend * { return new gateway::SshTunnel(req, parent); };
        eb = std::make_unique<EngineBrowser>(src);
        answerQuestions();
        eb->setWindowActive(true);
        QCOMPARE(eb->state(), QString("ready"));
        QVERIFY(eb->proxyPort() != 0);
        qml.addImportPath(QStringLiteral("qrc:/qt/qml"));
        qml.addImportPath(QStringLiteral(FORTIQ_QT_QML_DIR));   // Qt's own qml folder (QtWebEngine): the build tree's qt.conf points the default path elsewhere
        window.reset(makeWindow(QStringLiteral("EngineHarness.qml")));
        QVERIFY(window);
    }
    void cleanupTestCase() {
        window.reset();
        eb.reset();
    }

    // Positive control: a leak test is worthless if its watcher cannot see a leak. This process opens a DIRECT connection (and a UDP socket)
    // on purpose; the watcher must report both. (Needs no server: it also runs when the live fixture is absent.)
    void theWatcherCatchesADeliberateDirectConnection() {
#ifndef Q_OS_WIN
        QSKIP("the socket-table watcher is implemented for Windows only");
#else
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
        QUdpSocket udp;
        QVERIFY(udp.bind(QHostAddress::AnyIPv4, 0));
        SocketWatch::Allowed allowed;
        allowed.gatewayPort = 1;   // neither the gateway nor the SSH port: whatever this process connects to is "direct"
        allowed.sshPort = 2;
        allowed.sshHost = QStringLiteral("127.0.0.1");
        SocketWatch watch(allowed);
        watch.start();
        QTest::qWait(400);
        watch.stop();
        QVERIFY2(!watch.violations().isEmpty(), "the watcher did not see a direct TCP connection");
        QVERIFY2(watch.violations().join(QLatin1Char('\n')).contains(QString::number(server.serverPort())), qPrintable(watch.violations().join(QLatin1Char('\n'))));
        QVERIFY2(!watch.udpBound().isEmpty(), "the watcher did not see a UDP socket");
#endif
    }

    void profileAndSettingsAreTheClosedOnes() {
        if (!configured) QSKIP("FORTIQ_IT_* / FORTIQ_IT_WEB_BASE not set (tool/run_live_tests.py sets them)");
        QVariant profile, settings;
        QMetaObject::invokeMethod(window.get(), "profileInfo", Q_RETURN_ARG(QVariant, profile));
        QMetaObject::invokeMethod(window.get(), "settingsInfo", Q_RETURN_ARG(QVariant, settings));
        const QVariantMap p = profile.toMap(), s = settings.toMap();
        QVERIFY2(p.value("otr").toBool(), "the profile must be off-the-record (no cookie, cache or history on disk)");
        QVERIFY2(p.value("storage").toString().isEmpty() || p.value("otr").toBool(), qPrintable(p.value("storage").toString()));
        QVERIFY2(p.value("cachePath").toString().isEmpty(), qPrintable("disk cache path: " + p.value("cachePath").toString()));
        QCOMPARE(p.value("cookies").toInt(), 0);       // NoPersistentCookies
        QCOMPARE(p.value("cache").toInt(), 0);         // MemoryHttpCache (DiskHttpCache = 1, NoCache = 2)
        QVERIFY(!p.value("spell").toBool());
        QVERIFY(s.value("js").toBool());                // the point of the engine
        QVERIFY(!s.value("dns").toBool());
        QVERIFY(!s.value("audit").toBool());
        QVERIFY(s.value("rtc").toBool());
        QVERIFY(!s.value("plugins").toBool());
        QVERIFY(!s.value("clip").toBool());
        QVERIFY(!s.value("popups").toBool());
        QVERIFY(!s.value("remoteFromLocal").toBool());
        QVERIFY(!s.value("fileFromLocal").toBool());
        QVERIFY(!s.value("insecure").toBool());
    }

    // The command line Chromium REALLY runs with (chrome://version in a raw view of an off-the-record profile): every mandatory switch is
    // there. A Qt update that stops honouring one fails here, not in the field.
    void effectiveChromiumCommandLine() {
        if (!configured) QSKIP("FORTIQ_IT_* / FORTIQ_IT_WEB_BASE not set (tool/run_live_tests.py sets them)");
        std::unique_ptr<QObject> raw(makeWindow(QStringLiteral("RawHarness.qml")));
        QVERIFY(raw);
        QElapsedTimer t;
        t.start();
        QString text;
        while (t.elapsed() < 20000) {
            QTest::qWait(300);
            // chrome://gpu is a WebUI built from custom elements with shadow roots: walk them all
            text = evalJs(raw.get(), QStringLiteral("(function walk(n){var t='';if(n.shadowRoot)t+=walk(n.shadowRoot);for(const c of n.childNodes){if(c.nodeType===3)t+=c.textContent+' ';else if(c.nodeType===1)t+=walk(c);}return t;})(document.body||document)"), 3000).toString();
            if (text.contains(QLatin1String("--proxy-server"))) break;
        }
        QVERIFY2(text.contains(QLatin1String("--proxy-server")), qPrintable("chrome://gpu shows no command line:\n" + text.left(500)));
        // chrome://gpu prints the command line of the browser process. The resolver rules hold blanks (and may be quoted): lift them out first.
        QStringList joined;
        QString rest = text;
        const QRegularExpression rules(QStringLiteral("--host-resolver-rules=\"?(MAP \\* ~NOTFOUND , EXCLUDE 127\\.0\\.0\\.1)\"?"));
        const QRegularExpressionMatch m = rules.match(rest);
        if (m.hasMatch()) { joined << QStringLiteral("--host-resolver-rules=") + m.captured(1); rest.remove(m.capturedStart(), m.capturedLength()); }
        for (const QString &tok : rest.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
            if (!tok.startsWith(QLatin1String("--"))) continue;
            QString sw = tok;
            sw.remove(QLatin1Char('"'));
            joined << sw;
        }
        const QStringList problems = auditSwitches(joined, eb->proxyPort());
        QFile report(evidencePath() + QStringLiteral(".cmdline.txt"));
        if (report.open(QIODevice::WriteOnly | QIODevice::Text)) report.write(joined.join(QLatin1Char('\n')).toUtf8());
        QVERIFY2(problems.isEmpty(), qPrintable("Chromium runs without: " + problems.join("; ") + "\nCommand line seen:\n" + joined.join('\n')));
    }

    void javascriptRunsThroughTheGateway() {
        if (!configured) QSKIP("FORTIQ_IT_* / FORTIQ_IT_WEB_BASE not set (tool/run_live_tests.py sets them)");
        const QVariantMap before = eb->gatewayStats();
        asked.clear();
        QMetaObject::invokeMethod(window.get(), "open", Q_ARG(QVariant, webBase + QStringLiteral("/js")));
        QVERIFY2(waitTitle(window.get(), QStringLiteral("js-done"), 45000),
                 qPrintable("title=" + window->property("pageTitle").toString() + " failure=" + window->property("pageFailure").toString() + " asked=" + asked.join(',')));
        const QString text = evalJs(window.get(), QStringLiteral("document.getElementById('out').textContent")).toString();
        QVERIFY2(text.startsWith(QLatin1String("JS-OK sum=500500")), qPrintable(text));             // the script ran here
        QVERIFY2(text.contains(QLatin1String("host=") + fixtureHostPort), qPrintable(text));        // its fetch() reached the server's fixture
        const QVariantMap after = eb->gatewayStats();
        QVERIFY(after.value("accepted").toULongLong() > before.value("accepted").toULongLong());
        QVERIFY(after.value("channelsOpened").toULongLong() > before.value("channelsOpened").toULongLong());
        QVERIFY(after.value("bytesFromServer").toULongLong() > before.value("bytesFromServer").toULongLong());
        QVERIFY2(asked.contains(fixtureHostPort), "the internal address of the fixture must have been ASKED before being opened");
        QVERIFY(after.value("internalAllowed").toULongLong() >= 1);
    }

    void aRefusedInternalAddressIsNeverOpened() {
        if (!configured) QSKIP("FORTIQ_IT_* / FORTIQ_IT_WEB_BASE not set (tool/run_live_tests.py sets them)");
        const QString other = QStringLiteral("127.0.0.1:8098");
        const QVariantMap before = eb->gatewayStats();
        asked.clear();
        QMetaObject::invokeMethod(window.get(), "open", Q_ARG(QVariant, QStringLiteral("http://") + other + QStringLiteral("/page")));
        QTRY_VERIFY_WITH_TIMEOUT(asked.contains(other), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!window->property("pageFailure").toString().isEmpty(), 20000);
        const QVariantMap after = eb->gatewayStats();
        QCOMPARE(after.value("internalDenied").toULongLong(), before.value("internalDenied").toULongLong() + 1);
        QCOMPARE(after.value("channelsOpened").toULongLong(), before.value("channelsOpened").toULongLong());   // no channel was even opened
    }

    void refusedSchemesAndDownloads() {
        if (!configured) QSKIP("FORTIQ_IT_* / FORTIQ_IT_WEB_BASE not set (tool/run_live_tests.py sets them)");
        QMetaObject::invokeMethod(window.get(), "open", Q_ARG(QVariant, webBase + QStringLiteral("/js")));
        QVERIFY(waitTitle(window.get(), QStringLiteral("js-done"), 45000));
        const QString here = window->property("pageUrl").toString();
        // file: and chrome: are refused by OUR policy with a message; ftp: is not even a Chromium protocol any more. In every case the page
        // never changes.
        for (const char *u : {"file:///C:/Windows/win.ini", "chrome://gpu", "chrome://version", "ftp://example.org/x"}) {
            const bool saysWhy = QByteArray(u).startsWith("file:") || QByteArray(u).startsWith("chrome:");
            const int nBefore = window->property("notices").toList().size();
            QMetaObject::invokeMethod(window.get(), "open", Q_ARG(QVariant, QString::fromLatin1(u)));
            if (saysWhy) QTRY_VERIFY2_WITH_TIMEOUT(window->property("notices").toList().size() > nBefore, u, 10000);
            QTest::qWait(1500);
            QCOMPARE(window->property("pageUrl").toString(), here);   // the page never changed
        }
        // a download is cancelled and said so
        QSignalSpy notice(eb.get(), &EngineBrowser::notice);
        QMetaObject::invokeMethod(window.get(), "open", Q_ARG(QVariant, webBase + QStringLiteral("/binary")));
        QTRY_VERIFY_WITH_TIMEOUT(notice.count() > 0, 30000);
        QVERIFY2(notice.last().first().toString().contains(QString::fromUtf8("Téléchargement refusé")), qPrintable(notice.last().first().toString()));
    }

    void noDirectConnectionsWhateverThePageTries() {
        if (!configured) QSKIP("FORTIQ_IT_* / FORTIQ_IT_WEB_BASE not set (tool/run_live_tests.py sets them)");
#ifndef Q_OS_WIN
        QSKIP("the socket-table watcher is implemented for Windows only");
#else
        QMetaObject::invokeMethod(window.get(), "open", Q_ARG(QVariant, QStringLiteral("about:blank")));
        QTest::qWait(500);
        // A listener on THIS computer: if anything the page does reaches it, the engine connected directly (or the proxy bypass is on).
        QTcpServer canary;
        QVERIFY(canary.listen(QHostAddress::Any, 0));
        int canaryHits = 0;
        connect(&canary, &QTcpServer::newConnection, this, [&] { ++canaryHits; while (QTcpSocket *s = canary.nextPendingConnection()) { s->abort(); s->deleteLater(); } });
        const quint16 canaryPort = canary.serverPort();

        SocketWatch::Allowed allowed;
        allowed.gatewayPort = eb->proxyPort();
        allowed.sshPort = quint16(req.port);
        allowed.sshHost = req.host == QLatin1String("localhost") ? QStringLiteral("127.0.0.1") : req.host;
        SocketWatch watch(allowed);
        watch.start();
        const QVariantMap before = eb->gatewayStats();
        const int logBefore = eb->gatewayLog().size();
        asked.clear();

        QMetaObject::invokeMethod(window.get(), "open", Q_ARG(QVariant, webBase + QStringLiteral("/canary?cp=") + QString::number(canaryPort) + QStringLiteral("&skip=") + env("FORTIQ_ENGINE_CANARY_SKIP")));
        const bool done = waitTitle(window.get(), QStringLiteral("canary-done"), 90000);
        QString out = evalJs(window.get(), QStringLiteral("document.getElementById('out') ? document.getElementById('out').textContent : 'no #out'")).toString();
        if (!done) out += QStringLiteral(" | partial=") + evalJs(window.get(), QStringLiteral("typeof R === 'undefined' ? 'R undefined' : JSON.stringify(R)")).toString()
                          + QStringLiteral(" | title=") + window->property("pageTitle").toString() + QStringLiteral(" url=") + window->property("pageUrl").toString();
        // the ping attribute and a few seconds of speculative work after the load
        evalJs(window.get(), QStringLiteral("document.getElementById('ping').click(); 1"));
        QTest::qWait(6000);
        watch.stop();

        const QVariantMap after = eb->gatewayStats();
        const QStringList log = eb->gatewayLog().mid(logBefore);
        const QStringList violations = watch.violations();

        QStringList report;
        report << QStringLiteral("FORTIQ engine leak test") << QStringLiteral("canary page finished: ") + (done ? QStringLiteral("yes") : QStringLiteral("NO"))
               << QStringLiteral("page results: ") + out
               << QStringLiteral("socket polls: %1, most processes seen at once: %2").arg(watch.polls()).arg(watch.maxProcesses())
               << QStringLiteral("tolerated connections:") << watch.tolerated()
               << QStringLiteral("VIOLATIONS: %1").arg(violations.size()) << violations
               << QStringLiteral("UDP sockets bound by the process tree: %1").arg(watch.udpBound().size()) << watch.udpBound()
               << QStringLiteral("client canary listener (this computer, 0.0.0.0:%1) connections: %2").arg(canaryPort).arg(canaryHits)
               << QStringLiteral("internal addresses asked: ") + asked.join(QLatin1Char(','))
               << QStringLiteral("gateway: accepted +%1, blocked +%2, internal denied +%3, channels +%4").arg(after.value("accepted").toULongLong() - before.value("accepted").toULongLong())
                      .arg(after.value("blocked").toULongLong() - before.value("blocked").toULongLong()).arg(after.value("internalDenied").toULongLong() - before.value("internalDenied").toULongLong())
                      .arg(after.value("channelsOpened").toULongLong() - before.value("channelsOpened").toULongLong())
               << QStringLiteral("gateway log (host:port as the gateway saw it):") << log;
        QFile f(evidencePath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) f.write(report.join(QLatin1Char('\n')).toUtf8());
        qInfo().noquote() << report.join(QLatin1Char('\n'));

        QVERIFY2(done, "the canary page did not finish");
        QVERIFY2(violations.isEmpty(), qPrintable("DIRECT CONNECTIONS:\n" + violations.join('\n')));
        QCOMPARE(canaryHits, 0);
        QVERIFY2(watch.udpBound().isEmpty(), qPrintable("UDP sockets exist although the page needs none:\n" + watch.udpBound().join('\n')));
        // The page's attempts were seen by the gateway (they did go through it) ...
        const QString joinedLog = log.join('\n');
        for (const char *host : {"canary-fetch.invalid", "canary-https.invalid", "xhr.canary.invalid", "canary-ws.invalid", "beacon.canary.invalid", "img.canary.invalid", "script.canary.invalid"})
            QVERIFY2(joinedLog.contains(QLatin1String(host)), host);
        // ... and the ones aimed at this computer (127.0.0.1 / localhost / ::1 + canary port) were asked as the SERVER's loopback, never opened.
        QVERIFY2(asked.contains(QStringLiteral("127.0.0.1:%1").arg(canaryPort)) || joinedLog.contains(QStringLiteral(":%1").arg(canaryPort)), "the request for this computer's loopback must reach the gateway");
        QVERIFY(after.value("accepted").toULongLong() > before.value("accepted").toULongLong() + 5);
        // WebRTC produced no usable candidate and WebSocket/WebTransport did not open
        const QJsonObject res = QJsonDocument::fromJson(out.toUtf8()).object();
        QVERIFY2(res.value("websocket").toString() != QLatin1String("open"), qPrintable(out));
        // WebRTC does not exist in the page and in its srcdoc frames, so not even a UDP socket is bound (asserted below)
        QVERIFY2(res.value("rtc").toString().startsWith(QLatin1String("err:")), qPrintable(res.value("rtc").toString()));
        const QJsonObject frames = QJsonDocument::fromJson(res.value("rtc-frames").toString().toUtf8()).object();
        for (const char *k : {"main", "webkit", "srcdoc"})   // (a bare about:blank iframe is the known gap: see theWebRtcGapStillSendsNothing)
            QVERIFY2(frames.value(QLatin1String(k)).toString() == QLatin1String("undefined"), qPrintable(res.value("rtc-frames").toString()));
        QVERIFY2(res.value("webtransport").toString() == QLatin1String("absent") || res.value("webtransport").toString().startsWith(QLatin1String("err:")), qPrintable(out));   // removed, or QUIC refused
        QVERIFY2(res.value("geolocation").toString() != QLatin1String("granted"), qPrintable(out));
#endif
    }

    // The known gap: an about:blank iframe made by a script has a real RTCPeerConnection (the engine's user script does not run there).
    // The forced IP-handling policy must still stop everything: no candidate, no datagram on a UDP listener of this computer (loopback and
    // every local IPv4 address), no direct TCP connection.
    void theWebRtcGapStillSendsNothing() {
        if (!configured) QSKIP("FORTIQ_IT_* / FORTIQ_IT_WEB_BASE not set (tool/run_live_tests.py sets them)");
#ifndef Q_OS_WIN
        QSKIP("the socket-table watcher is implemented for Windows only");
#else
        QUdpSocket canary;
        QVERIFY(canary.bind(QHostAddress::AnyIPv4, 0));
        int datagrams = 0;
        connect(&canary, &QUdpSocket::readyRead, this, [&] { while (canary.hasPendingDatagrams()) { canary.readDatagram(nullptr, 0); ++datagrams; } });
        QStringList ips;
        for (const QHostAddress &a : QNetworkInterface::allAddresses()) if (a.protocol() == QAbstractSocket::IPv4Protocol) ips << a.toString();
        SocketWatch::Allowed allowed;
        allowed.gatewayPort = eb->proxyPort();
        allowed.sshPort = quint16(req.port);
        allowed.sshHost = req.host == QLatin1String("localhost") ? QStringLiteral("127.0.0.1") : req.host;
        SocketWatch watch(allowed);
        watch.start();
        QMetaObject::invokeMethod(window.get(), "open", Q_ARG(QVariant, webBase + QStringLiteral("/webrtc?udp=") + QString::number(canary.localPort()) + QStringLiteral("&ips=") + ips.join(',')));
        const bool done = waitTitle(window.get(), QStringLiteral("webrtc-done"), 60000);
        const QString out = evalJs(window.get(), QStringLiteral("document.getElementById('out') ? document.getElementById('out').textContent : 'no #out'")).toString();
        QTest::qWait(2000);
        watch.stop();
        const QJsonObject res = QJsonDocument::fromJson(out.toUtf8()).object();
        QStringList report;
        report << QStringLiteral("FORTIQ engine WebRTC-gap test") << QStringLiteral("page: ") + out
               << QStringLiteral("UDP canary (this computer, 0.0.0.0:%1; addresses given to the page: %2) datagrams received: %3").arg(canary.localPort()).arg(ips.join(',')).arg(datagrams)
               << QStringLiteral("UDP sockets bound: %1").arg(watch.udpBound().size()) << watch.udpBound()
               << QStringLiteral("TCP VIOLATIONS: %1").arg(watch.violations().size()) << watch.violations();
        QFile f(evidencePath() + QStringLiteral(".webrtc.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) f.write(report.join(QLatin1Char('\n')).toUtf8());
        qInfo().noquote() << report.join(QLatin1Char('\n'));
        QVERIFY2(done, qPrintable(out));
        QCOMPARE(datagrams, 0);
        QVERIFY2(res.value("candidates").toArray().isEmpty(), qPrintable(out));
        QVERIFY2(watch.violations().isEmpty(), qPrintable(watch.violations().join('\n')));
#endif
    }

    // The application's real window: address bar and padlock, the internal-address question with its two buttons, a page that runs JavaScript,
    // a refused navigation, the text-reader switch. A picture is saved for the human eye (engine-window.png next to the report).
    void theWindowOfTheShellWorks() {
        if (!configured) QSKIP("FORTIQ_IT_* / FORTIQ_IT_WEB_BASE not set (tool/run_live_tests.py sets them)");
        std::unique_ptr<QObject> host(makeWindow(QStringLiteral("WindowHarness.qml")));
        QVERIFY(host);
        auto *quick = qobject_cast<QQuickWindow *>(host.get());
        QVERIFY(quick);
        QObject *win = host->property("win").value<QObject *>();
        QVERIFY(win);
        eb->gateway()->forgetConfirmations();        // earlier tests already allowed the fixture: ask again
        manualAnswers = true;                        // this time the USER answers, through the buttons
        auto child = [&](const char *name) { return win->findChild<QObject *>(QString::fromLatin1(name)); };
        QTRY_VERIFY_WITH_TIMEOUT(win->property("ready").toBool(), 20000);
        QVERIFY(child("engineAddress"));
        QVERIFY(child("engineSearch"));
        QVERIFY(child("engineTextMode"));
        // type an address: the gateway asks, the confirmation panel shows, "Ouvrir une fois" is clicked
        QMetaObject::invokeMethod(win, "open", Q_ARG(QVariant, webBase + QStringLiteral("/js")));
        QTRY_VERIFY_WITH_TIMEOUT(child("engineConfirm") && child("engineConfirm")->property("visible").toBool(), 30000);
        QCOMPARE(eb->pendingConfirm().value("host").toString() + QLatin1Char(':') + eb->pendingConfirm().value("port").toString(), fixtureHostPort);
        QTest::qWait(300);
        quick->grabWindow().save(QDir::current().absoluteFilePath(QStringLiteral("engine-window-confirm.png")));
        QObject *open = child("engineConfirmOpen");
        QVERIFY(open);
        QMetaObject::invokeMethod(open, "clicked");
        QTRY_VERIFY_WITH_TIMEOUT(!child("engineConfirm")->property("visible").toBool(), 10000);
        QObject *view = win->property("view").value<QObject *>();
        QVERIFY(view);
        QTRY_COMPARE_WITH_TIMEOUT(view->property("title").toString(), QString("js-done"), 45000);
        QCOMPARE(view->property("failure").toString(), QString());
        QTest::qWait(500);
        quick->grabWindow().save(QDir::current().absoluteFilePath(QStringLiteral("engine-window.png")));
        // cancelling refuses: nothing is opened for the second address
        const QVariantMap before = eb->gatewayStats();
        QMetaObject::invokeMethod(win, "open", Q_ARG(QVariant, QStringLiteral("http://127.0.0.1:8097/never")));
        QTRY_VERIFY_WITH_TIMEOUT(child("engineConfirm")->property("visible").toBool(), 30000);
        QMetaObject::invokeMethod(child("engineConfirmCancel"), "clicked");
        QTRY_VERIFY_WITH_TIMEOUT(!child("engineConfirm")->property("visible").toBool(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!view->property("failure").toString().isEmpty(), 20000);
        QCOMPARE(eb->gatewayStats().value("channelsOpened").toULongLong(), before.value("channelsOpened").toULongLong());
        // a refused address says why in the notice bar
        QMetaObject::invokeMethod(win, "open", Q_ARG(QVariant, QStringLiteral("file:///C:/Windows/win.ini")));
        QTRY_VERIFY_WITH_TIMEOUT(!win->property("notice").toString().isEmpty() || !view->property("url").toString().startsWith(QLatin1String("file:")), 10000);
        QVERIFY(!view->property("url").toString().startsWith(QLatin1String("file:")));
        // the switch to the text reader is offered
        QSignalSpy textMode(win, SIGNAL(requestTextMode()));
        QMetaObject::invokeMethod(child("engineTextMode"), "clicked");
        QCOMPARE(textMode.count(), 1);
        manualAnswers = false;
    }

    void nothingIsWrittenToDisk() {
        if (!configured) QSKIP("FORTIQ_IT_* / FORTIQ_IT_WEB_BASE not set (tool/run_live_tests.py sets them)");
        QStringList now;
        for (const QString &r : storageRoots) now << treeFiles(r);
        QStringList created;
        // (the compiled-QML cache of Qt Quick for the harness file is not web data: it is excluded; the application's QML is in resources)
        for (const QString &f : now) if (!storageBefore.contains(f) && !f.contains(QLatin1String("/qmlcache/"))) created << f;
        QFile f(evidencePath() + QStringLiteral(".disk.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) f.write((QStringLiteral("roots: ") + storageRoots.join(';') + QStringLiteral("\nnew or changed files: %1\n").arg(created.size()) + created.join('\n')).toUtf8());
        QVERIFY2(created.isEmpty(), qPrintable("the engine wrote files:\n" + created.join('\n')));
    }

    void closingTheWindowClosesTheGateway() {
        if (!configured) QSKIP("FORTIQ_IT_* / FORTIQ_IT_WEB_BASE not set (tool/run_live_tests.py sets them)");
        const quint16 port = eb->proxyPort();
        window.reset();
        eb->setWindowActive(false);
        QCOMPARE(eb->proxyPort(), quint16(0));
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(!s.waitForConnected(500));      // the proxy port is closed: nothing can leave
    }
};

int main(int argc, char **argv) {
    // Run from the build tree: its qt.conf moves Qt's "prefix" to the build folder, where Chromium's helper process and resources are not.
    const QString kit = QDir::cleanPath(QStringLiteral(FORTIQ_QT_PREFIX));   // Chromium cannot open a path with ".."
    auto native = [&](const char *rel) { return QDir::toNativeSeparators(kit + QLatin1Char('/') + QLatin1String(rel)).toLocal8Bit(); };
    if (qEnvironmentVariableIsEmpty("QTWEBENGINEPROCESS_PATH")) qputenv("QTWEBENGINEPROCESS_PATH", native("bin/QtWebEngineProcess.exe"));
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_RESOURCES_PATH")) qputenv("QTWEBENGINE_RESOURCES_PATH", native("resources"));
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_LOCALES_PATH")) qputenv("QTWEBENGINE_LOCALES_PATH", native("translations/qtwebengine_locales"));
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CYBOU"));
    QCoreApplication::setApplicationName(QStringLiteral("FORTIQ-engine-it"));
    EngineLiveTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "EngineLiveTest.moc"
