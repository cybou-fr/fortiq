#pragma once

#include "../gateway/ProxyGateway.h"
#include "EngineProfile.h"

#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace fortiq {
class RemoteController;
}

namespace fortiq::engine {

// The side of the Browser engine that is NOT Chromium (docs/BROWSER.md, "Moteur"): it owns the SSH tunnel and the loopback gateway of
// the selected server, applies the Chromium switches, answers the proxy authentication and the internal-address question, and judges
// navigations. The web view itself is QML (qml/engine/); this class builds without Qt WebEngine so that it is testable everywhere.
//   - The engine is offered ("ready") only while a server is connected: without one nothing is started and nothing is reachable.
//   - Chromium reads its proxy port once per process: the first gateway port is kept and reused by a restart; if it cannot be
//     listened on again the state becomes "error" (the engine is never pointed at another port or at a direct route).
class EngineBrowser : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)           // Qt WebEngine is part of this build
    Q_PROPERTY(QString state READ state NOTIFY changed)          // noserver | ready | error
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString serverLabel READ serverLabel NOTIFY changed)
    Q_PROPERTY(QVariantMap pendingConfirm READ pendingConfirm NOTIFY changed)   // {host, port, ip, reason, details}; empty when nothing is asked
    Q_PROPERTY(int proxyPort READ proxyPortInt NOTIFY changed)      // the gateway's loopback port (0 when stopped)
    Q_PROPERTY(int generation READ generation NOTIFY changed)     // grows with every new gateway: views of an older one are dropped
public:
    // Test hook: a source of tunnels that does not need a RemoteController.
    struct Source {
        std::function<bool()> connected;
        std::function<gateway::TunnelBackend *(QObject *parent)> makeTunnel;   // created in the thread of this object
        std::function<QString()> label;
        std::function<int()> token;   // changes at every new connection
    };
    explicit EngineBrowser(RemoteController *remote, QObject *parent = nullptr);
    explicit EngineBrowser(Source source, QObject *parent = nullptr);
    ~EngineBrowser() override;

    static bool compiledIn();    // FORTIQ_HAVE_WEBENGINE
    bool available() const { return compiledIn(); }
    QString state() const { return m_state; }
    QString status() const { return m_status; }
    QString serverLabel() const { return m_label; }
    QVariantMap pendingConfirm() const;
    int generation() const { return m_generation; }

    // The window asks for the engine (true) or leaves it (false). Starting needs a connected server.
    Q_INVOKABLE void setWindowActive(bool active);
    Q_INVOKABLE void refresh();               // the source changed (connection state, token)
    Q_INVOKABLE void answerConfirm(bool allow);
    // The proxy credentials, ONLY for our own gateway ({} for any other proxy or origin server).
    Q_INVOKABLE QVariantMap proxyCredentials(const QString &host, int port) const;
    QString proxyRealm() const { return m_realm; }     // the secret realm of this gateway session (Android WebView: 401/407 challenge check)
    // {allowed, reason}; a refusal is also reported with notice().
    Q_INVOKABLE QVariantMap judge(const QString &url, bool topLevel);
    Q_INVOKABLE QString permissionRefusal() const { return engine::permissionRefusal(); }
    Q_INVOKABLE void reportDownloadRefused(const QString &fileName);

    quint16 proxyPort() const;
    int proxyPortInt() const { return proxyPort(); }
    QStringList currentSwitches() const;
    // Counters and the host:port lines the gateway reported (tests: "every request went through the gateway").
    QVariantMap gatewayStats() const;
    QStringList gatewayLog() const { return m_log; }
    gateway::ProxyGateway *gateway() const { return m_gateway.get(); }

signals:
    void changed();
    void notice(const QString &text);    // short message for the window (refused address, download refused...)
    void engineMustStop();                // the gateway went away: the views must be destroyed

private:
    void start();
    void stop(const QString &state, const QString &status);
    void askNext();

    Source m_source;
    RemoteController *m_remote = nullptr;
    bool m_active = false;
    QString m_state = QStringLiteral("noserver");
    QString m_status;
    QString m_label;
    int m_generation = 0;
    int m_token = 0;
    QString m_realm;
    std::unique_ptr<gateway::TunnelBackend> m_tunnel;
    std::unique_ptr<gateway::ProxyGateway> m_gateway;
    QStringList m_log;
    struct Pending {
        gateway::InternalAccessRequest request;
        std::function<void(bool)> answer;
    };
    QList<Pending> m_asks;
    bool m_asking = false;
};

}  // namespace fortiq::engine
