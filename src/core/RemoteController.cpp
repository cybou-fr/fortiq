#include "RemoteController.h"
#include "SecretStore.h"
#include "LocalDocument.h"
#include "RemotePath.h"
#include "../ssh/SshClient.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QPointer>
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
    : QObject(parent), m_session(std::make_shared<std::unique_ptr<SshSession>>()),
      m_transferSession(std::make_shared<std::unique_ptr<SshSession>>()),
      m_searchSession(std::make_shared<std::unique_ptr<SshSession>>())
{
    m_searchPool.setMaxThreadCount(1);
    m_searchPool.setExpiryTimeout(-1);
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
    m_transferPool.setMaxThreadCount(1);
    m_transferPool.setExpiryTimeout(-1);
}

RemoteController::~RemoteController() {
    for (Transfer &t : m_transfers) t.cancel->store(true);
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    auto transferSlot = m_transferSession;
    (void)QtConcurrent::run(&m_transferPool, [transferSlot] { transferSlot->reset(); });
    m_searchCancel->store(true);
    auto searchSlot = m_searchSession;
    (void)QtConcurrent::run(&m_searchPool, [searchSlot] { searchSlot->reset(); });
    m_pool.waitForDone();
    m_transferPool.waitForDone();
    m_searchPool.waitForDone();
}

ConnectionRequest RemoteController::connectionRequest() const {
    ConnectionRequest req;
    req.host = m_host; req.port = m_port; req.user = m_user;
    req.password = m_password; req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = QStringLiteral("/");
    return req;
}

QVariantList RemoteController::loadProfiles() const {
    QSettings settings;
    QVariantList list = QJsonDocument::fromJson(settings.value("servers/profiles").toByteArray()).array().toVariantList();
    // First run after the server manager appeared: keep the last server.
    if (list.isEmpty() && !settings.contains("servers/profiles") && !rememberedHost().isEmpty()) {
        list << QVariantMap {
            { "id", QUuid::createUuid().toString(QUuid::WithoutBraces) }, { "name", rememberedHost() },
            { "host", rememberedHost() }, { "port", rememberedPort() }, { "user", rememberedUser() },
            { "keyPath", rememberedKeyPath() } };
    }
    return list;
}

void RemoteController::storeProfiles(const QVariantList &profiles) {
    QSettings{}.setValue("servers/profiles", QJsonDocument(QJsonArray::fromVariantList(profiles)).toJson(QJsonDocument::Compact));
    emit serversChanged();
}

QVariantList RemoteController::servers() const {
    QVariantList out;
    QSettings settings;
    for (const QVariant &v : loadProfiles()) {
        QVariantMap p = v.toMap();
        const auto bytes = (p.value("host").toString().toLower() + ':' + QString::number(p.value("port").toInt())).toUtf8();
        const QString key = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        p.insert("pinned", settings.contains(key));
        p.insert("fingerprint", settings.value(key).toString());
        p.insert("active", m_connected && p.value("id").toString() == m_activeServerId);
        out << p;
    }
    return out;
}

bool RemoteController::secretStoreAvailable() const { return SecretStore::available(); }

void RemoteController::setSecretSaved(const QString &id, bool saved) {
    QVariantList list = loadProfiles();
    bool changed = false;
    for (QVariant &v : list) {
        QVariantMap p = v.toMap();
        if (p.value("id").toString() != id || p.value("secretSaved").toBool() == saved) continue;
        p.insert("secretSaved", saved);
        v = p; changed = true;
    }
    if (changed) storeProfiles(list);
}

