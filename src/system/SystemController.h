#pragma once

#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>

namespace fortiq {
class RemoteController;
class SshSession;

// System app (read-only): processes, systemd services and an overview of the
// selected server. Bound to the server session: a new server clears everything.
// Nothing is started, stopped or killed. See docs/SYSTEM.md.
class SystemController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)          // disconnected | loading | ready | error
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString updated READ updated NOTIFY changed)
    Q_PROPERTY(QVariantMap overview READ overview NOTIFY changed)
    Q_PROPERTY(QVariantList processes READ processes NOTIFY changed)
    Q_PROPERTY(QVariantList services READ services NOTIFY changed)
    Q_PROPERTY(bool hasSystemd READ hasSystemd NOTIFY changed)
    Q_PROPERTY(QStringList notes READ notes NOTIFY changed)

public:
    explicit SystemController(RemoteController *remote, QObject *parent = nullptr);
    ~SystemController() override;

    bool busy() const { return m_busy; }
    QString state() const { return m_state; }
    QString status() const { return m_status; }
    QString updated() const { return m_updated; }
    QVariantMap overview() const { return m_overview; }
    QVariantList processes() const { return m_processes; }
    QVariantList services() const { return m_services; }
    bool hasSystemd() const { return m_hasSystemd; }
    QStringList notes() const { return m_notes; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void setWindowActive(bool active);

signals:
    void changed();

private:
    void onRemoteChanged();

    RemoteController *m_remote;
    QThreadPool m_pool;
    std::shared_ptr<std::unique_ptr<SshSession>> m_session;   // one connection reused by every refresh (auto-refresh is cheap)
    std::shared_ptr<std::atomic_bool> m_cancel;
    int m_seq = 0, m_token = -1;
    bool m_busy = false, m_active = false, m_hasSystemd = false;
    QString m_state = QStringLiteral("disconnected"), m_status = QStringLiteral("Aucun serveur connecté"), m_updated;
    QVariantMap m_overview;
    QVariantList m_processes, m_services;
    QStringList m_notes;
};
}  // namespace fortiq
