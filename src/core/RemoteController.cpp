#include "RemoteController.h"
#include "RemotePath.h"
#include "../ssh/SshClient.h"

#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QSettings>
#include <QUrl>
#include <QtConcurrentRun>

namespace fortiq {
RemoteController::RemoteController(QObject *parent) : QObject(parent) {}

QString RemoteController::rememberedHost() const {
    return QSettings{}.value("bootstrap/lastHost").toString();
}
QString RemoteController::rememberedUser() const {
    return QSettings{}.value("bootstrap/lastUser").toString();
}
int RemoteController::rememberedPort() const {
    return QSettings{}.value("bootstrap/lastPort", 22).toInt();
}
QString RemoteController::pinKey() const {
    const auto bytes = (m_host.toLower() + ':' + QString::number(m_port)).toUtf8();
    return QStringLiteral("ssh/hostPins/") + QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
void RemoteController::scrubPassword() {
    m_password.fill(QChar::Null);
    m_password.clear();
}

QString RemoteController::rememberedKeyPath() const {
    return QSettings{}.value("bootstrap/lastKeyPath").toString();
}

void RemoteController::connectServer(QString host, int port, QString user, QString password, QString keyPath) {
    if (m_busy) return;
    ++m_generation;
    scrubPassword();
    m_host = host.trimmed();
    m_port = port;
    m_user = user.trimmed();
    m_password = std::move(password);
    m_keyPath = QUrl(keyPath.trimmed()).isLocalFile() ? QUrl(keyPath.trimmed()).toLocalFile() : keyPath.trimmed();
    m_pendingFingerprint.clear();
    m_connected = false;
    m_currentDirectory.clear();
    m_entries.clear();
    if (m_host.isEmpty() || m_user.isEmpty() || (m_password.isEmpty() && m_keyPath.isEmpty()) || port < 1 || port > 65535) {
        m_status = QStringLiteral("Host, port, user and a password or private key are required.");
        emit changed();
        return;
    }
    m_status = QStringLiteral("Connecting and checking the SSH host key...");
    emit changed();
    beginRequest({});
}

void RemoteController::trustHost() {
    if (m_busy || m_pendingFingerprint.isEmpty() || m_host.isEmpty()) return;
    // Explicit acceptance must happen after the user has checked the shown
    // fingerprint with an independent trustworthy source. The next connection
    // checks the pinned bytes again BEFORE sending any password.
    QSettings{}.setValue(pinKey(), m_pendingFingerprint);
    m_pendingFingerprint.clear();
    m_status = QStringLiteral("SSH host pinned. Authenticating...");
    emit changed();
    beginRequest({});
}

void RemoteController::beginRequest(const QString &directory) {
    if (m_busy) return;
    ConnectionRequest req;
    req.host = m_host;
    req.port = m_port;
    req.user = m_user;
    req.password = m_password;
    req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = directory;
    const int token = m_generation;
    m_busy = true;
    emit changed();

    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, token] {
        const SshResult result = watcher->result();
        watcher->deleteLater();
        if (token != m_generation) return; // old server's async response
        m_busy = false;
        m_status = result.message;
        if (result.status == SshResult::Status::TrustRequired) {
            m_connected = false;
            m_pendingFingerprint = result.fingerprint;
        } else if (result.status == SshResult::Status::Ok) {
            m_connected = true;
            m_pendingFingerprint.clear();
            m_currentDirectory = result.directory;
            m_entries = result.entries;
            QSettings settings;
            settings.setValue("bootstrap/lastHost", m_host);
            settings.setValue("bootstrap/lastPort", m_port);
            settings.setValue("bootstrap/lastUser", m_user);
            settings.setValue("bootstrap/lastKeyPath", m_keyPath); // path only, never key bytes
        } else if (!m_connected) {
            m_entries.clear();
            m_currentDirectory.clear();
        }
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run([req] { return SshClient::list(req); }));
}

void RemoteController::openDirectory(const QString &path) {
    if (!m_connected || m_busy) return;
    // Only a directory shown in the active server listing is navigable.
    bool present = false;
    for (const auto &v : m_entries) {
        const QVariantMap item = v.toMap();
        if (item.value("directory").toBool() && item.value("path").toString() == path) {
            present = true; break;
        }
    }
    if (!present) { m_status = QStringLiteral("Invalid directory selection."); emit changed(); return; }
    beginRequest(path);
}
void RemoteController::goUp() {
    if (m_connected && !m_busy && !m_currentDirectory.isEmpty())
        beginRequest(parentPath(m_currentDirectory));
}
void RemoteController::refresh() {
    if (m_connected && !m_busy && !m_currentDirectory.isEmpty()) beginRequest(m_currentDirectory);
}
void RemoteController::disconnectServer() {
    ++m_generation; // outstanding results can never bind to another server
    m_busy = false;
    m_connected = false;
    m_pendingFingerprint.clear();
    m_entries.clear();
    m_currentDirectory.clear();
    scrubPassword();
    m_status = QStringLiteral("Disconnected. Working data is not stored on this device.");
    emit changed();
}
}
