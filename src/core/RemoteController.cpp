#include "RemoteController.h"
#include "RemotePath.h"
#include "../ssh/SshClient.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QLocale>
#include <QStringDecoder>
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
namespace {
constexpr qint64 kTextLimit = 1024 * 1024;
constexpr qint64 kImageLimit = 16 * 1024 * 1024;

QString imageMime(const QString &path) {
    const QString ext = path.section('.', -1).toLower();
    static const QHash<QString, QString> types {
        { "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" },
        { "gif", "image/gif" }, { "webp", "image/webp" }, { "bmp", "image/bmp" },
        { "svg", "image/svg+xml" }, { "ico", "image/x-icon" } };
    return path.section('/', -1).contains('.') ? types.value(ext) : QString();
}
QString hexDump(const QByteArray &bytes) {
    QString out;
    for (qsizetype row = 0; row < bytes.size(); row += 16) {
        const QByteArray chunk = bytes.mid(row, 16);
        QString ascii;
        for (char ch : chunk) ascii += (ch >= 32 && ch < 127) ? QChar(ch) : QChar('.');
        out += QStringLiteral("%1  %2 %3\n").arg(row, 8, 16, QChar('0'))
                   .arg(QString::fromLatin1(chunk.toHex(' ')), -48).arg(ascii);
    }
    return out;
}
QString humanSize(qint64 bytes) {
    if (bytes < 0) return QStringLiteral("taille inconnue");
    return QLocale(QLocale::French).formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}
}

void RemoteController::openFile(const QString &path) {
    if (!m_connected) return;
    const auto clean = cleanAbsolutePath(path);
    if (!clean) return;
    const int seq = ++m_previewSeq;
    const int token = m_generation;
    const QString mime = imageMime(*clean);
    m_previewPath = *clean;
    m_previewKind.clear();
    m_previewText.clear();
    m_previewImage.clear();
    m_previewInfo = QStringLiteral("Lecture…");
    m_previewBusy = true;
    emit previewChanged();

    ConnectionRequest req;
    req.host = m_host; req.port = m_port; req.user = m_user;
    req.password = m_password; req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = m_currentDirectory;
    const qint64 limit = mime.isEmpty() ? kTextLimit : kImageLimit + 1;
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, seq, token, mime] {
        SshResult r = watcher->result();
        watcher->deleteLater();
        if (token != m_generation || seq != m_previewSeq || m_previewPath.isEmpty()) return;
        m_previewBusy = false;
        if (r.status != SshResult::Status::Ok) {
            m_previewKind = QStringLiteral("error");
            m_previewInfo = r.message;
        } else if (!mime.isEmpty()) {
            if (r.truncated) {
                m_previewKind = QStringLiteral("error");
                m_previewInfo = QStringLiteral("Image trop volumineuse pour l'aperçu (%1, limite 16 Mo).").arg(humanSize(r.fileSize));
            } else {
                m_previewKind = QStringLiteral("image");
                m_previewImage = QStringLiteral("data:%1;base64,").arg(mime) + QString::fromLatin1(r.data.toBase64());
                m_previewInfo = humanSize(r.data.size());
            }
        } else {
            QByteArray sample = r.data;
            // A truncated read may cut a UTF-8 sequence; drop the partial tail.
            if (r.truncated) {
                int back = 0;
                while (back < 4 && back < sample.size()
                       && (static_cast<uchar>(sample.at(sample.size() - 1 - back)) & 0xC0) == 0x80) ++back;
                if (back < sample.size() && static_cast<uchar>(sample.at(sample.size() - 1 - back)) >= 0xC0)
                    sample.chop(back + 1);
            }
            QStringDecoder decoder(QStringDecoder::Utf8);
            QString text = decoder.decode(sample);
            if (decoder.hasError() || r.data.left(8192).contains('\0')) {
                m_previewKind = QStringLiteral("binary");
                m_previewText = hexDump(r.data.left(1024));
                m_previewInfo = QStringLiteral("Fichier binaire, %1 — premiers 1 Ko en hexadécimal").arg(humanSize(r.fileSize));
            } else {
                m_previewKind = QStringLiteral("text");
                m_previewText = std::move(text);
                m_previewInfo = r.truncated
                    ? QStringLiteral("%1 — aperçu du premier Mo").arg(humanSize(r.fileSize))
                    : QStringLiteral("%1 — %2 lignes").arg(humanSize(r.fileSize)).arg(m_previewText.count('\n') + (m_previewText.endsWith('\n') ? 0 : 1));
            }
        }
        emit previewChanged();
    });
    const QString target = *clean;
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, target, limit] {
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult reopened = session.open(req); // pin re-checked
            if (reopened.status != SshResult::Status::Ok) return reopened;
        }
        SshResult r = session.readFile(target, limit);
        if (!session.isOpen() && session.open(req).status == SshResult::Status::Ok)
            r = session.readFile(target, limit);
        return r;
    }));
}

void RemoteController::closePreview() {
    ++m_previewSeq;
    m_previewBusy = false;
    m_previewPath.clear();
    m_previewKind.clear();
    m_previewText.clear();
    m_previewImage.clear();
    m_previewInfo.clear();
    emit previewChanged();
}

void RemoteController::disconnectServer() {
    closePreview();
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
