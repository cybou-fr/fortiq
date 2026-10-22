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
public:
    explicit TerminalController(RemoteController *remote, QObject *parent = nullptr);
    ~TerminalController() override;
    bool running() const { return m_running; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void send(const QByteArray &data);
    Q_INVOKABLE void resize(int columns, int rows);
signals:
    void changed();
    void output(const QByteArray &data);
    void resetScreen();
private:
    void poll();
    RemoteController *m_remote;
    QThreadPool m_pool;
    QTimer m_timer;
    std::shared_ptr<std::unique_ptr<SshSession>> m_session;
    QByteArray m_input;
    int m_seq = 0, m_serverToken = -1, m_columns = 80, m_rows = 24;
    bool m_running = false, m_busy = false;
    QString m_status = QStringLiteral("Terminal fermé.");
};
}
