#include "SshInternal.h"

namespace fortiq {
namespace {
// libssh2's global init is not per-request. No concurrent libssh2_exit calls.
std::once_flag sshInitFlag;
int sshInitResult = -1;
constexpr int kMaxEntries = 20000;
QString sshError(LIBSSH2_SESSION *session, const QString &prefix) {
    // Never echo unknown server text or credential data into the UI.
    return prefix + QStringLiteral(" (SSH code %1)").arg(libssh2_session_last_errno(session));
}
// S-1: the algorithms the client will negotiate. libssh2's own list (in ITS order, so the host-key type that is
// negotiated, and with it every saved pin, stays what it was) minus what is obsolete or weak: SHA-1 key exchange,
// SHA-1 (ssh-rsa) and DSA host keys, CBC / 3DES / RC4 / Blowfish / CAST ciphers, MD5 / SHA-1 MACs, "none".
bool obsoleteAlgorithm(int method, const QByteArray &name)
{
    const auto has = [&](std::initializer_list<const char *> parts) {
        for (const char *part : parts) if (name.contains(part)) return true;
        return false;
    };
    if (name.contains("none")) return true;
    switch (method) {
    case LIBSSH2_METHOD_KEX: return has({ "sha1", "group1-" });
    case LIBSSH2_METHOD_HOSTKEY: return name == "ssh-rsa" || has({ "dss", "sha1" }) || name.startsWith("ssh-rsa-cert");
    case LIBSSH2_METHOD_CRYPT_CS: case LIBSSH2_METHOD_CRYPT_SC: return has({ "cbc", "3des", "arcfour", "blowfish", "cast", "rijndael" });
    case LIBSSH2_METHOD_MAC_CS: case LIBSSH2_METHOD_MAC_SC: return has({ "md5", "sha1", "ripemd" });
    default: return false;
    }
}
// Applies the policy to a fresh session; false if a list would be empty or libssh2 refuses it.
bool applyAlgorithmPolicy(LIBSSH2_SESSION *session)
{
    for (const int method : { LIBSSH2_METHOD_KEX, LIBSSH2_METHOD_HOSTKEY, LIBSSH2_METHOD_CRYPT_CS, LIBSSH2_METHOD_CRYPT_SC,
                              LIBSSH2_METHOD_MAC_CS, LIBSSH2_METHOD_MAC_SC }) {
        const char **supported = nullptr;
        const int count = libssh2_session_supported_algs(session, method, &supported);
        if (count <= 0 || !supported) return false;
        QByteArrayList allowed;
        for (int i = 0; i < count; ++i)
            if (!obsoleteAlgorithm(method, QByteArray(supported[i]))) allowed << QByteArray(supported[i]);
        libssh2_free(session, static_cast<void *>(supported));
        if (method == LIBSSH2_METHOD_HOSTKEY) {
            // The host-key type decides the fingerprint a user has pinned. Keep the order that libssh2 1.11.1 negotiated (ECDSA before
            // Ed25519 before RSA/SHA-2) whatever the order of the library in use is, so that saved pins survive a libssh2 update.
            static const QByteArrayList preferred = { "ecdsa-sha2-nistp256", "ecdsa-sha2-nistp384", "ecdsa-sha2-nistp521", "ssh-ed25519",
                                                      "rsa-sha2-512", "rsa-sha2-256" };
            QByteArrayList ordered;
            for (const QByteArray &name : preferred) if (allowed.contains(name)) ordered << name;
            for (const QByteArray &name : allowed) if (!ordered.contains(name)) ordered << name;
            allowed = ordered;
        }
        if (allowed.isEmpty() || libssh2_session_method_pref(session, method, allowed.join(',').constData()) != 0) return false;
    }
    return true;
}
QString modeString(unsigned long mode) {
    QString s(10, QLatin1Char('-'));
    if (LIBSSH2_SFTP_S_ISDIR(mode)) s[0] = 'd';
    else if (LIBSSH2_SFTP_S_ISLNK(mode)) s[0] = 'l';
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i)
        if (mode & (1u << (8 - i))) s[i + 1] = QLatin1Char(rwx[i]);
    return s;
}
// Owner name from the server's display-only "ls -l" style longentry.
QString ownerFrom(const char *longentry, int length) {
    if (length <= 0) return {};
    const QList<QByteArray> parts = QByteArray(longentry, length).simplified().split(' ');
    return parts.size() > 3 ? QString::fromUtf8(parts.at(2)) : QString();
}
}