QString RemoteController::saveServer(const QVariantMap &profile) {
    const QString host = profile.value("host").toString().trimmed();
    const QString user = profile.value("user").toString().trimmed();
    const int port = profile.value("port").toInt();
    if (host.isEmpty() || user.isEmpty() || port < 1 || port > 65535 || host.contains(' ') || user.contains(' ')) return {};
    QString key = profile.value("keyPath").toString().trimmed();
    if (!key.isEmpty()) {
        const QUrl url = QUrl::fromUserInput(key);
        key = localDocumentPath(url);
        if (key.isEmpty()) return {};
    }
    QString id = profile.value("id").toString();
    if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString name = profile.value("name").toString().trimmed();
    const QVariantMap stored { { "id", id }, { "name", name.isEmpty() ? host : name }, { "host", host },
                               { "port", port }, { "user", user }, { "keyPath", key } };
    QVariantMap toStore = stored;
    QVariantList list = loadProfiles();
    bool replaced = false;
    bool forget = false;
    for (QVariant &v : list) {
        if (v.toMap().value("id") != id) continue;
        const QVariantMap old = v.toMap();
        // A remembered secret belongs to one account on one server: if the
        // target changes it must never be sent to the new one.
        const bool sameTarget = old.value("host").toString().toLower() == host.toLower()
            && old.value("port").toInt() == port && old.value("user").toString() == user
            && old.value("keyPath").toString() == key;
        if (old.value("secretSaved").toBool()) {
            if (sameTarget) toStore.insert("secretSaved", true);
            else forget = true;
        }
        v = toStore; replaced = true;
    }
    if (!replaced) list << toStore;
    if (forget) SecretStore::remove(id);
    storeProfiles(list);
    return id;
}

void RemoteController::removeServer(const QString &id) {
    QVariantList list = loadProfiles();
    list.removeIf([&](const QVariant &v) { return v.toMap().value("id").toString() == id; });
    storeProfiles(list);
    SecretStore::remove(id);
    if (id == m_activeServerId && m_connected) disconnectServer();
}

void RemoteController::forgetSecret(const QString &id) {
    const SecretStore::Result r = SecretStore::remove(id);
    if (!r.ok) { m_status = QStringLiteral("Could not delete the remembered password: ") + r.error; emit changed(); return; }
    setSecretSaved(id, false);
}

void RemoteController::connectToSaved(const QString &id) {
    const SecretStore::Result r = SecretStore::load(id);
    if (!r.ok) {
        // A missing entry means the flag is stale: drop it so the prompt asks again.
        if (r.notFound) setSecretSaved(id, false);
        m_status = r.notFound ? QStringLiteral("The remembered password is no longer in the Windows Credential Manager; enter it again.")
                              : QStringLiteral("Could not read the remembered password: ") + r.error;
        emit changed();
        return;
    }
    connectTo(id, r.secret);
}

void RemoteController::connectToRemembering(const QString &id, const QString &password, bool remember) {
    if (remember && !SecretStore::available()) {
        m_status = SecretStore::unavailableReason();
        emit changed();
        return; // explicit: connecting without saving would hide that the request was not honoured
    }
    connectTo(id, password);
    m_rememberPending = remember && !password.isEmpty() && m_activeServerId == id;
}

void RemoteController::connectTo(const QString &id, const QString &password) {
    for (const QVariant &v : loadProfiles()) {
        const QVariantMap p = v.toMap();
        if (p.value("id").toString() != id) continue;
        connectServer(p.value("host").toString(), p.value("port").toInt(), p.value("user").toString(),
                      password, p.value("keyPath").toString());
        m_activeServerId = id;
        emit changed();
        emit serversChanged();
        return;
    }
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
    m_activeServerId.clear();
    m_host = host.trimmed();
    m_port = port;
    m_user = user.trimmed();
    m_password = std::move(password);
    m_keyPath = keyPath.trimmed().isEmpty() ? QString() : localDocumentPath(QUrl::fromUserInput(keyPath.trimmed()));
    if (!keyPath.trimmed().isEmpty() && m_keyPath.isEmpty()) {
        scrubPassword();
        m_status = QStringLiteral("Choose a local private key or an Android document; this URL type is not supported.");
        emit changed(); return;
    }
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
    emit serversChanged();
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
            if (!m_connected) emit serversChanged();
            m_connected = true;
            m_pendingFingerprint.clear();
            m_currentDirectory = result.directory;
            m_home = result.home;
            m_entries = result.entries;
            m_status = result.message;
            if (kind == Kind::Open && m_rememberPending && !m_activeServerId.isEmpty()) {
                m_rememberPending = false;
                const SecretStore::Result saved = SecretStore::save(m_activeServerId, m_password);
                if (saved.ok) setSecretSaved(m_activeServerId, true);
                else m_status = QStringLiteral("Connected, but the password could not be remembered: ") + saved.error;
            }
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
                m_rememberPending = false;
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
    if (!m_connected || m_saving) return;
    m_editHash.clear();
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
                // Only complete files are editable; remember their exact version.
                if (!r.truncated) {
                    m_editHash = QCryptographicHash::hash(r.data, QCryptographicHash::Sha256);
                    m_editCrlf = r.data.contains("\r\n");
                    m_editBom = r.data.startsWith("\xEF\xBB\xBF");
                }
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
    m_editHash.clear();
    m_previewBusy = false;
    m_previewPath.clear();
    m_previewKind.clear();
    m_previewText.clear();
    m_previewImage.clear();
    m_previewInfo.clear();
    emit previewChanged();
}

