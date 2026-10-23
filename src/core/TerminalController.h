#pragma once
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include "../ssh/SshClient.h"

namespace fortiq {
class RemoteController;
class TerminalController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QString historyStatus READ historyStatus NOTIFY historyChanged)
public:
    explicit TerminalController(RemoteController *remote, QObject *parent = nullptr);
    ~TerminalController() override;
    bool running() const { return m_running; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QVariantList history() const { return m_history; }
    QString historyStatus() const { return m_historyStatus; }
    Q_INVOKABLE void setWindowActive(bool active);
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void send(const QByteArray &data);
    Q_INVOKABLE void resize(int columns, int rows);
signals:
    void changed();
    void output(const QByteArray &data);
    void resetScreen();
    void historyChanged();
private:
    void poll();
    void endTask(const QString &how, const QString &message);
    void maybeStart();
    RemoteController *m_remote;
    QThreadPool m_pool;
    QTimer m_timer;
    std::shared_ptr<std::unique_ptr<SshSession>> m_session;
    QByteArray m_input;
    int m_seq = 0, m_serverToken = -1, m_columns = 80, m_rows = 24, m_task = 0;
    QString m_endHow, m_endMessage;   // how the shell task ends (consumed by stop())
    bool m_running = false, m_busy = false, m_pollBusy = false, m_windowActive = false;
    int m_autoAttemptToken = -1;
    int m_historyToken = -1;
    QVariantList m_history;
    QString m_historyStatus = QStringLiteral("Historique Bash enregistré sur le serveur.");
    QString m_status = QStringLiteral("Terminal fermé.");
};
}
