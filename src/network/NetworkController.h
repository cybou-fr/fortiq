#pragma once

#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>

#include <atomic>
#include <memory>

namespace fortiq {
class RemoteController;
class SshSession;

// Network app (read-only): interfaces, routes, listening ports and DNS of the
// selected server, and three diagnostics (resolve, ping, trace) run there. Reads
// and runs only; nothing is changed on the server. Bound to the server session:
// a new server clears everything. See docs/NETWORK.md.
class NetworkController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)         // disconnected | loading | ready | error
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString updated READ updated NOTIFY changed)
    Q_PROPERTY(QString hostname READ hostname NOTIFY changed)
    Q_PROPERTY(QVariantList interfaces READ interfaces NOTIFY changed)
    Q_PROPERTY(QVariantList routes READ routes NOTIFY changed)
    Q_PROPERTY(QVariantList listeners READ listeners NOTIFY changed)
    Q_PROPERTY(int exposedCount READ exposedCount NOTIFY changed)   // listeners reachable from outside the host
    Q_PROPERTY(QStringList notes READ notes NOTIFY changed)
    Q_PROPERTY(QStringList dnsServers READ dnsServers NOTIFY changed)
    Q_PROPERTY(QStringList dnsSearch READ dnsSearch NOTIFY changed)
    Q_PROPERTY(QString dnsNote READ dnsNote NOTIFY changed)
    Q_PROPERTY(QString resolvedText READ resolvedText NOTIFY changed)
    Q_PROPERTY(QStringList tools READ tools NOTIFY changed)
    Q_PROPERTY(bool probeBusy READ probeBusy NOTIFY probeChanged)
    Q_PROPERTY(QString probeKind READ probeKind NOTIFY probeChanged)
    Q_PROPERTY(QString probeTarget READ probeTarget NOTIFY probeChanged)
    Q_PROPERTY(QString probeOutput READ probeOutput NOTIFY probeChanged)
    Q_PROPERTY(QString probeStatus READ probeStatus NOTIFY probeChanged)

public:
    explicit NetworkController(RemoteController *remote, QObject *parent = nullptr);
    ~NetworkController() override;

    bool busy() const { return m_busy; }
    QString state() const { return m_state; }
    QString status() const { return m_status; }
    QString updated() const { return m_updated; }
    QString hostname() const { return m_hostname; }
    QVariantList interfaces() const { return m_interfaces; }
    QVariantList routes() const { return m_routes; }
    QVariantList listeners() const { return m_listeners; }
    int exposedCount() const { return m_exposed; }
    QStringList notes() const { return m_notes; }
    QStringList dnsServers() const { return m_dnsServers; }
    QStringList dnsSearch() const { return m_dnsSearch; }
    QString dnsNote() const { return m_dnsNote; }
    QString resolvedText() const { return m_resolvedText; }
    QStringList tools() const { return m_tools; }
    bool probeBusy() const { return m_probeBusy; }
    QString probeKind() const { return m_probeKind; }
    QString probeTarget() const { return m_probeTarget; }
    QString probeOutput() const { return m_probeOutput; }
    QString probeStatus() const { return m_probeStatus; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void setWindowActive(bool active);
    // kind: "resolve" | "ping" | "trace". The target is validated before anything is sent.
    Q_INVOKABLE void runProbe(const QString &kind, const QString &target);
    Q_INVOKABLE void cancelProbe();

signals:
    void changed();
    void probeChanged();

private:
    void onRemoteChanged();
    void resetAll(const QString &state, const QString &status);

    RemoteController *m_remote;
    QThreadPool m_pool, m_probePool;
    std::shared_ptr<std::unique_ptr<SshSession>> m_probeSession;
    std::shared_ptr<std::atomic_bool> m_cancel, m_probeCancel;
    int m_seq = 0, m_probeSeq = 0, m_token = -1, m_probeTask = 0;
    bool m_busy = false, m_active = false, m_probeBusy = false;
    QString m_state = QStringLiteral("disconnected"), m_status = QStringLiteral("Aucun serveur connecté");
    QString m_updated, m_hostname, m_dnsNote, m_resolvedText;
    QVariantList m_interfaces, m_routes, m_listeners;
    int m_exposed = 0;
    QStringList m_notes, m_dnsServers, m_dnsSearch, m_tools;
    QString m_probeKind, m_probeTarget, m_probeOutput, m_probeStatus;
};
}  // namespace fortiq
