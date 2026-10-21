#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>

namespace fortiq {
class RemoteController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString currentDirectory READ currentDirectory NOTIFY changed)
    Q_PROPERTY(QString serverLabel READ serverLabel NOTIFY changed)
    Q_PROPERTY(QString pendingFingerprint READ pendingFingerprint NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QString rememberedHost READ rememberedHost CONSTANT)
    Q_PROPERTY(QString rememberedUser READ rememberedUser CONSTANT)
    Q_PROPERTY(int rememberedPort READ rememberedPort CONSTANT)

public:
    explicit RemoteController(QObject *parent = nullptr);
    bool busy() const { return m_busy; }
    bool connected() const { return m_connected; }
    QString statusText() const { return m_status; }
    QString currentDirectory() const { return m_currentDirectory; }
    QString serverLabel() const { return m_connected ? m_user + "@" + m_host : QString(); }
    QString pendingFingerprint() const { return m_pendingFingerprint; }
    QVariantList entries() const { return m_entries; }
    QString rememberedHost() const;
    QString rememberedUser() const;
    int rememberedPort() const;

    Q_INVOKABLE void connectServer(QString host, int port, QString user, QString password);
    Q_INVOKABLE void trustHost();
    Q_INVOKABLE void openDirectory(const QString &path);
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void disconnectServer();

signals:
    void changed();

private:
    void beginRequest(const QString &directory);
    void scrubPassword();
    QString pinKey() const;

    bool m_busy = false;
    bool m_connected = false;
    int m_generation = 0;
    int m_port = 22;
    QString m_host;
    QString m_user;
    QString m_password;
    QString m_pendingFingerprint;
    QString m_status = QStringLiteral("No Linux server connected.");
    QString m_currentDirectory;
    QVariantList m_entries;
};
}
