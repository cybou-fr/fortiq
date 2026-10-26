// Offline tests of the Browser's SSH proxy gateway (docs/BROWSER.md, "Passerelle SSH"): HTTP wire parsing, authentication, limits,
// address policy, the relays (CONNECT and absolute-form HTTP with keep-alive, chunked, close-delimited), backpressure.
// No SSH server: the gateway talks to a FakeTunnel that opens sockets to local test servers. The real tunnel is exercised by
// GatewayLiveTest.cpp against the disposable fixture.
#include "browser/gateway/HttpWire.h"
#include "browser/gateway/ProxyGateway.h"
#include "browser/gateway/TunnelBackend.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>

using namespace fortiq::gateway;

namespace {

// ---------------------------------------------------------------- fake SSH side
class FakeTunnel : public TunnelBackend {
public:
    QMap<QString, quint16> routes;          // "ip:port" -> local port the "server" listens on
    QMap<QString, QStringList> dns;         // name -> successive answers (the last one repeats)
    QStringList opened;                     // "ip:port" of every channel the gateway asked for
    QStringList execHosts;
    int pauseCalls = 0, resumeCalls = 0, closeCalls = 0;
    bool execFails = false;

    struct Ch { QTcpSocket *sock = nullptr; bool paused = false; QByteArray held; };
    QHash<quint64, Ch> chs;
    quint64 next = 1;

    quint64 openChannel(const QString &ip, int port) override {
        const quint64 id = next++;
        const QString key = QStringLiteral("%1:%2").arg(ip).arg(port);
        opened << key;
        QTimer::singleShot(0, this, [this, id, key] {
            if (!routes.contains(key)) { emit channelFailed(id, QStringLiteral("connexion refusée (fake)")); return; }
            auto *s = new QTcpSocket(this);
            chs[id].sock = s;
            connect(s, &QTcpSocket::connected, this, [this, id] { emit channelOpened(id); });
            connect(s, &QTcpSocket::readyRead, this, [this, id] { drain(id); });
            connect(s, &QTcpSocket::disconnected, this, [this, id] {
                if (!chs.contains(id)) return;
                drain(id);
                chs[id].sock->deleteLater();
                chs.remove(id);
                emit channelClosed(id, QString());
            });
            s->connectToHost(QHostAddress::LocalHost, routes.value(key));
        });
        return id;
    }
    void drain(quint64 id) {
        auto it = chs.find(id);
        if (it == chs.end() || it->paused || !it->sock) return;
        const QByteArray data = it->sock->readAll();
        if (!data.isEmpty()) emit channelData(id, data);
    }
    void write(quint64 id, const QByteArray &data) override {
        auto it = chs.find(id);
        if (it == chs.end() || !it->sock) return;
        it->sock->write(data);
        const qint64 n = data.size();
        QTimer::singleShot(0, this, [this, id, n] { emit channelWritten(id, n); });
    }
    void shutdownWrite(quint64) override {}
    void setReading(quint64 id, bool on) override {
        auto it = chs.find(id);
        if (it == chs.end()) return;
        it->paused = !on;
        if (on) { ++resumeCalls; QTimer::singleShot(0, this, [this, id] { drain(id); }); } else ++pauseCalls;
    }
    void closeChannel(quint64 id) override {
        ++closeCalls;
        auto it = chs.find(id);
        if (it == chs.end()) return;
        QTcpSocket *s = it->sock;
        chs.erase(it);
        if (s) { s->disconnect(this); s->abort(); s->deleteLater(); }
    }
    quint64 exec(const QByteArray &script, int) override {
        const quint64 id = next++;
        static const QRegularExpression re(QStringLiteral("getent ahosts '([^']*)'"));
        const QString host = re.match(QString::fromUtf8(script)).captured(1);
        execHosts << host;
        QTimer::singleShot(0, this, [this, id, host] {
            if (execFails) { emit execFinished(id, false, {}, QStringLiteral("délai (fake)")); return; }
            QStringList answers = dns.value(host);
            QString answer;
            if (!answers.isEmpty()) { answer = answers.size() > 1 ? answers.takeFirst() : answers.first(); dns[host] = answers; }
            emit execFinished(id, true, answer.toUtf8(), QString());
        });
        return id;
    }
    void dropAll(const QString &reason) {   // the SSH transport died
        const QList<quint64> ids = chs.keys();
        for (const quint64 id : ids) {
            QTcpSocket *s = chs[id].sock;
            chs.remove(id);
            if (s) { s->disconnect(this); s->abort(); s->deleteLater(); }
            emit channelClosed(id, reason);
        }
    }
};

// ---------------------------------------------------------------- "web server on the remote side"
class WebServer : public QObject {
public:
    QTcpServer server;
    QStringList heads;            // request heads as received (one string per request)
    QList<QByteArray> bodies;
    explicit WebServer(QObject *parent = nullptr) : QObject(parent) {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto *buf = new QByteArray;
                connect(s, &QTcpSocket::readyRead, s, [this, s, buf] { *buf += s->readAll(); handle(s, buf); });
                connect(s, &QTcpSocket::disconnected, s, [s, buf] { delete buf; s->deleteLater(); });
            }
        });
    }
    quint16 port() const { return server.serverPort(); }

    static QByteArray chunked(const QList<QByteArray> &parts) {
        QByteArray out;
        for (const QByteArray &p : parts) out += QByteArray::number(p.size(), 16) + "\r\n" + p + "\r\n";
        return out + "0\r\nX-Trailer: yes\r\n\r\n";
    }

    void handle(QTcpSocket *s, QByteArray *buf) {
        const qsizetype end = buf->indexOf("\r\n\r\n");
        if (end < 0) return;
        const QByteArray head = buf->left(end);
        const QByteArray line = head.left(head.indexOf("\r\n"));
        const QByteArray rest = buf->mid(end + 4);
        QByteArray body;
        if (head.toLower().contains("transfer-encoding: chunked")) {
            if (!rest.endsWith("0\r\n\r\n")) return;   // wait for the whole chunked body (trailer-less in tests)
            qsizetype i = 0;
            while (i < rest.size()) {
                const qsizetype eol = rest.indexOf("\r\n", i);
                const int n = rest.mid(i, eol - i).toInt(nullptr, 16);
                if (n == 0) break;
                body += rest.mid(eol + 2, n);
                i = eol + 2 + n + 2;
            }
        } else {
            const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("content-length: *(\\d+)"), QRegularExpression::CaseInsensitiveOption)
                                                  .match(QString::fromLatin1(head));
            const int len = m.hasMatch() ? m.captured(1).toInt() : 0;
            if (rest.size() < len) return;
            body = rest.left(len);
        }
        buf->clear();
        heads << QString::fromLatin1(head);
        bodies << body;
        QByteArray path = line.split(' ').value(1);
        if (path.contains('?')) path = path.left(path.indexOf('?'));
        QByteArray resp;
        auto fixed = [](const QByteArray &payload, const QByteArray &type = "text/plain") {
            return "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\nConnection: keep-alive\r\n\r\n" + payload;
        };
        if (path == "/hello") resp = line.startsWith("HEAD") ? QByteArray("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n") : fixed("hello");
        else if (path == "/headers") resp = fixed(head);
        else if (path == "/echo") resp = fixed(body);
        else if (path == "/chunked") resp = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" + chunked({ "abc", "defgh", QByteArray(5000, 'x') });
        else if (path == "/close") resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nstreamed-until-close";
        else if (path == "/nocontent") resp = "HTTP/1.1 204 No Content\r\n\r\n";
        else if (path == "/continue") resp = "HTTP/1.1 100 Continue\r\n\r\n" + fixed("after-continue");
        else if (path == "/upgrade") resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n\r\n";
        else if (path == "/big") resp = fixed(QByteArray(3 * 1024 * 1024, 'b'));
        else if (path == "/cut") resp = "HTTP/1.1 200 OK\r\nContent-Length: 100000\r\n\r\nonly-a-little";
        else resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        s->write(resp);
        s->disconnectFromHost();
    }
};

