#include "RemoteControllerInternal.h"

namespace fortiq {
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
        m_status = r.notFound ? QStringLiteral("The remembered password is no longer in the protected store; enter it again.")
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
    m_password.wipeNow();
}

void RemoteController::connectServer(QString host, int port, QString user, QString password, QString keyPath) {
    if (m_busy) return;
    disconnectServer();
    m_activeServerId.clear();
    m_host = host.trimmed();
    m_port = port;
    m_user = user.trimmed();
    m_password = password;
    wipe(password);   // this handle only; the buffer is zeroed by the last holder (m_password / the task copies)
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
}  // namespace fortiq