SshSession::SshSession() : d(std::make_unique<Impl>()) {}
SshSession::~SshSession() { d->close(); }
bool SshSession::isOpen() const { return d->sftp != nullptr; }
void SshSession::close() { d->close(); }


SshResult SshSession::open(const ConnectionRequest &req)
{
    d->close();
    if (req.host.trimmed().isEmpty() || req.user.trimmed().isEmpty() ||
        req.port < 1 || req.port > 65535 || (req.password.isEmpty() && req.keyPath.isEmpty())) {
        return fail(QStringLiteral("Provide host, port, user and a password or private key."));
    }
    if (!req.directory.isEmpty() && !req.directory.startsWith('/'))
        return fail(QStringLiteral("Invalid remote directory."));

    d->key = LinkMonitor::key(req.host, req.port, req.user);
    std::call_once(sshInitFlag, [] { sshInitResult = libssh2_init(0); });
    if (sshInitResult != 0) return fail(QStringLiteral("Échec d'initialisation de la bibliothèque SSH."));

    d->socket.connectToHost(req.host, static_cast<quint16>(req.port));
    if (!d->socket.waitForConnected(10000)) {
        d->close();
        LinkMonitor::reportLost(d->key, QStringLiteral("Serveur injoignable."));   // ignored for a server never reached
        return fail(QStringLiteral("Connexion TCP impossible ou délai dépassé."));
    }

    d->handle = libssh2_session_init();
    if (!d->handle) { d->close(); return fail(QStringLiteral("Échec de création de la session SSH.")); }
    libssh2_session_set_blocking(d->handle, 1);
    libssh2_session_set_timeout(d->handle, 15000);
    if (!applyAlgorithmPolicy(d->handle)) { d->close(); return fail(QStringLiteral("Cannot apply the SSH algorithm policy.")); }
    if (libssh2_session_handshake(d->handle,
            static_cast<libssh2_socket_t>(d->socket.socketDescriptor())) != 0) {
        const int code = libssh2_session_last_errno(d->handle);
        const SshResult r = fail(code == LIBSSH2_ERROR_METHOD_NONE || code == LIBSSH2_ERROR_KEX_FAILURE
            ? QStringLiteral("Le serveur ne propose que des algorithmes SSH obsolètes (échange de clés SHA-1, chiffrement CBC, MAC SHA-1 ou clé d'hôte ssh-rsa) : connexion refusée. Mettez à jour la configuration du serveur. (SSH code %1)").arg(code)
            : sshError(d->handle, QStringLiteral("Échec de la négociation SSH")));
        d->close();
        LinkMonitor::reportLost(d->key, QStringLiteral("Serveur injoignable."));
        return r;
    }

    size_t hostKeyLength = 0;
    int keyType = 0;
    const char *hostKey = libssh2_session_hostkey(d->handle, &hostKeyLength, &keyType);
    if (!hostKey || hostKeyLength == 0) {
        d->close();
        return fail(QStringLiteral("Server did not provide a usable host key."));
    }
    const QByteArray digest = QCryptographicHash::hash(
        QByteArray(hostKey, static_cast<qsizetype>(hostKeyLength)), QCryptographicHash::Sha256);
    d->fingerprint = QStringLiteral("SHA256:") + QString::fromLatin1(
        digest.toBase64(QByteArray::OmitTrailingEquals));
    if (req.expectedHostFingerprint.isEmpty()) {
        SshResult result;
        result.status = SshResult::Status::TrustRequired;
        result.fingerprint = d->fingerprint;
        result.message = QStringLiteral("Check the SSH host fingerprint with a trusted source before accepting it.");
        d->close();
        return result;
    }
    if (d->fingerprint != req.expectedHostFingerprint) {
        d->close();
        return fail(QStringLiteral("SSH HOST KEY CHANGED. Connection blocked. Verify server identity out of band."));
    }

    const QByteArray user = req.user.toUtf8();
    SecretBytes password = req.password.toUtf8();   // wiped by its destructor even on an early return
    int rc = 0;
    if (!req.keyPath.isEmpty()) {
        // QFile understands Android SAF grants; libssh2's native fopen does not.
        // The selected key stays in RAM only and is wiped after authentication.
        QFile keyFile(req.keyPath);
        constexpr qint64 maxKeyBytes = 1024 * 1024;
        if (!keyFile.open(QIODevice::ReadOnly)) {
            d->close();
            return fail(QStringLiteral("Cannot read the selected private key. Select it again if its document permission expired."));
        }
        SecretBytes key = keyFile.read(maxKeyBytes + 1);
        const bool complete = keyFile.error() == QFileDevice::NoError && keyFile.atEnd();
        keyFile.close();
        if (!complete || key.isEmpty() || key.size() > maxKeyBytes) {
            d->close();
            return fail(QStringLiteral("Private key is empty, unreadable or larger than 1 MiB."));
        }
        rc = libssh2_userauth_publickey_frommemory(d->handle, user.constData(), static_cast<size_t>(user.size()),
            nullptr, 0, key.constData(), static_cast<size_t>(key.size()), password.isEmpty() ? nullptr : password.constData());
        key.wipeNow();
    } else {
        rc = libssh2_userauth_password_ex(d->handle, user.constData(),
            static_cast<unsigned int>(user.size()), password.constData(),
            static_cast<unsigned int>(password.size()), nullptr);
    }
    password.wipeNow();
    if (rc != 0) {
        d->close();
        return fail(req.keyPath.isEmpty()
            ? QStringLiteral("SSH authentication failed (password).")
            : QStringLiteral("SSH authentication failed (private key). Check the key file, its passphrase and authorized_keys."));
    }

    // Keep idle NAT/firewall mappings alive for the persistent session.
    libssh2_keepalive_config(d->handle, 1, 30);
    d->sftp = libssh2_sftp_init(d->handle);
    if (!d->sftp) {
        const SshResult r = fail(sshError(d->handle, QStringLiteral("SFTP indisponible")));
        d->close();
        return r;
    }
    char buffer[4096];
    const int n = libssh2_sftp_realpath(d->sftp, ".", buffer, sizeof(buffer));
    if (n <= 0) { d->close(); return fail(QStringLiteral("Cannot resolve remote home directory.")); }
    d->home = QString::fromUtf8(buffer, n);

    SshResult result = list(req.directory.isEmpty() ? d->home : req.directory);
    if (result.status == SshResult::Status::Ok)
        result.message = QStringLiteral("Connected to Linux over verified SSH/SFTP.");
    return result;   // list() reported the link as working
}