class EchoServer : public QObject {
public:
    QTcpServer server;
    explicit EchoServer(QObject *parent = nullptr) : QObject(parent) {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [s] { s->write(s->readAll()); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    quint16 port() const { return server.serverPort(); }
};

// ---------------------------------------------------------------- a minimal proxy client
struct Client {
    QTcpSocket sock;
    QByteArray got;
    bool closed = false;
    Client() {
        QObject::connect(&sock, &QTcpSocket::readyRead, &sock, [this] { got += sock.readAll(); });
        QObject::connect(&sock, &QTcpSocket::aboutToClose, &sock, [this] { got += sock.readAll(); });
        QObject::connect(&sock, &QTcpSocket::disconnected, &sock, [this] { closed = true; });
    }
    bool connectTo(quint16 port) {
        sock.connectToHost(QHostAddress::LocalHost, port);
        return sock.waitForConnected(3000);
    }
    void send(const QByteArray &data) { sock.write(data); }
    int status() const {
        const QByteArray line = got.left(got.indexOf("\r\n"));
        return line.split(' ').value(1).toInt();
    }
    QByteArray body() const { const qsizetype i = got.indexOf("\r\n\r\n"); return i < 0 ? QByteArray() : got.mid(i + 4); }
};

QByteArray head(const QByteArray &method, const QByteArray &target, const QByteArray &auth, const QByteArray &extra = {}) {
    return method + " " + target + " HTTP/1.1\r\nHost: x\r\n" + (auth.isEmpty() ? QByteArray() : "Proxy-Authorization: " + auth + "\r\n") + extra + "\r\n";
}
}  // namespace

class GatewayTest : public QObject {
    Q_OBJECT

    FakeTunnel *tunnel = nullptr;
    ProxyGateway *gateway = nullptr;
    WebServer *web = nullptr;
    EchoServer *echo = nullptr;
    QByteArray auth;
    int asked = 0;
    bool answer = false;
    QList<InternalAccessRequest> questions;

    void startGateway(const GatewayOptions &options = {}) {
        gateway = new ProxyGateway(tunnel);
        QVERIFY(gateway->start(options));
        auth = fortiq::gateway::basicAuthValue("fortiq", gateway->token().toLatin1());
        gateway->setConfirmHandler([this](const InternalAccessRequest &r, std::function<void(bool)> reply) {
            ++asked;
            questions << r;
            reply(answer);
        });
    }
    // CONNECT through the gateway; returns the client once the status line is complete.
    std::unique_ptr<Client> connectVia(const QByteArray &target, const QByteArray &authValue = {}) {
        auto c = std::make_unique<Client>();
        if (!c->connectTo(gateway->port())) return nullptr;
        c->send(head("CONNECT", target, authValue.isNull() ? auth : authValue));
        const Client *p = c.get();
        (void)QTest::qWaitFor([p] { return p->got.contains("\r\n\r\n") || p->closed; }, 4000);
        return c;
    }
    std::unique_ptr<Client> request(const QByteArray &raw, bool wait = true) {
        auto c = std::make_unique<Client>();
        if (!c->connectTo(gateway->port())) return nullptr;
        c->send(raw);
        const Client *p = c.get();
        if (wait) (void)QTest::qWaitFor([p] { return p->closed; }, 4000);
        return c;
    }

private slots:
    void init() {
        tunnel = new FakeTunnel;
        web = new WebServer;
        echo = new EchoServer;
        tunnel->routes.insert(QStringLiteral("93.184.216.34:80"), web->port());
        tunnel->routes.insert(QStringLiteral("93.184.216.34:443"), echo->port());
        tunnel->routes.insert(QStringLiteral("127.0.0.1:8099"), web->port());
        tunnel->routes.insert(QStringLiteral("10.1.2.3:80"), web->port());
        tunnel->dns.insert(QStringLiteral("public.test"), { QStringLiteral("93.184.216.34\n") });
        tunnel->dns.insert(QStringLiteral("mixed.test"), { QStringLiteral("93.184.216.34\n127.0.0.1\n") });
        tunnel->dns.insert(QStringLiteral("meta.test"), { QStringLiteral("169.254.169.254\n") });
        tunnel->dns.insert(QStringLiteral("internal.test"), { QStringLiteral("127.0.0.1\n") });
        tunnel->dns.insert(QStringLiteral("nothing.test"), { QString() });
        asked = 0; answer = false; questions.clear();
    }
    void cleanup() {
        delete gateway; gateway = nullptr;
        delete tunnel; tunnel = nullptr;
        delete web; web = nullptr;
        delete echo; echo = nullptr;
    }