QVariantList RemoteController::transfers() const {
    QVariantList out;
    for (const Transfer &t : m_transfers) {
        out.push_back(QVariantMap {
            { "id", t.id }, { "upload", t.upload }, { "name", t.name }, { "remote", t.remote },
            { "local", t.local }, { "state", t.state }, { "message", t.message },
            { "done", static_cast<double>(t.done) }, { "total", static_cast<double>(t.total) } });
    }
    return out;
}
int RemoteController::activeTransfers() const {
    int n = 0;
    for (const Transfer &t : m_transfers) n += (t.state == "queued" || t.state == "running" || t.state == "conflict");
    return n;
}
RemoteController::Transfer *RemoteController::findTransfer(int id) {
    for (Transfer &t : m_transfers) if (t.id == id) return &t;
    return nullptr;
}

void RemoteController::download(const QString &remotePath, const QUrl &localFile) {
    const auto clean = cleanAbsolutePath(remotePath);
    const QString local = localDocumentPath(localFile);
    if (!m_connected || !clean || local.isEmpty()) return;
    Transfer t;
    t.id = m_nextTransferId++;
    t.name = clean->section('/', -1);
    t.remote = *clean;
    t.local = local;
    m_transfers.push_back(t);
    emit transfersChanged();
    startTransfer(t.id, static_cast<int>(ConflictPolicy::Fail));
}

void RemoteController::upload(const QVariantList &localFiles, const QString &remoteDir) {
    const auto dir = cleanAbsolutePath(remoteDir.isEmpty() ? m_currentDirectory : remoteDir);
    if (!m_connected || !dir) return;
    for (const QVariant &v : localFiles) {
        const QUrl url = v.toUrl();
        const QString local = localDocumentPath(url);
        if (local.isEmpty()) continue;
        Transfer t;
        t.id = m_nextTransferId++;
        t.upload = true;
        t.name = localDocumentName(local);
        if (t.name.isEmpty()) {
            t.name = QStringLiteral("Document");
            t.state = QStringLiteral("error");
            t.message = QStringLiteral("Cannot read the selected document name. Select the file again to renew access.");
            m_transfers.push_back(t);
            continue;
        }
        t.remote = *dir;
        t.local = local;
        m_transfers.push_back(t);
        startTransfer(t.id, static_cast<int>(ConflictPolicy::Fail));
    }
    emit transfersChanged();
}

void RemoteController::resolveConflict(int id, const QString &policy) {
    Transfer *t = findTransfer(id);
    if (!t || t->state != "conflict") return;
    if (policy == "skip") {
        t->state = QStringLiteral("cancelled");
        t->message = QStringLiteral("Ignoré ; le fichier existant est conservé.");
        emit transfersChanged();
        return;
    }
    t->state = QStringLiteral("queued");
    t->message.clear();
    t->done = 0;
    emit transfersChanged();
    startTransfer(id, static_cast<int>(policy == "replace" ? ConflictPolicy::Replace : ConflictPolicy::KeepBoth));
}

void RemoteController::cancelTransfer(int id) {
    Transfer *t = findTransfer(id);
    if (!t) return;
    t->cancel->store(true);
    if (t->state == "queued" || t->state == "conflict") {
        t->state = QStringLiteral("cancelled");
        t->message = QStringLiteral("Annulé.");
        emit transfersChanged();
    }
}

void RemoteController::clearFinishedTransfers() {
    m_transfers.removeIf([](const Transfer &t) {
        return t.state == "done" || t.state == "error" || t.state == "cancelled" || t.state == "unknown";
    });
    emit transfersChanged();
}