SshResult SshSession::list(const QString &remoteDir)
{
    if (!isOpen()) return fail(QStringLiteral("La session SSH n'est pas ouverte."));
    if (!remoteDir.startsWith('/'))
        return fail(QStringLiteral("Remote path is not absolute."));
    const QByteArray dirUtf8 = remoteDir.toUtf8();
    LIBSSH2_SFTP_HANDLE *dir = libssh2_sftp_opendir(d->sftp, dirUtf8.constData());
    if (!dir) {
        // A dead transport surfaces as a session-level error, not an SFTP status.
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->closeLost();
            return fail(QStringLiteral("Connexion SSH perdue."));
        }
        return fail(QStringLiteral("Cannot open remote folder (permission or path error)."));
    }

    QVariantList entries;
    char name[8192];
    char longentry[8192];
    while (true) {
        LIBSSH2_SFTP_ATTRIBUTES attr{};
        const int n = libssh2_sftp_readdir_ex(dir, name, sizeof(name), longentry, sizeof(longentry), &attr);
        if (n == 0) break;
        if (n < 0) {
            const bool dead = libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL;
            libssh2_sftp_closedir(dir);
            if (dead) { d->closeLost(); return fail(QStringLiteral("Connexion SSH perdue.")); }
            return fail(QStringLiteral("Remote directory read failed."));
        }
        const QByteArray rawName(name, n);
        const QString fileName = QString::fromUtf8(rawName);
        if (fileName.toUtf8() != rawName) continue; // no lossy path reconstruction
        const auto child = safeChildPath(remoteDir, fileName);
        if (!child) continue;
        const bool hasMode = attr.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS;
        const bool isDirectory = hasMode && LIBSSH2_SFTP_S_ISDIR(attr.permissions);
        // Link targets are not stat'ed here: one round trip per link made
        // listings of / or /etc seconds slow. Opening a link resolves it.
        const bool isLink = hasMode && LIBSSH2_SFTP_S_ISLNK(attr.permissions);
        QVariantMap entry;
        entry.insert("name", fileName);
        entry.insert("directory", isDirectory);
        entry.insert("link", isLink);
        entry.insert("hidden", fileName.startsWith('.'));
        entry.insert("path", *child);
        entry.insert("size", static_cast<double>((attr.flags & LIBSSH2_SFTP_ATTR_SIZE) ? attr.filesize : 0));
        entry.insert("mtime", static_cast<double>((attr.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? attr.mtime : 0));
        entry.insert("mode", hasMode ? modeString(attr.permissions) : QString());
        entry.insert("owner", ownerFrom(longentry, static_cast<int>(qstrnlen(longentry, sizeof(longentry)))));
        entries.push_back(entry);
        if (entries.size() > kMaxEntries) {
            libssh2_sftp_closedir(dir);
            return fail(QStringLiteral("Directory has more than %1 entries; paging is not implemented yet.").arg(kMaxEntries));
        }
    }
    libssh2_sftp_closedir(dir);
    std::sort(entries.begin(), entries.end(), [](const QVariant &a, const QVariant &b) {
        const auto aa = a.toMap(), bb = b.toMap();
        if (aa.value("directory").toBool() != bb.value("directory").toBool())
            return aa.value("directory").toBool();
        return aa.value("name").toString().compare(bb.value("name").toString(), Qt::CaseInsensitive) < 0;
    });
    SshResult result;
    result.status = SshResult::Status::Ok;
    result.fingerprint = d->fingerprint;
    result.directory = remoteDir;
    result.home = d->home;
    result.entries = entries;
    result.message = QStringLiteral("%1 items").arg(entries.size());
    LinkMonitor::reportOk(d->key);
    return result;
}