    // ------------------------------------------------------------ HttpWire (no gateway)
    void wireParsesAndRejects_data() {
        QTest::addColumn<QByteArray>("raw");
        QTest::addColumn<int>("status");   // ParseStatus
        QTest::newRow("ok connect") << QByteArray("CONNECT a.example:443 HTTP/1.1\r\nHost: a.example:443\r\n\r\n") << int(ParseStatus::Ok);
        QTest::newRow("ok absolute") << QByteArray("GET http://a.example/x?y=1 HTTP/1.1\r\nHost: a.example\r\nAccept: */*\r\n\r\n") << int(ParseStatus::Ok);
        QTest::newRow("incomplete") << QByteArray("GET http://a.example/ HTTP/1.1\r\nHost: a") << int(ParseStatus::NeedMore);
        QTest::newRow("bare LF") << QByteArray("GET / HTTP/1.1\nHost: a\n\n\r\n\r\n") << int(ParseStatus::Bad);
        QTest::newRow("bare CR") << QByteArray("GET / HTTP/1.1\r\nHost: a\rX: y\r\n\r\n") << int(ParseStatus::Bad);
        QTest::newRow("NUL") << QByteArray("GET / HTTP/1.1\r\nHost: a\0b\r\n\r\n", 34) << int(ParseStatus::Bad);
        QTest::newRow("obs-fold") << QByteArray("GET / HTTP/1.1\r\nHost: a\r\n b\r\n\r\n") << int(ParseStatus::Bad);
        QTest::newRow("space before colon") << QByteArray("GET / HTTP/1.1\r\nHost : a\r\n\r\n") << int(ParseStatus::Bad);
        QTest::newRow("bad version") << QByteArray("GET / HTTP/2.0\r\nHost: a\r\n\r\n") << int(ParseStatus::Bad);
        QTest::newRow("two spaces") << QByteArray("GET  / HTTP/1.1\r\n\r\n") << int(ParseStatus::Bad);
        QTest::newRow("lower method ok token") << QByteArray("get / HTTP/1.1\r\n\r\n") << int(ParseStatus::Ok);
        QTest::newRow("too large") << (QByteArray("GET / HTTP/1.1\r\nX: ") + QByteArray(20000, 'a') + "\r\n\r\n") << int(ParseStatus::TooLarge);
        QTest::newRow("too large no end") << (QByteArray("GET / HTTP/1.1\r\nX: ") + QByteArray(20000, 'a')) << int(ParseStatus::TooLarge);
    }
    void wireParsesAndRejects() {
        QFETCH(QByteArray, raw);
        QFETCH(int, status);
        RequestHead h;
        QCOMPARE(int(parseRequestHead(raw, &h).status), status);
    }
    void wireTooManyHeaders() {
        QByteArray raw = "GET / HTTP/1.1\r\n";
        for (int i = 0; i < 101; ++i) raw += "X-" + QByteArray::number(i) + ": v\r\n";
        raw += "\r\n";
        RequestHead h;
        QCOMPARE(int(parseRequestHead(raw, &h).status), int(ParseStatus::Bad));
    }
    void wireAuthority_data() {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<QString>("host");
        QTest::addColumn<int>("port");
        QTest::newRow("name") << QByteArray("Example.COM:443") << true << "example.com" << 443;
        QTest::newRow("ipv4") << QByteArray("10.0.0.1:8080") << true << "10.0.0.1" << 8080;
        QTest::newRow("ipv6") << QByteArray("[2001:db8::1]:443") << true << "2001:db8::1" << 443;
        QTest::newRow("no port") << QByteArray("example.com") << false << "" << 0;
        QTest::newRow("port 0") << QByteArray("example.com:0") << false << "" << 0;
        QTest::newRow("port 65536") << QByteArray("example.com:65536") << false << "" << 0;
        QTest::newRow("port text") << QByteArray("example.com:80a") << false << "" << 0;
        QTest::newRow("userinfo") << QByteArray("a@example.com:80") << false << "" << 0;
        QTest::newRow("percent") << QByteArray("exa%6dple.com:80") << false << "" << 0;
        QTest::newRow("unicode") << QByteArray("exämple.com:80") << false << "" << 0;
        QTest::newRow("space") << QByteArray("exa mple.com:80") << false << "" << 0;
        QTest::newRow("bad v6") << QByteArray("[::zz]:80") << false << "" << 0;
        QTest::newRow("v4 in brackets") << QByteArray("[127.0.0.1]:80") << false << "" << 0;
        QTest::newRow("empty") << QByteArray(":80") << false << "" << 0;
    }
    void wireAuthority() {
        QFETCH(QByteArray, text);
        QFETCH(bool, ok);
        QFETCH(QString, host);
        QFETCH(int, port);
        const Authority a = parseAuthority(text, 0);
        QCOMPARE(a.ok, ok);
        if (ok) { QCOMPARE(a.host, host); QCOMPARE(a.port, port); }
    }
    void wireAbsoluteForm_data() {
        QTest::addColumn<QByteArray>("uri");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<QString>("host");
        QTest::addColumn<int>("port");
        QTest::addColumn<QByteArray>("path");
        QTest::newRow("plain") << QByteArray("http://a.example/x/y?z=1") << true << "a.example" << 80 << QByteArray("/x/y?z=1");
        QTest::newRow("port") << QByteArray("http://a.example:8080") << true << "a.example" << 8080 << QByteArray("/");
        QTest::newRow("query only") << QByteArray("http://a.example?q=1") << true << "a.example" << 80 << QByteArray("/?q=1");
        QTest::newRow("fragment dropped") << QByteArray("http://a.example/p#frag") << true << "a.example" << 80 << QByteArray("/p");
        QTest::newRow("scheme case") << QByteArray("HTTP://A.example/") << true << "a.example" << 80 << QByteArray("/");
        QTest::newRow("ipv6") << QByteArray("http://[::1]:81/a") << true << "::1" << 81 << QByteArray("/a");
        QTest::newRow("https refused") << QByteArray("https://a.example/") << false << "" << 0 << QByteArray();
        QTest::newRow("userinfo") << QByteArray("http://u:p@a.example/") << false << "" << 0 << QByteArray();
        QTest::newRow("ftp") << QByteArray("ftp://a.example/") << false << "" << 0 << QByteArray();
        QTest::newRow("empty host") << QByteArray("http:///x") << false << "" << 0 << QByteArray();
    }
    void wireAbsoluteForm() {
        QFETCH(QByteArray, uri);
        QFETCH(bool, ok);
        QFETCH(QString, host);
        QFETCH(int, port);
        QFETCH(QByteArray, path);
        const HttpTarget t = parseAbsoluteHttp(uri);
        QCOMPARE(t.ok, ok);
        if (ok) { QCOMPARE(t.host, host); QCOMPARE(t.port, port); QCOMPARE(t.path, path); }
    }
    void wireProxyAuthorization() {
        const QByteArray good = basicAuthValue("fortiq", "s3cret");
        RequestHead h;
        h.headers = { { "Proxy-Authorization", good } };
        QVERIFY(proxyAuthorized(&h, good));
        QVERIFY2(h.headers[0].value.isEmpty(), "the credential must be wiped from the parsed head");
        h.headers = { { "Proxy-Authorization", basicAuthValue("fortiq", "s3creT") } };
        QVERIFY(!proxyAuthorized(&h, good));
        h.headers = { { "Proxy-Authorization", good.left(good.size() - 1) } };
        QVERIFY(!proxyAuthorized(&h, good));
        h.headers = {};
        QVERIFY(!proxyAuthorized(&h, good));
        h.headers = { { "Proxy-Authorization", good }, { "proxy-authorization", good } };
        QVERIFY2(!proxyAuthorized(&h, good), "two credentials are ambiguous");
        h.headers = { { "Authorization", good } };
        QVERIFY2(!proxyAuthorized(&h, good), "Authorization is not Proxy-Authorization");
    }
    void wireFramingDecisions() {
        RequestHead h;
        h.headers = { { "Content-Length", "5" }, { "Transfer-Encoding", "chunked" } };
        QVERIFY2(!requestFraming(h).ok, "CL + TE = smuggling shape");
        h.headers = { { "Content-Length", "5" }, { "Content-Length", "6" } };
        QVERIFY(!requestFraming(h).ok);
        h.headers = { { "Content-Length", "5, 5" } };
        QVERIFY(requestFraming(h).ok);
        h.headers = { { "Content-Length", "+5" } };
        QVERIFY(!requestFraming(h).ok);
        h.headers = { { "Content-Length", "-1" } };
        QVERIFY(!requestFraming(h).ok);
        h.headers = { { "Transfer-Encoding", "gzip, chunked" } };
        QVERIFY(!requestFraming(h).ok);
        h.headers = {};
        QCOMPARE(requestFraming(h).mode, BodyFramer::Mode::None);
        ResponseHead r;
        r.status = 200;
        QCOMPARE(responseFraming(r, "GET").mode, BodyFramer::Mode::UntilClose);
        QCOMPARE(responseFraming(r, "HEAD").mode, BodyFramer::Mode::None);
        r.status = 304;
        QCOMPARE(responseFraming(r, "GET").mode, BodyFramer::Mode::None);
        r.status = 200;
        r.headers = { { "Content-Length", "0" } };
        QCOMPARE(responseFraming(r, "GET").mode, BodyFramer::Mode::None);
    }
    void wireChunkedFramerAnySplit() {
        const QByteArray body = WebServer::chunked({ "hello", QByteArray(300, 'q'), "end" });
        const QByteArray tail = "NEXT-MESSAGE";
        for (int step : { 1, 2, 3, 7, 64, 1000 }) {
            BodyFramer f;
            f.reset(BodyFramer::Mode::Chunked);
            const QByteArray all = body + tail;
            qsizetype used = 0;
            for (qsizetype i = 0; i < all.size() && !f.done(); i += step)
                used += f.consume(all.mid(i, step));
            QVERIFY2(f.done() && !f.failed(), qPrintable(QStringLiteral("step %1").arg(step)));
            QCOMPARE(used, body.size());
        }
        BodyFramer bad;
        bad.reset(BodyFramer::Mode::Chunked);
        bad.consume("zz\r\n");
        QVERIFY(bad.failed());
        BodyFramer missingCrlf;
        missingCrlf.reset(BodyFramer::Mode::Chunked);
        missingCrlf.consume("3\r\nabcXY");
        QVERIFY(missingCrlf.failed());
        BodyFramer len;
        len.reset(BodyFramer::Mode::Length, 5);
        QCOMPARE(len.consume("abcdefgh"), qsizetype(5));
        QVERIFY(len.done());
    }
    void wireOriginRequestIsClean() {
        RequestHead h;
        QVERIFY(parseRequestHead("GET http://a.example:8080/p?q=1 HTTP/1.1\r\nHost: evil\r\nProxy-Authorization: Basic eHg=\r\nProxy-Connection: keep-alive\r\n"
                                 "Connection: keep-alive, X-Secret\r\nX-Secret: s\r\nKeep-Alive: 5\r\nTE: trailers\r\nAccept: */*\r\nCookie: a=b\r\n\r\n", &h).status == ParseStatus::Ok);
        const HttpTarget t = parseAbsoluteHttp(h.target);
        const QByteArray out = serializeOriginRequest(h, t);
        QVERIFY(out.startsWith("GET /p?q=1 HTTP/1.1\r\nHost: a.example:8080\r\n"));
        const QByteArray lower = out.toLower();
        for (const char *banned : { "proxy-", "keep-alive", "x-secret", "te:", "evil" }) QVERIFY2(!lower.contains(banned), banned);
        QVERIFY(out.contains("Accept: */*\r\n") && out.contains("Cookie: a=b\r\n"));
        QVERIFY(out.endsWith("Connection: close\r\n\r\n"));
    }