void RemoteController::startTransfer(int id, int policyValue) {
    Transfer *t = findTransfer(id);
    if (!t) return;
    t->cancel->store(false);
    ConnectionRequest req;
    req.host = m_host; req.port = m_port; req.user = m_user;
    req.password = m_password; req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = QStringLiteral("/");
    const Transfer job = *t;
    const int token = m_generation;
    const auto policy = static_cast<ConflictPolicy>(policyValue);
    QPointer<RemoteController> self(this);
    auto slot = m_transferSession;

    auto report = [self, id, token](qint64 done, qint64 total) {
        QMetaObject::invokeMethod(self, [self, id, token, done, total] {
            if (!self || token != self->m_generation) return;
            if (Transfer *t = self->findTransfer(id); t && t->state == "running") {
                t->done = done; t->total = total;
                emit self->transfersChanged();
            }
        }, Qt::QueuedConnection);
    };

    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, id, token, job] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        Transfer *t = findTransfer(id);
        if (!t || token != m_generation || t->state == "cancelled") return;
        if (r.status == SshResult::Status::Ok) {
            t->state = QStringLiteral("done");
            t->done = r.bytes;
            t->total = r.bytes;
            t->message = job.upload ? QStringLiteral("Publié : %1").arg(r.directory.section('/', -1))
                                    : QStringLiteral("Enregistré : %1").arg(r.directory);
            if (job.upload && m_connected && job.remote == m_currentDirectory) refresh();
        } else {
            t->state = r.code == "conflict" ? QStringLiteral("conflict")
                     : r.code == "cancelled" ? QStringLiteral("cancelled")
                     : r.code == "unknown" ? QStringLiteral("unknown") : QStringLiteral("error");
            t->message = r.message;
        }
        emit transfersChanged();
    });
    watcher->setFuture(QtConcurrent::run(&m_transferPool, [slot, req, job, policy, report, self, id, token] {
        if (job.cancel->load()) return SshResult{};  // cancelled while queued; result ignored
        QMetaObject::invokeMethod(self, [self, id, token] {
            if (!self || token != self->m_generation) return;
            if (Transfer *t = self->findTransfer(id); t && t->state == "queued") {
                t->state = QStringLiteral("running");
                emit self->transfersChanged();
            }
        }, Qt::QueuedConnection);
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(req); // host pin re-checked
            if (opened.status != SshResult::Status::Ok) return opened;
        }
        QElapsedTimer tick;
        tick.start();
        const TransferProgress progress = [&](qint64 done, qint64 total) {
            if (tick.elapsed() > 120 || done == total) { tick.restart(); report(done, total); }
            return !job.cancel->load();
        };
        if (job.upload) return session.upload(job.local, job.remote, job.name, policy, progress);
        auto result = session.download(job.remote, job.local, progress);
        // A native document picker can outlive an idle transport. Retry only
        // the read-only preflight, before any destination has been opened.
        if (result.code == "disconnected" && !job.cancel->load()) {
            const auto reopened = session.open(req); // pin checked again
            if (reopened.status != SshResult::Status::Ok) return reopened;
            result = session.download(job.remote, job.local, progress);
        }
        return result;
    }));
}

void RemoteController::runOperation(const QString &doneMessage, std::function<SshResult(SshSession &)> op) {
    if (!m_connected || m_operationBusy) return;
    ConnectionRequest req;
    req.host = m_host; req.port = m_port; req.user = m_user;
    req.password = m_password; req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = m_currentDirectory;
    const int token = m_generation;
    const QString folder = m_currentDirectory;
    m_operationBusy = true;
    emit operationBusyChanged();
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, token, folder, doneMessage] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        if (token != m_generation) return;
        m_operationBusy = false;
        emit operationBusyChanged();
        const bool ok = r.status == SshResult::Status::Ok;
        // The folder changed (or may have): drop its RAM copy and re-list.
        if (ok || r.code == "unknown") {
            // Restores and Trash moves touch other folders too.
            m_cache.clear();
            if (m_currentDirectory == folder) refresh();
        }
        emit operationFinished(ok, ok ? doneMessage : r.message, r.code, r.directory);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, op] {
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(req); // pin re-checked
            if (opened.status != SshResult::Status::Ok) return opened;
        }
        // Never replay a mutation after a drop: the result says "unknown".
        return op(session);
    }));
}