SshResult SshSession::readFile(const QString &path, qint64 maxBytes)
{
    if (!isOpen()) return fail(QStringLiteral("La session SSH n'est pas ouverte."));
    if (!path.startsWith('/') || maxBytes <= 0) return fail(QStringLiteral("Invalid remote file."));
    const QByteArray pathUtf8 = path.toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES attr{};
    if (libssh2_sftp_stat_ex(d->sftp, pathUtf8.constData(), static_cast<unsigned int>(pathUtf8.size()),
            LIBSSH2_SFTP_STAT, &attr) != 0) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->closeLost();
            return fail(QStringLiteral("Connexion SSH perdue."));
        }
        return fail(QStringLiteral("Cannot read file (permission or path error)."));
    }
    if ((attr.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && !LIBSSH2_SFTP_S_ISREG(attr.permissions))
        return fail(QStringLiteral("Not a regular file."));
    // Read-only open: the viewer never creates, truncates or locks the file.
    LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open_ex(d->sftp, pathUtf8.constData(),
        static_cast<unsigned int>(pathUtf8.size()), LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE);
    if (!file) return fail(QStringLiteral("Cannot open file (permission error)."));

    SshResult result;
    result.fileSize = (attr.flags & LIBSSH2_SFTP_ATTR_SIZE) ? static_cast<qint64>(attr.filesize) : -1;
    QByteArray data;
    data.reserve(static_cast<qsizetype>(std::min<qint64>(maxBytes, result.fileSize >= 0 ? result.fileSize : maxBytes)));
    char buffer[65536];
    while (data.size() < maxBytes) {
        const size_t want = static_cast<size_t>(std::min<qint64>(sizeof(buffer), maxBytes - data.size()));
        const auto n = libssh2_sftp_read(file, buffer, want);
        if (n == 0) break;
        if (n < 0) {
            libssh2_sftp_close(file);
            return fail(QStringLiteral("Remote file read failed."));
        }
        data.append(buffer, static_cast<qsizetype>(n));
    }
    libssh2_sftp_close(file);
    result.truncated = result.fileSize > data.size() || (result.fileSize < 0 && data.size() >= maxBytes);
    result.status = SshResult::Status::Ok;
    result.directory = path;
    result.data = std::move(data);
    result.message = QStringLiteral("%1 bytes read").arg(result.data.size());
    return result;
}

QString SshSession::homeDirectory() const { return d->home; }

SshResult SshClient::list(const ConnectionRequest &request)
{
    SshSession session;
    return session.open(request);
}
}  // namespace fortiq