    // ------------------------------------------------------------ authentication, limits, malformed input
    void listensOnLoopbackOnly() {
        startGateway();
        QVERIFY(gateway->isListening());
        QVERIFY(gateway->port() > 0);
        QCOMPARE(gateway->token().size(), 48);
        QVERIFY(gateway->token() != QString(48, QLatin1Char('0')));
        // the listening socket is bound to 127.0.0.1 (QTcpServer::serverAddress)
        auto *probe = new QTcpServer(this);
        QVERIFY(probe->listen(QHostAddress::LocalHost, 0));
        delete probe;
    }
    void tokenIsDifferentForEachStart() {
        startGateway();
        const QString first = gateway->token();
        gateway->stop();
        QVERIFY(gateway->start());
        QVERIFY(gateway->token() != first);
        QCOMPARE(gateway->token().size(), 48);
    }
    void missingOrWrongAuthIsRefused() {
        startGateway();
        auto none = request(head("CONNECT", "public.test:443", {}));
        QCOMPARE(none->status(), 407);
        QVERIFY(none->got.contains("Proxy-Authenticate: Basic"));
        auto wrong = request(head("CONNECT", "public.test:443", fortiq::gateway::basicAuthValue("fortiq", "wrong")));
        QCOMPARE(wrong->status(), 407);
        auto otherUser = request(head("CONNECT", "public.test:443", fortiq::gateway::basicAuthValue("root", gateway->token().toLatin1())));
        QCOMPARE(otherUser->status(), 407);
        auto http = request(head("GET", "http://public.test/hello", {}));
        QCOMPARE(http->status(), 407);
        QCOMPARE(tunnel->opened.size(), 0);
        QCOMPARE(tunnel->execHosts.size(), 0);
        QVERIFY(!none->got.contains(gateway->token().toLatin1()));
        QCOMPARE(gateway->stats().authRejected, quint64(4));
    }
    void directRequestToTheGatewayItselfIsNotProxied() {
        startGateway();
        auto c = request("GET /hello HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        QCOMPARE(c->status(), 407);   // no credential: refused before anything else
        c = request(head("GET", "/hello", auth));
        QCOMPARE(c->status(), 400);
        QCOMPARE(tunnel->opened.size(), 0);
    }
    void malformedAndOversizedInput() {
        startGateway();
        QCOMPARE(request("GARBAGE\r\n\r\n")->status(), 400);
        QCOMPARE(request(head("CONNECT", "no-port.test", auth))->status(), 400);
        QCOMPARE(request(head("CONNECT", "a b.test:443", auth))->status(), 400);
        QCOMPARE(request(head("GET", "https://public.test/", auth))->status(), 400);
        QCOMPARE(request(head("GET", "http://u:p@public.test/", auth))->status(), 400);
        QCOMPARE(request(head("POST", "http://public.test/echo", auth, "Content-Length: 5\r\nTransfer-Encoding: chunked\r\n"))->status(), 400);
        QCOMPARE(request(head("GET", "http://public.test/", auth, "Upgrade: websocket\r\nConnection: Upgrade\r\n"))->status(), 501);
        QCOMPARE(request("GET http://public.test/ HTTP/1.1\r\nX: " + QByteArray(20000, 'a') + "\r\n\r\n")->status(), 431);
        QCOMPARE(tunnel->opened.size(), 0);
        QVERIFY(gateway->stats().malformed >= 6);
    }
    void slowHeadTimesOut() {
        GatewayOptions o;
        o.headTimeoutMs = 1000;
        startGateway(o);
        Client c;
        QVERIFY(c.connectTo(gateway->port()));
        c.send("CONNECT public.test:443 HTTP/1.1\r\n");   // never finished
        QTRY_VERIFY_WITH_TIMEOUT(c.closed, 4000);
        QTRY_COMPARE_WITH_TIMEOUT(gateway->stats().active, 0, 1000);
    }
    void connectionLimitAnswers503() {
        GatewayOptions o;
        o.maxConnections = 3;
        startGateway(o);
        std::vector<std::unique_ptr<Client>> held;
        for (int i = 0; i < 3; ++i) {
            held.push_back(std::make_unique<Client>());
            QVERIFY(held.back()->connectTo(gateway->port()));
        }
        QTRY_COMPARE(gateway->stats().active, 3);
        auto extra = request(head("CONNECT", "public.test:443", auth));
        QCOMPARE(extra->status(), 503);
        QCOMPARE(gateway->stats().limitRejected, quint64(1));
        held.clear();
        QTRY_COMPARE_WITH_TIMEOUT(gateway->stats().active, 0, 2000);
        auto again = connectVia("public.test:443");
        QCOMPARE(again->status(), 200);
    }
    void idleTunnelIsClosed() {
        GatewayOptions o;
        o.idleTimeoutMs = 1000;
        startGateway(o);
        auto c = connectVia("public.test:443");
        QCOMPARE(c->status(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(c->closed, 4500);
        QVERIFY(tunnel->closeCalls >= 1);
    }

    // ------------------------------------------------------------ CONNECT relay
    void connectRelaysBytesBothWays() {
        startGateway();
        auto c = connectVia("public.test:443");
        QCOMPARE(c->status(), 200);
        QVERIFY(c->got.startsWith("HTTP/1.1 200 Connection Established"));
        c->got.clear();
        c->send("hello through the tunnel");
        QTRY_COMPARE(c->got, QByteArray("hello through the tunnel"));
        QCOMPARE(tunnel->opened, QStringList { QStringLiteral("93.184.216.34:443") });
        QCOMPARE(tunnel->execHosts, QStringList { QStringLiteral("public.test") });
        c->sock.disconnectFromHost();
        QTRY_COMPARE(gateway->stats().active, 0);
        QVERIFY(tunnel->closeCalls >= 1);
    }
    void connectEarlyBytesAreKept() {
        startGateway();
        Client c;
        QVERIFY(c.connectTo(gateway->port()));
        c.send(head("CONNECT", "public.test:443", auth) + "EARLY");   // a client that does not wait for the 200
        QTRY_VERIFY(c.got.endsWith("EARLY"));
    }
    void connectToLiteralPublicIpNeedsNoResolution() {
        startGateway();
        tunnel->routes.insert(QStringLiteral("8.8.8.8:443"), echo->port());
        auto c = connectVia("8.8.8.8:443");
        QCOMPARE(c->status(), 200);
        QCOMPARE(tunnel->execHosts.size(), 0);
        QCOMPARE(tunnel->opened, QStringList { QStringLiteral("8.8.8.8:443") });
    }
    void tunnelFailureIsAnHonest502() {
        startGateway();
        auto c = connectVia("8.8.4.4:443");   // no route in the fake: "connection refused" on the server
        QCOMPARE(c->status(), 502);
        QVERIFY(c->got.contains("n'a pas pu ouvrir"));
    }
    void lostTransportBreaksTheTunnelsAtOnce() {
        startGateway();
        std::vector<std::unique_ptr<Client>> cs;
        for (int i = 0; i < 5; ++i) cs.push_back(connectVia("public.test:443"));
        for (auto &c : cs) QCOMPARE(c->status(), 200);
        tunnel->dropAll(QStringLiteral("Connexion SSH perdue"));
        for (auto &c : cs) QTRY_VERIFY(c->closed);
        QTRY_COMPARE(gateway->stats().active, 0);
        auto fresh = connectVia("public.test:443");   // the next request works (the fake has no reconnect to do)
        QCOMPARE(fresh->status(), 200);
    }
    void hundredParallelTunnels() {
        startGateway();
        constexpr int N = 100;
        std::vector<std::unique_ptr<Client>> cs;
        for (int i = 0; i < N; ++i) {
            cs.push_back(std::make_unique<Client>());
            Client *c = cs.back().get();
            const QByteArray first = head("CONNECT", "public.test:443", auth) + "ping-" + QByteArray::number(i);
            QObject::connect(&c->sock, &QTcpSocket::connected, &c->sock, [c, first] { c->send(first); });
            c->sock.connectToHost(QHostAddress::LocalHost, gateway->port());
        }
        for (int i = 0; i < N; ++i) {
            Client *c = cs[i].get();
            const QByteArray want = "ping-" + QByteArray::number(i);
            QTRY_VERIFY_WITH_TIMEOUT(c->got.endsWith(want), 8000);
            QVERIFY(c->got.startsWith("HTTP/1.1 200"));
        }
        QCOMPARE(tunnel->execHosts.size(), 1);   // one resolution for the 100 concurrent connections
        QCOMPARE(tunnel->opened.size(), N);
        QCOMPARE(gateway->stats().channelsOpened, quint64(N));
        cs.clear();
        QTRY_COMPARE_WITH_TIMEOUT(gateway->stats().active, 0, 3000);
    }
    void backpressureStopsTheServerSideForASlowClient() {
        startGateway();
        auto c = connectVia("public.test:443");
        QCOMPARE(c->status(), 200);
        c->got.clear();
        c->sock.setReadBufferSize(32 * 1024);
        QObject::disconnect(&c->sock, &QTcpSocket::readyRead, nullptr, nullptr);   // the client stops reading
        QByteArray payload(6 * 1024 * 1024, 0);
        for (int i = 0; i < payload.size(); i += 4) { const quint32 r = QRandomGenerator::global()->generate(); memcpy(payload.data() + i, &r, 4); }
        const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
        c->send(payload);
        QTRY_VERIFY_WITH_TIMEOUT(tunnel->pauseCalls > 0, 8000);   // the gateway stopped reading the server: the SSH window would fill
        QByteArray received;
        QTimer pump;
        QObject::connect(&pump, &QTimer::timeout, &c->sock, [&] { received += c->sock.readAll(); });
        pump.start(2);
        QTRY_VERIFY_WITH_TIMEOUT(received.size() >= payload.size(), 20000);
        QCOMPARE(QCryptographicHash::hash(received.left(payload.size()), QCryptographicHash::Sha256), digest);
        QVERIFY(tunnel->resumeCalls > 0);
    }

    // ------------------------------------------------------------ address policy through the gateway
    void policyTable_data() {
        QTest::addColumn<QByteArray>("target");
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("asks");      // confirmations requested
        QTest::addColumn<int>("execs");     // names resolved on the server
        QTest::newRow("public name") << QByteArray("public.test:443") << 200 << 0 << 1;
        QTest::newRow("public literal") << QByteArray("8.8.8.8:443") << 502 << 0 << 0;   // allowed; the fake has no route
        QTest::newRow("loopback literal") << QByteArray("127.0.0.1:8099") << 403 << 1 << 0;
        QTest::newRow("private 10/8") << QByteArray("10.1.2.3:80") << 403 << 1 << 0;
        QTest::newRow("private 172.16/12") << QByteArray("172.31.0.1:80") << 403 << 1 << 0;
        QTest::newRow("private 192.168/16") << QByteArray("192.168.1.1:80") << 403 << 1 << 0;
        QTest::newRow("CGNAT") << QByteArray("100.64.0.1:80") << 403 << 1 << 0;
        QTest::newRow("ULA") << QByteArray("[fd12:3456::1]:80") << 403 << 1 << 0;
        QTest::newRow("v6 loopback") << QByteArray("[::1]:80") << 403 << 1 << 0;
        QTest::newRow("v4-mapped loopback") << QByteArray("[::ffff:127.0.0.1]:80") << 403 << 1 << 0;
        QTest::newRow("metadata") << QByteArray("169.254.169.254:80") << 403 << 0 << 0;
        QTest::newRow("metadata v4-mapped") << QByteArray("[::ffff:169.254.169.254]:80") << 403 << 0 << 0;
        QTest::newRow("metadata alibaba") << QByteArray("100.100.100.200:80") << 403 << 0 << 0;
        QTest::newRow("metadata v6") << QByteArray("[fd00:ec2::254]:80") << 403 << 0 << 0;
        QTest::newRow("link-local") << QByteArray("169.254.10.10:80") << 403 << 0 << 0;
        QTest::newRow("multicast") << QByteArray("224.0.0.1:80") << 403 << 0 << 0;
        QTest::newRow("unspecified") << QByteArray("0.0.0.0:80") << 403 << 0 << 0;
        QTest::newRow("broadcast") << QByteArray("255.255.255.255:80") << 403 << 0 << 0;
        QTest::newRow("decimal ip") << QByteArray("2130706433:80") << 403 << 0 << 0;
        QTest::newRow("hex ip") << QByteArray("0x7f000001:80") << 403 << 0 << 0;
        QTest::newRow("octal ip") << QByteArray("0177.0.0.1:80") << 403 << 0 << 0;
        QTest::newRow("short ip") << QByteArray("127.1:80") << 403 << 0 << 0;
        QTest::newRow("leading zero ip") << QByteArray("010.1.2.3:80") << 403 << 0 << 0;
        QTest::newRow("metadata name") << QByteArray("metadata.google.internal:80") << 403 << 0 << 0;
        QTest::newRow("name to metadata") << QByteArray("meta.test:80") << 403 << 0 << 1;
        QTest::newRow("name to loopback") << QByteArray("internal.test:80") << 403 << 1 << 1;
        QTest::newRow("name mixed public+internal") << QByteArray("mixed.test:80") << 403 << 1 << 1;
        QTest::newRow("unknown name") << QByteArray("nothing.test:80") << 502 << 0 << 1;
    }
    void policyTable() {
        QFETCH(QByteArray, target);
        QFETCH(int, status);
        QFETCH(int, asks);
        QFETCH(int, execs);
        startGateway();
        tunnel->routes.insert(QStringLiteral("8.8.8.8:443"), echo->port());
        tunnel->routes.remove(QStringLiteral("8.8.8.8:443"));
        answer = false;   // the user says no to every question
        auto c = connectVia(target);
        QCOMPARE(c->status(), status);
        QCOMPARE(asked, asks);
        QCOMPARE(tunnel->execHosts.size(), execs);
        if (status == 403) {
            QCOMPARE(tunnel->opened.size(), 0);   // nothing was opened on the server for a refused target
        }
    }
    void internalAddressWorksAfterConfirmationAndAsksOnce() {
        startGateway();
        answer = true;
        auto first = connectVia("127.0.0.1:8099");
        QCOMPARE(first->status(), 200);
        QCOMPARE(asked, 1);
        QCOMPARE(questions.last().pinIp, QStringLiteral("127.0.0.1"));
        QCOMPARE(questions.last().port, 8099);
        auto second = connectVia("127.0.0.1:8099");
        QCOMPARE(second->status(), 200);
        QCOMPARE(asked, 1);   // remembered for that host:port
        tunnel->routes.insert(QStringLiteral("127.0.0.1:8100"), web->port());
        auto otherPort = connectVia("127.0.0.1:8100");
        QCOMPARE(otherPort->status(), 200);
        QCOMPARE(asked, 2);   // another port is another question
        gateway->forgetConfirmations();
        auto afterForget = connectVia("127.0.0.1:8099");
        QCOMPARE(afterForget->status(), 200);
        QCOMPARE(asked, 3);
    }
    void declinedInternalAddressIsNeverOpened() {
        startGateway();
        answer = false;
        auto c = connectVia("127.0.0.1:8099");
        QCOMPARE(c->status(), 403);
        QVERIFY(c->got.contains("Refusé par l'utilisateur"));
        QCOMPARE(tunnel->opened.size(), 0);
        QCOMPARE(gateway->stats().internalDenied, quint64(1));
    }
    void noConfirmHandlerMeansNo() {
        gateway = new ProxyGateway(tunnel);
        QVERIFY(gateway->start());
        auth = basicAuthValue("fortiq", gateway->token().toLatin1());
        auto c = connectVia("127.0.0.1:8099");
        QCOMPARE(c->status(), 403);
        QCOMPARE(tunnel->opened.size(), 0);
    }
    void asynchronousConfirmationIsCoalesced() {
        gateway = new ProxyGateway(tunnel);
        QVERIFY(gateway->start());
        auth = basicAuthValue("fortiq", gateway->token().toLatin1());
        std::function<void(bool)> pending;
        gateway->setConfirmHandler([&](const InternalAccessRequest &, std::function<void(bool)> reply) { ++asked; pending = reply; });
        auto a = std::make_unique<Client>();
        auto b = std::make_unique<Client>();
        QVERIFY(a->connectTo(gateway->port()) && b->connectTo(gateway->port()));
        a->send(head("CONNECT", "127.0.0.1:8099", auth));
        b->send(head("CONNECT", "127.0.0.1:8099", auth));
        QTRY_VERIFY(pending != nullptr);
        QTest::qWait(100);
        QCOMPARE(asked, 1);   // one question for two requests to the same host:port
        pending(true);
        QTRY_VERIFY(a->got.contains("200") && b->got.contains("200"));
    }
    void confirmationIsBoundToTheAddressNotTheName() {
        startGateway();
        answer = true;
        tunnel->routes.insert(QStringLiteral("127.0.0.2:80"), web->port());
        GatewayOptions o;
        gateway->stop();
        o.resolveCacheMs = 0;
        QVERIFY(gateway->start(o));
        auth = basicAuthValue("fortiq", gateway->token().toLatin1());
        tunnel->dns[QStringLiteral("shifty.test")] = { QStringLiteral("127.0.0.1\n"), QStringLiteral("127.0.0.2\n") };
        tunnel->routes.insert(QStringLiteral("127.0.0.1:80"), web->port());
        auto first = connectVia("shifty.test:80");
        QCOMPARE(first->status(), 200);
        QCOMPARE(asked, 1);
        auto second = connectVia("shifty.test:80");   // the name now points to 127.0.0.2: the old confirmation does not cover it
        QCOMPARE(second->status(), 200);
        QCOMPARE(asked, 2);
        QCOMPARE(questions.last().pinIp, QStringLiteral("127.0.0.2"));
    }
    void dnsRebindingChannelGoesToThePinnedAddress() {
        // The name answers "public" for the policy check; the channel must be opened to THAT numeric address, never to the name.
        GatewayOptions o;
        o.resolveCacheMs = 0;
        startGateway(o);
        tunnel->dns[QStringLiteral("flip.test")] = { QStringLiteral("93.184.216.34\n"), QStringLiteral("127.0.0.1\n") };
        auto c = connectVia("flip.test:443");
        QCOMPARE(c->status(), 200);
        QCOMPARE(tunnel->opened, QStringList { QStringLiteral("93.184.216.34:443") });
        for (const QString &o2 : tunnel->opened) QVERIFY2(!o2.contains(QStringLiteral("flip")), "a name must never reach the tunnel");
        auto next = connectVia("flip.test:443");     // the second resolution says 127.0.0.1: internal, asked and refused
        QCOMPARE(next->status(), 403);
        QCOMPARE(asked, 1);
        QCOMPARE(tunnel->opened.size(), 1);
    }
    void resolutionIsCachedBrieflyAndFailuresAreReported() {
        startGateway();
        auto a = connectVia("public.test:443");
        auto b = connectVia("public.test:443");
        QCOMPARE(a->status(), 200);
        QCOMPARE(b->status(), 200);
        QCOMPARE(tunnel->execHosts.size(), 1);
        tunnel->execFails = true;
        auto c = connectVia("another.test:443");
        QCOMPARE(c->status(), 502);
        QVERIFY(c->got.contains("Résolution du nom impossible"));
    }
    void blockedTargetIsReportedToTheUi() {
        startGateway();
        QSignalSpy spy(gateway, &ProxyGateway::connectionBlocked);
        auto c = connectVia("169.254.169.254:80");
        QCOMPARE(c->status(), 403);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("169.254.169.254"));
        QCOMPARE(asked, 0);   // no question is ever asked for metadata
    }

    // ------------------------------------------------------------ plain HTTP (absolute-form)
    void httpGetBecomesOriginFormAndHidesTheProxy() {
        startGateway();
        auto c = request(head("GET", "http://public.test/headers?a=1", auth, "Accept: text/html\r\nProxy-Connection: keep-alive\r\nConnection: close\r\n"));
        QCOMPARE(c->status(), 200);
        QVERIFY(c->body().startsWith("GET /headers?a=1 HTTP/1.1\r\nHost: public.test\r\n"));
        QVERIFY(c->body().contains("Accept: text/html"));
        QVERIFY(!c->body().toLower().contains("proxy-"));
        QVERIFY(!c->body().contains(gateway->token().toLatin1()));
        QVERIFY(!c->body().contains(auth.mid(6)));
        QVERIFY(c->body().contains("Connection: close"));
        QCOMPARE(tunnel->opened, QStringList { QStringLiteral("93.184.216.34:80") });
    }
    void httpKeepAliveServesSeveralRequestsOnOneConnection() {
        startGateway();
        Client c;
        QVERIFY(c.connectTo(gateway->port()));
        for (int i = 0; i < 3; ++i) {
            c.got.clear();
            c.send(head("GET", "http://public.test/hello", auth));
            QTRY_VERIFY_WITH_TIMEOUT(c.got.endsWith("hello"), 4000);
            QVERIFY(c.got.contains("Connection: keep-alive"));
            QVERIFY(!c.closed);
        }
        QCOMPARE(tunnel->opened.size(), 3);                 // one channel per request
        QCOMPARE(tunnel->execHosts.size(), 1);
        c.got.clear();
        c.send(head("GET", "http://public.test/hello", auth, "Connection: close\r\n"));
        QTRY_VERIFY_WITH_TIMEOUT(c.closed, 4000);
        QVERIFY(c.got.contains("Connection: close"));
    }
    void httpPipelinedRequestsAreAnsweredInOrder() {
        startGateway();
        Client c;
        QVERIFY(c.connectTo(gateway->port()));
        c.send(head("GET", "http://public.test/hello", auth) + head("GET", "http://public.test/nocontent", auth, "Connection: close\r\n"));
        QTRY_VERIFY_WITH_TIMEOUT(c.closed, 6000);
        const qsizetype first = c.got.indexOf("HTTP/1.1 200");
        const qsizetype second = c.got.indexOf("HTTP/1.1 204");
        QVERIFY(first >= 0 && second > first);
    }
    void httpChunkedResponseIsDelimitedAndKeepAliveSurvives() {
        startGateway();
        Client c;
        QVERIFY(c.connectTo(gateway->port()));
        c.send(head("GET", "http://public.test/chunked", auth));
        QTRY_VERIFY_WITH_TIMEOUT(c.got.endsWith("0\r\nX-Trailer: yes\r\n\r\n"), 4000);
        QVERIFY(c.got.contains("Connection: keep-alive"));
        QVERIFY(!c.closed);
        c.got.clear();
        c.send(head("GET", "http://public.test/hello", auth));
        QTRY_VERIFY_WITH_TIMEOUT(c.got.endsWith("hello"), 4000);
    }
    void httpCloseDelimitedResponseClosesTheClient() {
        startGateway();
        auto c = request(head("GET", "http://public.test/close", auth));
        QVERIFY(c->closed);
        QVERIFY(c->got.endsWith("streamed-until-close"));
        QVERIFY(c->got.contains("Connection: close"));
    }
    void httpHeadAndNoContentHaveNoBody() {
        startGateway();
        Client c;
        QVERIFY(c.connectTo(gateway->port()));
        c.send(head("HEAD", "http://public.test/hello", auth));
        QTRY_VERIFY_WITH_TIMEOUT(c.got.contains("\r\n\r\n"), 4000);
        QVERIFY(c.got.contains("Content-Length: 5"));
        QVERIFY(!c.closed);
        c.got.clear();
        c.send(head("GET", "http://public.test/nocontent", auth));
        QTRY_VERIFY_WITH_TIMEOUT(c.got.contains("204"), 4000);
        c.got.clear();
        c.send(head("GET", "http://public.test/hello", auth));   // the connection is still in sync
        QTRY_VERIFY_WITH_TIMEOUT(c.got.endsWith("hello"), 4000);
    }
    void httpInterimResponseIsPassedOn() {
        startGateway();
        auto c = request(head("GET", "http://public.test/continue", auth, "Connection: close\r\n"));
        QVERIFY(c->got.startsWith("HTTP/1.1 100 Continue"));
        QVERIFY(c->got.contains("200 OK") && c->got.endsWith("after-continue"));
    }
    void httpUpgradeResponseIsRefused() {
        startGateway();
        auto c = request(head("GET", "http://public.test/upgrade", auth));
        QCOMPARE(c->status(), 502);
    }
    void httpPostBodiesContentLengthAndChunked() {
        startGateway();
        Client c;
        QVERIFY(c.connectTo(gateway->port()));
        c.send(head("POST", "http://public.test/echo", auth, "Content-Length: 11\r\n") + "hello world");
        QTRY_VERIFY_WITH_TIMEOUT(c.got.endsWith("hello world"), 4000);
        c.got.clear();
        c.send(head("POST", "http://public.test/echo", auth, "Transfer-Encoding: chunked\r\n") + "5\r\nabcde\r\n3\r\nfgh\r\n0\r\n\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(c.got.endsWith("abcdefgh"), 4000);
        QVERIFY(web->heads.last().toLower().contains("transfer-encoding: chunked"));
        // a body split over several packets and a request body that is larger than the channel high-water mark
        c.got.clear();
        const QByteArray big(2 * 1024 * 1024, 'p');
        c.send(head("POST", "http://public.test/echo", auth, "Content-Length: " + QByteArray::number(big.size()) + "\r\n"));
        for (qsizetype i = 0; i < big.size(); i += 100000) c.send(big.mid(i, 100000));
        QTRY_VERIFY_WITH_TIMEOUT(c.got.size() > big.size(), 15000);
        QVERIFY(c.got.endsWith(big.right(10)));
    }
    void httpBigResponseStreamsThrough() {
        startGateway();
        auto c = request(head("GET", "http://public.test/big", auth, "Connection: close\r\n"));
        QVERIFY(c->closed);
        QCOMPARE(c->body().size(), qsizetype(3 * 1024 * 1024));
    }
    void httpTruncatedResponseIsABrokenConnection() {
        startGateway();
        auto c = request(head("GET", "http://public.test/cut", auth));
        QVERIFY(c->closed);
        QVERIFY2(c->body().size() < 100000, "a short body must not look like a complete answer: the connection is cut");
        QVERIFY(c->sock.error() != QAbstractSocket::UnknownSocketError || c->body().size() < 100000);
    }
    void httpToInternalAddressNeedsConfirmationToo() {
        startGateway();
        auto denied = request(head("GET", "http://127.0.0.1:8099/hello", auth, "Connection: close\r\n"));
        QCOMPARE(denied->status(), 403);
        answer = true;
        auto allowed = request(head("GET", "http://127.0.0.1:8099/hello", auth, "Connection: close\r\n"));
        QCOMPARE(allowed->status(), 200);
        QVERIFY(allowed->got.endsWith("hello"));
    }

    // ------------------------------------------------------------ "no direct exits"
    void gatewaySourcesCreateNoOutgoingSocket() {
        // The gateway and the wire code may only ACCEPT local connections and talk to the tunnel. The tunnel creates exactly one
        // outgoing socket: the TCP connection to the SSH server of the ConnectionRequest.
        const QDir dir(QStringLiteral(FORTIQ_SRC_DIR) + QStringLiteral("/browser/gateway"));
        QVERIFY2(dir.exists(), qPrintable(dir.path()));
        const QStringList banned = { "connectToHost", "QNetworkAccessManager", "QNetworkRequest", "QUdpSocket", "QSslSocket", "QHostInfo", "QDnsLookup",
                                     "QWebSocket", "QLocalSocket", "::connect(", "WSAConnect", "getaddrinfo", "QProcess", "std::system", "popen" };
        int files = 0;
        for (const QFileInfo &fi : dir.entryInfoList({ "*.cpp", "*.h" }, QDir::Files)) {
            QFile f(fi.absoluteFilePath());
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(f.readAll());
            ++files;
            for (const QString &b : banned) {
                int count = 0;
                for (qsizetype i = text.indexOf(b); i >= 0; i = text.indexOf(b, i + 1)) {
                    // comments describing the rule are fine
                    const qsizetype lineStart = text.lastIndexOf(QLatin1Char('\n'), i) + 1;
                    const QString line = text.mid(lineStart, text.indexOf(QLatin1Char('\n'), i) - lineStart).trimmed();
                    if (!line.startsWith(QLatin1String("//"))) ++count;
                }
                const bool tunnel = fi.fileName() == QLatin1String("SshTunnel.cpp");
                const int allowed = (tunnel && b == QLatin1String("connectToHost")) ? 1 : 0;
                QVERIFY2(count == allowed, qPrintable(QStringLiteral("%1 uses %2 %3 time(s)").arg(fi.fileName(), b).arg(count)));
            }
        }
        QVERIFY(files >= 6);
    }
};

QTEST_MAIN(GatewayTest)
#include "GatewayTest.moc"
