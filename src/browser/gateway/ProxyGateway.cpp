#include "ProxyGateway.h"

#include "HttpWire.h"
#include "../UrlPolicy.h"

#include <QDateTime>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QTcpSocket>

#include <algorithm>
#include <memory>

namespace fortiq::gateway {

namespace {
constexpr qint64 kHighWater = 512 * 1024;   // bytes in flight in one direction before the other side is paused
constexpr qint64 kLowWater = 128 * 1024;
constexpr qint64 kEarlyBytes = 64 * 1024;   // what a client may send before the channel is open
constexpr qint64 kClientReadBuffer = 256 * 1024;
qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
QString hostPort(const QString &host, int port) {
    return (host.contains(QLatin1Char(':')) ? QStringLiteral("[%1]").arg(host) : host) + QLatin1Char(':') + QString::number(port);
}
}  // namespace

struct ProxyGateway::Conn {
    enum class S { Head, Resolving, Opening, Tunnel, SendBody, RespHead, RespBody, Closing };
    quint64 id = 0;
    QTcpSocket *sock = nullptr;
    QByteArray in;                  // bytes read from the client and not consumed yet
    S s = S::Head;
    RequestHead req;
    HttpTarget target;
    QString host;
    int port = 0;
    bool isConnect = false;
    bool keepAlive = false;
    int served = 0;                 // requests completed on this connection
    quint64 chan = 0;
    qint64 inflight = 0;            // bytes given to the tunnel, not yet taken by SSH
    bool clientPaused = false;      // we stopped reading the client (the tunnel is behind)
    bool serverPaused = false;      // we stopped reading the server (the client is behind)
    BodyFramer reqBody, respBody;
    QByteArray respBuf;
    bool respUntilClose = false;
    bool headSent = false;          // some response bytes already went to the client
    qint64 lastActivity = 0, since = 0;
    bool dead = false;
};

ProxyGateway::ProxyGateway(TunnelBackend *tunnel, QObject *parent) : QObject(parent), m_tunnel(tunnel) {
    connect(&m_server, &QTcpServer::newConnection, this, &ProxyGateway::onNewConnection);
    m_sweep.setInterval(1000);
    connect(&m_sweep, &QTimer::timeout, this, &ProxyGateway::sweep);
    connect(tunnel, &TunnelBackend::channelOpened, this, &ProxyGateway::onChannelOpened);
    connect(tunnel, &TunnelBackend::channelFailed, this, &ProxyGateway::onChannelFailed);
    connect(tunnel, &TunnelBackend::channelData, this, &ProxyGateway::onChannelData);
    connect(tunnel, &TunnelBackend::channelWritten, this, &ProxyGateway::onChannelWritten);
    connect(tunnel, &TunnelBackend::channelClosed, this, &ProxyGateway::onChannelClosed);
    connect(tunnel, &TunnelBackend::execFinished, this, &ProxyGateway::onExecFinished);
}

ProxyGateway::~ProxyGateway() { stop(); }

bool ProxyGateway::start(const GatewayOptions &options) {
    if (m_server.isListening()) return true;
    m_options = options;
    QByteArray raw;
    for (int i = 0; i < 6; ++i) {
        const quint32 r = QRandomGenerator::system()->generate();
        raw.append(reinterpret_cast<const char *>(&r), sizeof(r));
    }
    m_token = QString::fromLatin1(raw.toHex());
    wipe(raw);
    m_expectedAuth = basicAuthValue("fortiq", m_token.toLatin1());
    m_server.setMaxPendingConnections(128);
    if (!m_server.listen(QHostAddress::LocalHost, 0)) return false;   // 127.0.0.1 only, never another interface
    m_sweep.start();
    return true;
}

void ProxyGateway::stop() {
    m_sweep.stop();
    m_server.close();
    const QList<Conn *> all = m_conns.values();
    for (Conn *c : all) destroy(c, true);
}

bool ProxyGateway::isListening() const { return m_server.isListening(); }
quint16 ProxyGateway::port() const { return m_server.serverPort(); }
QString ProxyGateway::token() const { return m_token; }
GatewayStats ProxyGateway::stats() const { GatewayStats s = m_stats; s.active = static_cast<int>(m_conns.size()); return s; }
void ProxyGateway::forgetConfirmations() { m_approved.clear(); }

// ---------------------------------------------------------------- accepting
void ProxyGateway::onNewConnection() {
    while (QTcpSocket *s = m_server.nextPendingConnection()) {
        if (!s->peerAddress().isLoopback() || m_conns.size() >= m_options.maxConnections) {
            if (s->peerAddress().isLoopback()) ++m_stats.limitRejected;
            s->write(errorResponse(503, QStringLiteral("Trop de connexions à la passerelle FORTIQ.")));
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            // Closing a socket that still holds unread bytes resets the connection and the client loses the answer: let the request
            // arrive, discard it, then close.
            QTimer::singleShot(100, s, [s] { s->readAll(); s->disconnectFromHost(); });
            QTimer::singleShot(5000, s, [s] { s->abort(); });
            continue;
        }
        ++m_stats.accepted;
        auto *c = new Conn;
        c->id = m_nextConn++;
        c->sock = s;
        c->lastActivity = c->since = nowMs();
        s->setParent(this);
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        s->setReadBufferSize(kClientReadBuffer);
        m_conns.insert(c->id, c);
        const quint64 id = c->id;
        connect(s, &QTcpSocket::readyRead, this, [this, id] { if (Conn *x = m_conns.value(id)) onClientData(x); });
        connect(s, &QTcpSocket::bytesWritten, this, [this, id] { if (Conn *x = m_conns.value(id)) onClientWritten(x); });
        connect(s, &QTcpSocket::disconnected, this, [this, id] { if (Conn *x = m_conns.value(id)) onClientGone(x); });
        if (s->bytesAvailable() > 0) onClientData(c);
    }
}

// ---------------------------------------------------------------- the client talks
void ProxyGateway::onClientData(Conn *c) {
    if (c->dead) return;
    c->lastActivity = nowMs();
    switch (c->s) {
    case Conn::S::Head:
        c->in += c->sock->readAll();
        processHead(c);
        break;
    case Conn::S::Resolving: case Conn::S::Opening: case Conn::S::RespHead: case Conn::S::RespBody:
        // Early bytes (a CONNECT client that did not wait for 200) or a pipelined request: keep a bounded amount, the rest waits
        // in the socket buffer and the TCP window closes.
        if (c->in.size() < kEarlyBytes) c->in += c->sock->read(kEarlyBytes - c->in.size());
        break;
    case Conn::S::Tunnel: pumpTunnel(c); break;
    case Conn::S::SendBody: pumpBody(c); break;
    case Conn::S::Closing: c->sock->readAll(); break;
    }
}

void ProxyGateway::processHead(Conn *c) {
    RequestHead head;
    const HeadParse p = parseRequestHead(c->in, &head);
    switch (p.status) {
    case ParseStatus::NeedMore: return;
    case ParseStatus::TooLarge: ++m_stats.malformed; reply(c, 431, p.error); return;
    case ParseStatus::Bad: ++m_stats.malformed; reply(c, 400, p.error); return;
    case ParseStatus::Ok: break;
    }
    c->in.remove(0, p.consumed);
    handleRequest(c, std::move(head));
}

void ProxyGateway::handleRequest(Conn *c, RequestHead head) {
    if (!proxyAuthorized(&head, m_expectedAuth)) {
        ++m_stats.authRejected;
        reply(c, 407, QStringLiteral("Authentification de la passerelle FORTIQ requise."),
              { QByteArray("Proxy-Authenticate: Basic realm=\"FORTIQ\"") });
        return;
    }
    c->req = head;
    c->isConnect = head.method == "CONNECT";
    c->respBuf.clear();
    c->headSent = false;
    if (c->isConnect) {
        const Authority a = parseAuthority(head.target, 0);
        if (!a.ok) { ++m_stats.malformed; reply(c, 400, a.error); return; }
        c->host = a.host;
        c->port = a.port;
        c->keepAlive = false;
    } else {
        if (head.target.startsWith('/') || head.target == "*") {
            ++m_stats.malformed;
            reply(c, 400, QStringLiteral("Passerelle FORTIQ : ceci est un proxy HTTP, pas un serveur web."));
            return;
        }
        c->target = parseAbsoluteHttp(head.target);
        if (!c->target.ok) { ++m_stats.malformed; reply(c, 400, c->target.error); return; }
        if (head.hasToken("Connection", "upgrade") || head.count("Upgrade") > 0) {
            reply(c, 501, QStringLiteral("Upgrade (WebSocket) hors CONNECT non pris en charge."));
            return;
        }
        const Framing f = requestFraming(head);
        if (!f.ok) { ++m_stats.malformed; reply(c, 400, f.error); return; }
        c->reqBody.reset(f.mode, f.length);
        c->host = c->target.host;
        c->port = c->target.port;
        const bool http11 = head.version == "HTTP/1.1";
        c->keepAlive = http11 ? !head.hasToken("Connection", "close") : (head.hasToken("Connection", "keep-alive"));
    }
    c->s = Conn::S::Resolving;
    c->since = nowMs();
    const quint64 id = c->id;
    evaluate(c->host, c->port, [this, id](Verdict v) {
        Conn *x = m_conns.value(id);
        if (!x || x->dead || x->s != Conn::S::Resolving) return;
        startOpen(x, v);
    });
}

void ProxyGateway::startOpen(Conn *c, const Verdict &v) {
    const QString target = hostPort(c->host, c->port);
    if (v.kind == Verdict::Kind::Block) {
        ++m_stats.blocked;
        emit log(QStringLiteral("%1 %2 -> refusé").arg(QString::fromLatin1(c->req.method), target));
        emit connectionBlocked(c->host, c->port, v.message);
        reply(c, 403, v.message);
        return;
    }
    if (v.kind == Verdict::Kind::Unavailable) {
        emit log(QStringLiteral("%1 %2 -> indisponible").arg(QString::fromLatin1(c->req.method), target));
        reply(c, 502, v.message);
        return;
    }
    emit log(QStringLiteral("%1 %2 -> %3").arg(QString::fromLatin1(c->req.method), target, v.ip));
    c->s = Conn::S::Opening;
    c->since = nowMs();
    c->chan = m_tunnel->openChannel(v.ip, c->port);
    m_byChan.insert(c->chan, c);
}

// ---------------------------------------------------------------- address policy (the single point of control)
void ProxyGateway::evaluate(const QString &host, int port, std::function<void(Verdict)> done) {
    const QString written = host.contains(QLatin1Char(':')) ? QStringLiteral("[%1]").arg(host) : host;
    const HostPlan plan = planHost(QStringLiteral("http://%1:%2/").arg(written).arg(port));
    if (!plan.ok) {
        Verdict v; v.kind = Verdict::Kind::Block; v.message = plan.error;
        done(v);
        return;
    }
    // QUrl normalizes 2130706433, 0x7f000001, 0177.0.0.1, 127.1 ... to dotted decimal: what the browser wrote must already BE that form.
    if (!host.contains(QLatin1Char(':')) && plan.host != host) {
        Verdict v; v.kind = Verdict::Kind::Block; v.message = QStringLiteral("Adresse IP non canonique (formes décimale, octale, hexadécimale ou abrégée refusées).");
        done(v);
        return;
    }
    if (plan.literalIp) { decide(plan.ip, port, { plan.ip }, std::move(done)); return; }
    const QString name = plan.host;
    resolveName(name, [this, name, port, done = std::move(done)](QStringList ips, QString problem) {
        if (ips.isEmpty()) {
            Verdict v; v.kind = Verdict::Kind::Unavailable;
            v.message = problem.isEmpty() ? QStringLiteral("Le serveur ne connaît pas le nom « %1 ».").arg(name)
                                          : QStringLiteral("Résolution du nom impossible sur le serveur : %1").arg(problem);
            done(v);
            return;
        }
        decide(name, port, ips, done);
    });
}

void ProxyGateway::decide(const QString &host, int port, const QStringList &ips, std::function<void(Verdict)> done) {
    const PolicyDecision d = decideAddresses(ips);
    Verdict v;
    if (d.kind == PolicyDecision::Kind::Block) { v.kind = Verdict::Kind::Block; v.message = d.reason; done(v); return; }
    if (d.kind == PolicyDecision::Kind::Allow) { v.kind = Verdict::Kind::Allow; v.ip = d.pinIp; done(v); return; }
    // Internal: the user confirms exactly the address that will be contacted, once per host:port (a different address asks again).
    const QString key = hostPort(host, port);
    if (m_approved.value(key) == d.pinIp) { v.kind = Verdict::Kind::Allow; v.ip = d.pinIp; done(v); return; }
    auto waiting = m_asking.find(key);
    if (waiting != m_asking.end()) {   // a question for this host:port is open: join it
        waiting->push_back(done);
        return;
    }
    if (!m_confirm) {
        ++m_stats.internalDenied;
        v.kind = Verdict::Kind::Block;
        v.message = d.reason + QStringLiteral(" Aucune confirmation n'est possible : refusé.");
        done(v);
        return;
    }
    ++m_stats.internalAsked;
    m_asking.insert(key, { done });
    InternalAccessRequest req;
    req.host = host; req.port = port; req.pinIp = d.pinIp; req.reason = d.reason; req.details = d.details;
    auto answered = std::make_shared<bool>(false);
    m_confirm(req, [this, key, pin = d.pinIp, reason = d.reason, answered](bool yes) {
        if (*answered) return;
        *answered = true;
        Verdict r;
        if (yes) { ++m_stats.internalAllowed; m_approved.insert(key, pin); r.kind = Verdict::Kind::Allow; r.ip = pin; }
        else { ++m_stats.internalDenied; r.kind = Verdict::Kind::Block; r.message = reason + QStringLiteral(" Refusé par l'utilisateur."); }
        const auto callbacks = m_asking.take(key);
        for (const auto &cb : callbacks) cb(r);
    });
}

void ProxyGateway::resolveName(const QString &host, std::function<void(QStringList, QString)> done) {
    const auto cached = m_dns.constFind(host);
    if (cached != m_dns.constEnd() && cached->expires > nowMs()) { done(cached->ips, QString()); return; }
    auto waiting = m_resolving.find(host);
    if (waiting != m_resolving.end()) { waiting->push_back(std::move(done)); return; }
    m_resolving.insert(host, { std::move(done) });
    m_execHost.insert(m_tunnel->exec(buildResolveScript(host), 8192), host);
}

void ProxyGateway::onExecFinished(quint64 id, bool ok, const QByteArray &output, const QString &problem) {
    const auto it = m_execHost.find(id);
    if (it == m_execHost.end()) return;
    const QString host = it.value();
    m_execHost.erase(it);
    QStringList ips;
    QString why;
    if (ok) {
        ips = parseResolveOutput(output, &why);
        if (!ips.isEmpty() && m_options.resolveCacheMs > 0) m_dns.insert(host, { ips, nowMs() + m_options.resolveCacheMs });
    } else {
        why = problem;
    }
    const auto callbacks = m_resolving.take(host);
    for (const auto &cb : callbacks) cb(ips, why);
}

// ---------------------------------------------------------------- the tunnel answers
void ProxyGateway::onChannelOpened(quint64 id) {
    Conn *c = m_byChan.value(id);
    if (!c || c->dead || c->s != Conn::S::Opening) return;
    ++m_stats.channelsOpened;
    c->since = nowMs();
    if (c->isConnect) {
        c->s = Conn::S::Tunnel;
        c->sock->write("HTTP/1.1 200 Connection Established\r\nProxy-Agent: FORTIQ\r\n\r\n");
        if (!c->in.isEmpty()) {
            const QByteArray early = std::move(c->in);
            c->in.clear();
            c->inflight += early.size();
            m_stats.bytesToServer += static_cast<quint64>(early.size());
            m_tunnel->write(id, early);
        }
        pumpTunnel(c);
        return;
    }
    c->s = Conn::S::SendBody;
    const QByteArray head = serializeOriginRequest(c->req, c->target);
    c->inflight += head.size();
    m_tunnel->write(id, head);
    pumpBody(c);
}

void ProxyGateway::onChannelFailed(quint64 id, const QString &message) {
    Conn *c = m_byChan.value(id);
    if (!c || c->dead) return;
    m_byChan.remove(id);
    c->chan = 0;
    reply(c, 502, QStringLiteral("Le serveur n'a pas pu ouvrir la connexion : %1").arg(message));
}

void ProxyGateway::onChannelWritten(quint64 id, qint64 bytes) {
    Conn *c = m_byChan.value(id);
    if (!c || c->dead) return;
    c->inflight = (std::max<qint64>)(0, c->inflight - bytes);
    if (c->clientPaused && c->inflight < kLowWater) {
        c->clientPaused = false;
        onClientData(c);   // whatever the socket buffered meanwhile
    }
}

void ProxyGateway::onChannelData(quint64 id, const QByteArray &data) {
    Conn *c = m_byChan.value(id);
    if (!c || c->dead) return;
    c->lastActivity = nowMs();
    m_stats.bytesFromServer += static_cast<quint64>(data.size());
    if (c->s == Conn::S::Tunnel) { toClient(c, data); return; }
    if (c->s == Conn::S::SendBody || c->s == Conn::S::RespHead || c->s == Conn::S::RespBody) { httpResponseData(c, data); return; }
}

void ProxyGateway::onChannelClosed(quint64 id, const QString &reason) {
    Conn *c = m_byChan.value(id);
    if (!c || c->dead) return;
    m_byChan.remove(id);
    c->chan = 0;
    switch (c->s) {
    case Conn::S::Tunnel:
        if (reason.isEmpty()) closeGracefully(c); else destroy(c, true);
        return;
    case Conn::S::RespBody:
        if (c->respUntilClose && reason.isEmpty()) { c->keepAlive = false; finishResponse(c); return; }
        destroy(c, true);   // truncated body: the client must see a broken connection, not a short but valid answer
        return;
    case Conn::S::SendBody: case Conn::S::RespHead:
        if (!c->headSent) reply(c, 502, reason.isEmpty() ? QStringLiteral("Le serveur a fermé la connexion sans répondre.")
                                                          : QStringLiteral("Connexion au serveur perdue : %1").arg(reason));
        else destroy(c, true);
        return;
    default:
        destroy(c, true);
        return;
    }
}

// ---------------------------------------------------------------- relays
void ProxyGateway::toClient(Conn *c, const QByteArray &data) {
    c->headSent = true;
    c->sock->write(data);
    if (!c->serverPaused && c->chan && c->sock->bytesToWrite() > kHighWater) {
        c->serverPaused = true;
        m_tunnel->setReading(c->chan, false);
    }
}

void ProxyGateway::onClientWritten(Conn *c) {
    if (c->dead) return;
    c->lastActivity = nowMs();
    if (c->serverPaused && c->chan && c->sock->bytesToWrite() < kLowWater) {
        c->serverPaused = false;
        m_tunnel->setReading(c->chan, true);
    }
}

void ProxyGateway::pumpTunnel(Conn *c) {
    if (c->clientPaused || !c->chan) return;
    const QByteArray data = c->sock->readAll();
    if (data.isEmpty()) return;
    c->inflight += data.size();
    m_stats.bytesToServer += static_cast<quint64>(data.size());
    m_tunnel->write(c->chan, data);
    if (c->inflight > kHighWater) c->clientPaused = true;
}

void ProxyGateway::onClientGone(Conn *c) {
    if (c->dead) return;
    destroy(c, false);
}

void ProxyGateway::pumpBody(Conn *c) {
    if (!c->chan) return;
    while (!c->reqBody.done()) {
        if (c->in.isEmpty()) {
            if (c->clientPaused) return;
            c->in = c->sock->readAll();
            if (c->in.isEmpty()) return;
        }
        const qsizetype n = c->reqBody.consume(c->in);
        if (c->reqBody.failed()) { ++m_stats.malformed; reply(c, 400, QStringLiteral("Corps de requête mal formé.")); return; }
        if (n > 0) {
            const QByteArray part = c->in.left(n);
            c->in.remove(0, n);
            c->inflight += part.size();
            m_stats.bytesToServer += static_cast<quint64>(part.size());
            m_tunnel->write(c->chan, part);
            if (c->inflight > kHighWater) { c->clientPaused = true; return; }
        } else {
            return;
        }
    }
    if (c->s == Conn::S::SendBody) c->s = Conn::S::RespHead;
}

void ProxyGateway::httpResponseData(Conn *c, const QByteArray &data) {
    if (c->s == Conn::S::RespBody) { bodyBytes(c, data); return; }
    c->respBuf += data;
    for (;;) {
        ResponseHead rh;
        const HeadParse p = parseResponseHead(c->respBuf, &rh);
        if (p.status == ParseStatus::NeedMore) return;
        if (p.status != ParseStatus::Ok) { reply(c, 502, QStringLiteral("Réponse du serveur distant illisible.")); return; }
        const QByteArray raw = c->respBuf.left(p.consumed);
        c->respBuf.remove(0, p.consumed);
        if (rh.status == 101) { reply(c, 502, QStringLiteral("Changement de protocole refusé.")); return; }
        if (rh.status >= 100 && rh.status < 200) {   // 100 Continue and friends: pass on, keep waiting for the real answer
            toClient(c, raw);
            continue;
        }
        const Framing f = responseFraming(rh, c->req.method);
        if (!f.ok) { reply(c, 502, f.error); return; }
        c->respBody.reset(f.mode, f.length);
        c->respUntilClose = f.mode == BodyFramer::Mode::UntilClose;
        // The connection can be reused only if the request is fully sent, the answer is delimited and the client asked for it.
        c->keepAlive = c->keepAlive && !c->respUntilClose && c->reqBody.done();
        c->s = Conn::S::RespBody;
        toClient(c, serializeResponseHead(rh, c->keepAlive));
        if (c->respBody.done()) { finishResponse(c); return; }
        if (!c->respBuf.isEmpty()) { const QByteArray rest = std::move(c->respBuf); c->respBuf.clear(); bodyBytes(c, rest); }
        return;
    }
}

void ProxyGateway::bodyBytes(Conn *c, const QByteArray &data) {
    const qsizetype n = c->respBody.consume(data);
    if (c->respBody.failed()) { destroy(c, true); return; }
    if (n > 0) toClient(c, n == data.size() ? data : data.left(n));
    if (c->respBody.done()) finishResponse(c);
}

void ProxyGateway::finishResponse(Conn *c) {
    releaseChannel(c);
    ++c->served;
    if (!c->keepAlive || c->dead || c->sock->state() != QAbstractSocket::ConnectedState) { closeGracefully(c); return; }
    c->s = Conn::S::Head;
    c->req = RequestHead();
    c->since = c->lastActivity = nowMs();
    c->inflight = 0;
    c->clientPaused = c->serverPaused = false;
    const quint64 id = c->id;
    QMetaObject::invokeMethod(this, [this, id] {   // a pipelined request, or bytes the socket buffered meanwhile
        if (Conn *x = m_conns.value(id)) if (x->s == Conn::S::Head && (!x->in.isEmpty() || x->sock->bytesAvailable() > 0)) onClientData(x);
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------- ending
void ProxyGateway::reply(Conn *c, int status, const QString &text, const QList<QByteArray> &extra) {
    if (c->dead) return;
    releaseChannel(c);
    c->sock->write(errorResponse(status, text, extra));
    closeGracefully(c);
}

void ProxyGateway::releaseChannel(Conn *c) {
    if (!c->chan) return;
    m_byChan.remove(c->chan);
    m_tunnel->closeChannel(c->chan);
    c->chan = 0;
}

void ProxyGateway::closeGracefully(Conn *c) {
    if (c->dead) return;
    releaseChannel(c);
    c->s = Conn::S::Closing;
    c->in.clear();
    c->sock->readAll();   // unread bytes at close time would turn the close into a reset and eat the answer
    c->sock->disconnectFromHost();   // flushes what is queued first; `disconnected` destroys the connection
    if (c->sock->state() == QAbstractSocket::UnconnectedState) destroy(c, false);
}

void ProxyGateway::destroy(Conn *c, bool abort) {
    if (c->dead) return;
    c->dead = true;
    releaseChannel(c);
    m_conns.remove(c->id);
    QTcpSocket *s = c->sock;
    s->disconnect(this);
    if (abort) s->abort(); else s->close();
    s->deleteLater();
    QMetaObject::invokeMethod(this, [c] { delete c; }, Qt::QueuedConnection);   // never inside one of its own handlers
}

void ProxyGateway::sweep() {
    const qint64 now = nowMs();
    const QList<Conn *> all = m_conns.values();
    for (Conn *c : all) {
        if (c->dead) continue;
        switch (c->s) {
        case Conn::S::Head:
            if (now - c->since > (c->served ? m_options.keepAliveIdleMs : m_options.headTimeoutMs)) destroy(c, true);
            break;
        case Conn::S::Resolving: case Conn::S::Opening:
            if (now - c->since > m_options.openTimeoutMs) reply(c, 504, QStringLiteral("Délai dépassé : le serveur n'a pas ouvert la connexion."));
            break;
        case Conn::S::Closing:
            if (now - c->lastActivity > 10000) destroy(c, true);
            break;
        default:
            if (now - c->lastActivity > m_options.idleTimeoutMs) destroy(c, true);
            break;
        }
    }
}

}  // namespace fortiq::gateway