void RemoteController::createFolder(const QString &name) {
    const QString dir = m_currentDirectory;
    runOperation(QStringLiteral("Dossier « %1 » créé.").arg(name.trimmed()),
                 [dir, name](SshSession &s) { return s.makeDirectory(dir, name); });
}
void RemoteController::renameEntry(const QString &path, const QString &newName) {
    runOperation(QStringLiteral("Renommé en « %1 ».").arg(newName.trimmed()),
                 [path, newName](SshSession &s) { return s.renameEntry(path, newName); });
}
void RemoteController::trashEntry(const QString &path) {
    runOperation(QStringLiteral("« %1 » mis à la corbeille.").arg(path.section('/', -1)),
                 [path](SshSession &s) { return s.moveToTrash(path); });
}

void RemoteController::restoreEntry(const QString &trashedPath, bool keepBoth) {
    runOperation(QStringLiteral("« %1 » restauré.").arg(trashedPath.section('/', -1)), [trashedPath, keepBoth](SshSession &s) {
        return s.restoreFromTrash(trashedPath, keepBoth ? ConflictPolicy::KeepBoth : ConflictPolicy::Fail);
    });
}
void RemoteController::deletePermanently(const QString &trashedPath) {
    runOperation(QStringLiteral("« %1 » supprimé définitivement.").arg(trashedPath.section('/', -1)),
                 [trashedPath](SshSession &s) { return s.deleteFromTrash(trashedPath); });
}
void RemoteController::emptyTrash() {
    runOperation(QStringLiteral("Corbeille vidée."), [](SshSession &s) { return s.emptyTrash(); });
}

void RemoteController::saveFile(const QString &text, bool force) { runSave(text, force ? 1 : 0); }
void RemoteController::saveFileCopy(const QString &text) { runSave(text, 2); }

void RemoteController::runSave(const QString &text, int mode) {
    if (!m_connected || m_saving || !previewEditable()) return;
    // Keep the file's own conventions: line endings and byte-order mark.
    QString body = text;
    body.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (m_editCrlf) body.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    QByteArray bytes = body.toUtf8();
    if (m_editBom) bytes.prepend("\xEF\xBB\xBF");

    ConnectionRequest req;
    req.host = m_host; req.port = m_port; req.user = m_user;
    req.password = m_password; req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = m_currentDirectory;
    const QString path = m_previewPath;
    const QByteArray expected = m_editHash;
    const int token = m_generation;
    const int seq = m_previewSeq;
    m_saving = true;
    emit savingChanged();

    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, token, seq, mode, text] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        if (token != m_generation) return;
        m_saving = false;
        emit savingChanged();
        const bool ok = r.status == SshResult::Status::Ok;
        if (ok && mode != 2 && seq == m_previewSeq) {
            // The saved bytes are the new base version for the next save.
            m_editHash = r.data;
            m_previewText = text;
            m_previewInfo = QStringLiteral("%1 — enregistré").arg(humanSize(r.bytes));
            emit previewChanged();
        }
        if (ok) {
            m_cache.remove(parentPath(r.directory));
            if (m_currentDirectory == parentPath(r.directory)) refresh();
        }
        emit saveFinished(ok, ok ? (mode == 2 ? QStringLiteral("Copie enregistrée : %1").arg(r.directory.section('/', -1))
                                               : QStringLiteral("Enregistré."))
                                 : r.message,
                          r.code, r.directory);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, path, bytes, expected, mode] {
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(req); // pin re-checked
            if (opened.status != SshResult::Status::Ok) return opened;
        }
        // Never replayed after a drop: the user decides after "unknown".
        return mode == 2 ? session.saveCopy(parentPath(path), path.section('/', -1), bytes)
                         : session.saveFile(path, bytes, expected, mode == 1);
    }));
}

