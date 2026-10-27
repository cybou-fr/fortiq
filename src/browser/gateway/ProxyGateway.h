#pragma once

#include "HttpWire.h"
#include "TunnelBackend.h"

#include "../../core/SecureWipe.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTcpServer>
#include <QTimer>
#include <QVariantList>

#include <functional>

namespace fortiq::gateway {

struct GatewayOptions {
    int maxConnections = 256;        // simultaneous client connections; more are answered 503
    int headTimeoutMs = 15000;       // first request head must arrive within this
    int keepAliveIdleMs = 30000;     // an idle HTTP connection between two requests
    int openTimeoutMs = 30000;       // name resolution + opening the channel
    int idleTimeoutMs = 120000;      // no byte either way (tunnels and transfers)
    quint16 preferredPort = 0;       // 0 = any free port; the engine reuses its first port on a restart (Chromium reads it once)
    QString realm = QStringLiteral("FORTIQ");   // Proxy-Authenticate realm; the Android WebView hands the password only to this (secret) realm
    int resolveCacheMs = 5000;       // how long a resolution is reused (0 = never; the PIN, not the cache, is the rebinding defence)
};

// What the user is asked before an INTERNAL address of the server (loopback, private network, ...) is contacted.
struct InternalAccessRequest {
    QString host;          // as the browser wrote it
    int port = 0;
    QString pinIp;         // the exact address that would be contacted
    QString reason;        // user-facing text
    QVariantList details;  // [{ip, label, cls}]
};

struct GatewayStats {
    quint64 accepted = 0, authRejected = 0, limitRejected = 0, malformed = 0;
    quint64 blocked = 0, internalAsked = 0, internalAllowed = 0, internalDenied = 0;
    quint64 channelsOpened = 0, bytesToServer = 0, bytesFromServer = 0;
    int active = 0;
};

// Local HTTP proxy for the Browser engine (docs/BROWSER.md, "Passerelle SSH"). The ONLY way out is the TunnelBackend (SSH to the
// selected server): this class never creates an outgoing socket. It listens on 127.0.0.1 on a random port, demands the session token
// (Proxy-Authorization: Basic fortiq:<token>) and is the single point of the address policy (UrlPolicy): the name is resolved ON THE
// SERVER, the answer is classified, the channel is opened to the PINNED numeric address, never to the name.
// Create, use and destroy it in the thread of the tunnel (QObject rules).
class ProxyGateway : public QObject {
    Q_OBJECT
public:
    // answer(true) = the user allows contacting `request.pinIp`; may be called later (asynchronously), once.
    using ConfirmHandler = std::function<void(const InternalAccessRequest &request, std::function<void(bool)> answer)>;

    explicit ProxyGateway(TunnelBackend *tunnel, QObject *parent = nullptr);   // does not own the tunnel
    ~ProxyGateway() override;

    bool start(const GatewayOptions &options = {});
    void stop();
    bool isListening() const;
    quint16 port() const;
    QString proxyUser() const { return QStringLiteral("fortiq"); }
    QString token() const;   // the session secret: hand it to the engine, never log it

    void setConfirmHandler(ConfirmHandler handler) { m_confirm = std::move(handler); }
    void forgetConfirmations();   // e.g. when the user leaves the Browser or switches server
    GatewayStats stats() const;

signals:
    void log(const QString &line);                                         // host:port and verdicts only; no secret, no path, no query
    void connectionBlocked(const QString &host, int port, const QString &reason);

private:
    struct Conn;
    struct Verdict {
        enum class Kind { Allow, Block, Unavailable } kind = Kind::Block;
        QString ip;        // Allow: the pinned numeric address
        QString message;   // Block / Unavailable: shown to the user
    };
    void onNewConnection();
    void onClientData(Conn *c);
    void onClientWritten(Conn *c);
    void onClientGone(Conn *c);
    void processHead(Conn *c);
    void handleRequest(Conn *c, RequestHead head);
    void evaluate(const QString &host, int port, std::function<void(Verdict)> done);
    void decide(const QString &host, int port, const QStringList &ips, std::function<void(Verdict)> done);
    void resolveName(const QString &host, std::function<void(QStringList, QString)> done);
    void startOpen(Conn *c, const Verdict &v);
    void onChannelOpened(quint64 id);
    void onChannelFailed(quint64 id, const QString &message);
    void onChannelData(quint64 id, const QByteArray &data);
    void onChannelWritten(quint64 id, qint64 bytes);
    void onChannelClosed(quint64 id, const QString &reason);
    void onExecFinished(quint64 id, bool ok, const QByteArray &output, const QString &problem);
    void pumpBody(Conn *c);
    void pumpTunnel(Conn *c);
    void httpResponseData(Conn *c, const QByteArray &data);
    void bodyBytes(Conn *c, const QByteArray &data);
    void finishResponse(Conn *c);
    void toClient(Conn *c, const QByteArray &data);
    void reply(Conn *c, int status, const QString &text, const QList<QByteArray> &extra = {});
    void closeGracefully(Conn *c);
    void destroy(Conn *c, bool abort);
    void releaseChannel(Conn *c);
    void sweep();

    TunnelBackend *m_tunnel;
    QTcpServer m_server;
    QTimer m_sweep;
    GatewayOptions m_options;
    SecretString m_token;
    SecretBytes m_expectedAuth;
    ConfirmHandler m_confirm;
    QHash<quint64, Conn *> m_conns;     // by connection id
    QHash<quint64, Conn *> m_byChan;    // by tunnel channel id
    quint64 m_nextConn = 1;
    GatewayStats m_stats;
    // name resolution: one script per name at a time, answers reused for resolveCacheMs
    struct DnsEntry { QStringList ips; qint64 expires = 0; };
    QHash<QString, DnsEntry> m_dns;
    QHash<QString, QList<std::function<void(QStringList, QString)>>> m_resolving;
    QHash<quint64, QString> m_execHost;
    // confirmations: "host:port" -> the address the user allowed
    QHash<QString, QString> m_approved;
    QHash<QString, QList<std::function<void(Verdict)>>> m_asking;
};

}  // namespace fortiq::gateway
