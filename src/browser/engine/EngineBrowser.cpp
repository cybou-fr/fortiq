#include "EngineBrowser.h"

namespace fortiq::engine {

namespace {
// Chromium reads the proxy port once per process (the switches are applied before the first profile): the first gateway port of
// this process is the only one the engine will ever use.
quint16 g_enginePort = 0;
constexpr int kLogLimit = 500;
}  // namespace

bool EngineBrowser::compiledIn() {
#if defined(FORTIQ_HAVE_WEBENGINE) || defined(FORTIQ_HAVE_ANDROID_WEBVIEW)
    return true;
#else
    return false;
#endif
}

EngineBrowser::EngineBrowser(Source source, QObject *parent) : QObject(parent), m_source(std::move(source)) {}

EngineBrowser::~EngineBrowser() {
    m_gateway.reset();   // the gateway goes first: it uses the tunnel
    m_tunnel.reset();
}

QVariantMap EngineBrowser::pendingConfirm() const {
    if (m_asks.isEmpty()) return {};
    const gateway::InternalAccessRequest &r = m_asks.constFirst().request;
    return { { "host", r.host }, { "port", r.port }, { "ip", r.pinIp }, { "reason", r.reason }, { "details", r.details } };
}

quint16 EngineBrowser::proxyPort() const { return m_gateway && m_gateway->isListening() ? m_gateway->port() : 0; }

QStringList EngineBrowser::currentSwitches() const { return chromiumSwitches(g_enginePort); }

void EngineBrowser::setWindowActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    if (!active) stop(QStringLiteral("idle"), QString());
    refresh();
}

void EngineBrowser::refresh() {
    if (!m_active) return;
    const bool connected = m_source.connected && m_source.connected();
    if (!connected) {
        if (m_state != QLatin1String("noserver"))
            stop(QStringLiteral("noserver"), QStringLiteral("Aucun serveur Linux connecté : le navigateur n'accède à aucun réseau."));
        else if (m_status.isEmpty()) {
            m_status = QStringLiteral("Aucun serveur Linux connecté : le navigateur n'accède à aucun réseau.");
            emit changed();
        }
        return;
    }
    const int token = m_source.token ? m_source.token() : 0;
    if (m_gateway && token != m_token) stop(QStringLiteral("idle"), QString());   // another connection: new tunnel, new secret
    if (!m_gateway) start();
}

void EngineBrowser::start() {
    m_token = m_source.token ? m_source.token() : 0;
    m_label = m_source.label ? m_source.label() : QString();
    m_tunnel.reset(m_source.makeTunnel ? m_source.makeTunnel(this) : nullptr);
    if (!m_tunnel) { stop(QStringLiteral("error"), QStringLiteral("Tunnel SSH indisponible.")); return; }
    m_gateway = std::make_unique<gateway::ProxyGateway>(m_tunnel.get());
    gateway::GatewayOptions options;
    options.preferredPort = g_enginePort;
    m_realm = newGatewayRealm();
    options.realm = m_realm;
    if (!m_gateway->start(options)) {
        stop(QStringLiteral("error"), g_enginePort
            ? QStringLiteral("Le port local %1 de la passerelle est pris : redémarrez FORTIQ pour utiliser le navigateur.").arg(g_enginePort)
            : QStringLiteral("La passerelle locale n'a pas pu démarrer."));
        return;
    }
    if (g_enginePort == 0) g_enginePort = m_gateway->port();
    QString problem;
#ifdef FORTIQ_HAVE_WEBENGINE
    if (!applyChromiumFlags(g_enginePort, &problem)) { stop(QStringLiteral("error"), problem); return; }
#endif
    m_log.clear();
    connect(m_gateway.get(), &gateway::ProxyGateway::log, this, [this](const QString &line) {
        m_log << line;
        if (m_log.size() > kLogLimit) m_log.removeFirst();
    });
    connect(m_gateway.get(), &gateway::ProxyGateway::connectionBlocked, this, [this](const QString &host, int port, const QString &reason) {
        emit notice(QStringLiteral("%1:%2 — %3").arg(host).arg(port).arg(reason));
    });
    m_gateway->setConfirmHandler([this](const gateway::InternalAccessRequest &request, std::function<void(bool)> answer) {
        m_asks.append({ request, std::move(answer) });
        if (m_asks.size() == 1) emit changed();
    });
    ++m_generation;
    m_state = QStringLiteral("ready");
    m_status = QStringLiteral("Pages chargées par %1, à travers le tunnel SSH.").arg(m_label);
    emit changed();
}

void EngineBrowser::stop(const QString &state, const QString &status) {
    const bool wasRunning = m_gateway != nullptr;
    // Pending questions are answered "no": nothing may stay open after the gateway is gone.
    const QList<Pending> asks = m_asks;
    m_asks.clear();
    for (const Pending &p : asks) if (p.answer) p.answer(false);
    m_gateway.reset();
    m_tunnel.reset();
    m_state = state;
    m_status = status;
    ++m_generation;
    if (wasRunning) emit engineMustStop();
    emit changed();
}

void EngineBrowser::answerConfirm(bool allow) {
    if (m_asks.isEmpty()) return;
    const Pending p = m_asks.takeFirst();
    if (p.answer) p.answer(allow);
    emit changed();
}

QVariantMap EngineBrowser::proxyCredentials(const QString &host, int port) const {
    if (!m_gateway || !isOurProxy(host, port, m_gateway->port())) return {};
    return { { "user", m_gateway->proxyUser() }, { "password", m_gateway->token() } };
}

QVariantMap EngineBrowser::judge(const QString &url, bool topLevel) {
    const NavigationVerdict v = judgeUrl(QUrl(url), topLevel);
    if (!v.allowed && topLevel) emit notice(v.reason);
    return { { "allowed", v.allowed }, { "reason", v.reason } };
}

void EngineBrowser::reportDownloadRefused(const QString &fileName) {
    emit notice(QStringLiteral("Téléchargement refusé (%1) : les téléchargements ne sont pas pris en charge par le navigateur à ce stade.").arg(fileName));
}

QVariantMap EngineBrowser::gatewayStats() const {
    if (!m_gateway) return {};
    const gateway::GatewayStats s = m_gateway->stats();
    return { { "accepted", qulonglong(s.accepted) }, { "authRejected", qulonglong(s.authRejected) }, { "blocked", qulonglong(s.blocked) },
             { "internalAsked", qulonglong(s.internalAsked) }, { "internalAllowed", qulonglong(s.internalAllowed) },
             { "internalDenied", qulonglong(s.internalDenied) }, { "channelsOpened", qulonglong(s.channelsOpened) },
             { "bytesToServer", qulonglong(s.bytesToServer) }, { "bytesFromServer", qulonglong(s.bytesFromServer) }, { "active", s.active } };
}

}  // namespace fortiq::engine