void RemoteController::startSearch(const QString &query, const QString &mode) {
    const QString q = query.trimmed();
    if (!m_connected || q.isEmpty() || m_currentDirectory.isEmpty()) return;
    m_searchCancel->store(true); // a running search stops; its result is ignored
    m_searchCancel = std::make_shared<std::atomic_bool>(false);
    const int seq = ++m_searchSeq;
    const int token = m_generation;
    m_searchActive = true;
    m_searchBusy = true;
    m_searchQuery = q;
    m_searchMode = mode == "content" ? QStringLiteral("content") : QStringLiteral("name");
    m_searchScope = m_currentDirectory;
    m_searchResults.clear();
    m_searchInfo = QStringLiteral("Recherche…");
    emit searchChanged();

    ConnectionRequest req;
    req.host = m_host; req.port = m_port; req.user = m_user;
    req.password = m_password; req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = QStringLiteral("/");
    auto slot = m_searchSession;
    auto cancel = m_searchCancel;
    const QString scope = m_searchScope, searchMode = m_searchMode;
    QPointer<RemoteController> self(this);
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, seq, token] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        if (token != m_generation || seq != m_searchSeq) return;
        m_searchBusy = false;
        m_searchResults = r.entries;
        const int n = static_cast<int>(r.entries.size());
        if (r.status == SshResult::Status::Ok) {
            m_searchInfo = (n == 0 ? QStringLiteral("Aucun résultat")
                                   : QStringLiteral("%1 résultat%2").arg(n).arg(n > 1 ? "s" : ""))
                + (r.truncated ? QStringLiteral(" — limite atteinte, affinez la recherche") : QString())
                + QStringLiteral("  ·  %1").arg(r.code);
        } else {
            m_searchInfo = r.code == "cancelled" ? QStringLiteral("Recherche annulée — %1 résultat(s) partiel(s)").arg(n)
                                                 : r.message;
        }
        emit searchChanged();
    });
    watcher->setFuture(QtConcurrent::run(&m_searchPool, [slot, req, scope, q, searchMode, cancel, self, seq] {
        if (cancel->load()) return SshResult{};
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(req); // pin re-checked
            if (opened.status != SshResult::Status::Ok) return opened;
        }
        int lastReported = -1;
        return session.search(scope, q, searchMode, 500, [&](int found) {
            if (found != lastReported) {
                lastReported = found;
                QMetaObject::invokeMethod(self, [self, seq, found] {
                    if (!self || seq != self->m_searchSeq || !self->m_searchBusy) return;
                    self->m_searchInfo = QStringLiteral("Recherche… %1 trouvé(s)").arg(found);
                    emit self->searchChanged();
                }, Qt::QueuedConnection);
            }
            return !cancel->load();
        });
    }));
}

void RemoteController::cancelSearch() {
    m_searchCancel->store(true);
}

void RemoteController::clearSearch() {
    m_searchCancel->store(true);
    ++m_searchSeq;
    m_searchActive = false;
    m_searchBusy = false;
    m_searchResults.clear();
    m_searchInfo.clear();
    m_searchQuery.clear();
    emit searchChanged();
}

void RemoteController::disconnectServer() {
    m_rememberPending = false;
    clearSearch();
    auto searchSlot = m_searchSession;
    (void)QtConcurrent::run(&m_searchPool, [searchSlot] { searchSlot->reset(); });
    m_searchSession = std::make_shared<std::unique_ptr<SshSession>>();
    if (m_operationBusy) { m_operationBusy = false; emit operationBusyChanged(); }
    if (m_saving) { m_saving = false; emit savingChanged(); }
    // Transfers are bound to this server: stop them, never redirect them.
    for (Transfer &t : m_transfers) {
        if (t.state == "queued" || t.state == "running" || t.state == "conflict") {
            t.cancel->store(true);
            const bool documentMayBePartial = t.state == "running" && !t.upload && isContentDocument(t.local);
            t.state = t.state == "running" && (t.upload || documentMayBePartial) ? QStringLiteral("unknown") : QStringLiteral("cancelled");
            t.message = documentMayBePartial
                ? QStringLiteral("Déconnecté du serveur. Le document choisi peut être partiel ; vérifiez-le avant de réessayer.")
                : QStringLiteral("Déconnecté du serveur.");
        }
    }
    auto transferSlot = m_transferSession;
    (void)QtConcurrent::run(&m_transferPool, [transferSlot] { transferSlot->reset(); });
    m_transferSession = std::make_shared<std::unique_ptr<SshSession>>();
    emit transfersChanged();
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
    emit serversChanged();
    m_status = QStringLiteral("Disconnected. Working data is not stored on this device.");
    emit changed();
}
}
