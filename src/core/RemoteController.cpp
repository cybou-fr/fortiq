#include "RemoteController.h"
#include "RemotePath.h"
#include "../ssh/SshClient.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QSettings>
#include <QUrl>
#include <QtConcurrentRun>

namespace fortiq {
namespace {
struct Outcome {
    SshResult result;
    int elapsedMs = 0;
};
}

RemoteController::RemoteController(QObject *parent)
    : QObject(parent), m_session(std::make_shared<std::unique_ptr<SshSession>>())
{
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
}

RemoteController::~RemoteController() {
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_pool.waitForDone();
}

QString RemoteController::rememberedHost() const {
    return QSettings{}.value("bootstrap/lastHost").toString();
}
QString RemoteController::rememberedUser() const {
    return QSettings{}.value("bootstrap/lastUser").toString();
}
int RemoteController::rememberedPort() const {
    return QSettings{}.value("bootstrap/lastPort", 22).toInt();
}
QString RemoteController::rememberedKeyPath() const {
    return QSettings{}.value("bootstrap/lastKeyPath").toString();
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

void RemoteController::connectServer(QString host, int port, QString user, QString password, QString keyPath) {
    if (m_busy) return;
    disconnectServer();
    m_host = host.trimmed();
    m_port = port;
    m_user = user.trimmed();
    m_password = std::move(password);
    m_keyPath = QUrl(keyPath.trimmed()).isLocalFile() ? QUrl(keyPath.trimmed()).toLocalFile() : keyPath.trimmed();
    if (m_host.isEmpty() || m_user.isEmpty() || (m_password.isEmpty() && m_keyPath.isEmpty()) || port < 1 || port > 65535) {
        m_status = QStringLiteral("Host, port, user and a password or private key are required.");
        emit changed();
        return;
    }
    m_status = QStringLiteral("Connecting and checking the SSH host key...");
    request(Kind::Open, {}, false);
}

void RemoteController::trustHost() {
    if (m_busy || m_pendingFingerprint.isEmpty() || m_host.isEmpty()) return;
    // Explicit acceptance must happen after the user has checked the shown
    // fingerprint with an independent trustworthy source. The next connection
    // checks the pinned bytes again BEFORE sending any password.
    QSettings{}.setValue(pinKey(), m_pendingFingerprint);
    m_pendingFingerprint.clear();
    m_status = QStringLiteral("SSH host pinned. Authenticating...");
    request(Kind::Open, {}, false);
}

void RemoteController::request(Kind kind, const QString &directory, bool record) {
    ConnectionRequest req;
    req.host = m_host;
    req.port = m_port;
    req.user = m_user;
    req.password = m_password; // kept in RAM until disconnect for transparent reconnects
    req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = directory;

    const int token = m_generation;
    const QString previous = m_currentDirectory;
    m_pendingDirectory = directory;
    // Show the RAM copy at once; the live listing replaces it when it arrives.
    if (kind == Kind::List && m_cache.contains(directory)) {
        m_currentDirectory = directory;
        m_entries = m_cache.value(directory);
    }
    m_busy = true;
    emit changed();

    auto slot = m_session;
    auto *watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, token, kind, previous, record] {
        const Outcome outcome = watcher->result();
        watcher->deleteLater();
        if (token != m_generation) return; // old server's async response
        const SshResult &result = outcome.result;
        if (result.status == SshResult::Status::Ok) m_cache.insert(result.directory, result.entries);
        // A newer navigation is queued; only its answer may change the view.
        if (kind == Kind::List && result.status == SshResult::Status::Ok
            && result.directory != m_pendingDirectory) return;
        m_busy = false;
        m_latencyMs = outcome.elapsedMs;
        if (result.status == SshResult::Status::TrustRequired) {
            m_connected = false;
            m_pendingFingerprint = result.fingerprint;
            m_status = result.message;
        } else if (result.status == SshResult::Status::Ok) {
            if (record && !previous.isEmpty() && previous != result.directory) {
                m_back.push_back(previous);
                m_forward.clear();
            }
            m_connected = true;
            m_pendingFingerprint.clear();
            m_currentDirectory = result.directory;
            m_home = result.home;
            m_entries = result.entries;
            m_status = result.message;
            if (kind == Kind::Open) {
                QSettings settings;
                settings.setValue("bootstrap/lastHost", m_host);
                settings.setValue("bootstrap/lastPort", m_port);
                settings.setValue("bootstrap/lastUser", m_user);
                settings.setValue("bootstrap/lastKeyPath", m_keyPath); // path only, never key bytes
            }
        } else {
            m_status = result.message;
            if (kind == Kind::Open) {
                m_connected = false;
                m_entries.clear();
                m_currentDirectory.clear();
            } else {
                // Keep the user where they were; the failed folder is not entered.
                m_currentDirectory = previous;
                m_entries = m_cache.value(previous, m_entries);
            }
        }
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, kind] {
        QElapsedTimer timer;
        timer.start();
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        Outcome outcome;
        if (kind == Kind::Open || !session.isOpen()) {
            outcome.result = session.open(req);
        } else {
            outcome.result = session.list(req.directory);
            if (!session.isOpen()) outcome.result = session.open(req); // one reconnect, pin re-checked
        }
        outcome.elapsedMs = static_cast<int>(timer.elapsed());
        return outcome;
    }));
}

void RemoteController::navigate(const QString &path) {
    if (!m_connected) return;
    const auto clean = cleanAbsolutePath(path.trimmed());
    if (!clean) {
        m_status = QStringLiteral("Invalid folder path.");
        emit changed();
        return;
    }
    request(Kind::List, *clean, true);
}
void RemoteController::goUp() {
    if (m_connected && !m_currentDirectory.isEmpty() && m_currentDirectory != "/")
        request(Kind::List, parentPath(m_currentDirectory), true);
}
void RemoteController::goBack() {
    if (!m_connected || m_back.isEmpty()) return;
    m_forward.push_back(m_currentDirectory);
    request(Kind::List, m_back.takeLast(), false);
}
void RemoteController::goForward() {
    if (!m_connected || m_forward.isEmpty()) return;
    m_back.push_back(m_currentDirectory);
    request(Kind::List, m_forward.takeLast(), false);
}
void RemoteController::goHome() {
    if (m_connected && !m_home.isEmpty()) request(Kind::List, m_home, true);
}
void RemoteController::refresh() {
    if (m_connected && !m_currentDirectory.isEmpty()) request(Kind::List, m_currentDirectory, false);
}
void RemoteController::disconnectServer() {
    ++m_generation; // outstanding results can never bind to another server
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_session = std::make_shared<std::unique_ptr<SshSession>>();
    m_busy = false;
    m_connected = false;
    m_pendingFingerprint.clear();
    m_entries.clear();
    m_currentDirectory.clear();
    m_home.clear();
    m_back.clear();
    m_forward.clear();
    m_cache.clear();
    scrubPassword();
    m_status = QStringLiteral("Disconnected. Working data is not stored on this device.");
    emit changed();
}
}
