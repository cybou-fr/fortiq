#include "SshClient.h"
#include "../core/RemotePath.h"
#include "../core/LocalDocument.h"
#include <QTemporaryFile>

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QUrl>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpSocket>
#include <QVariantMap>
#include <algorithm>
#include <mutex>
#include <vector>

#include <libssh2.h>
#include <libssh2_sftp.h>
#ifdef FORTIQ_PUBLICATION_TESTS
#include "PublicationHooks.h"
#endif

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
SshResult fail(const QString &reason) {
    SshResult result; result.message = reason; return result;
}
int replaceFile(LIBSSH2_SESSION *session, LIBSSH2_SFTP *sftp, const QByteArray &from, const QByteArray &to) {
#ifdef FORTIQ_PUBLICATION_TESTS
    if (publication_test::replace) return publication_test::replace(session, sftp, from, to);
#else
    Q_UNUSED(session);
#endif
    return libssh2_sftp_posix_rename_ex(sftp, from.constData(), static_cast<size_t>(from.size()),
                                      to.constData(), static_cast<size_t>(to.size()));
}
// Unsupported fsync extensions do not imply durable storage. Every other
// flush error and every CLOSE error must prevent publication of the staging file.
bool flushFile(LIBSSH2_SFTP *sftp, LIBSSH2_SFTP_HANDLE *file) {
    int rc;
#ifdef FORTIQ_PUBLICATION_TESTS
    if (publication_test::flush) rc = publication_test::flush(file);
    else
#endif
    rc = libssh2_sftp_fsync(file);
    return rc == 0 || rc == LIBSSH2_ERROR_METHOD_NOT_SUPPORTED
        || (rc == LIBSSH2_ERROR_SFTP_PROTOCOL
            && libssh2_sftp_last_error(sftp) == LIBSSH2_FX_OP_UNSUPPORTED);
}
int setFileAttributes(LIBSSH2_SFTP_HANDLE *file, LIBSSH2_SFTP_ATTRIBUTES *attr) {
#ifdef FORTIQ_PUBLICATION_TESTS
    if (publication_test::attributes) return publication_test::attributes(file, attr);
#endif
    return libssh2_sftp_fstat_ex(file, attr, 1);
}
int closeStagingFile(LIBSSH2_SFTP_HANDLE *file) {
#ifdef FORTIQ_PUBLICATION_TESTS
    if (publication_test::close) return publication_test::close(file);
#endif
    return libssh2_sftp_close(file);
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

struct SshSession::Impl {
    QTcpSocket socket;
    LIBSSH2_SESSION *handle = nullptr;
    LIBSSH2_SFTP *sftp = nullptr;
    QString fingerprint;
    QString home;

    void close() {
        if (sftp) { libssh2_sftp_shutdown(sftp); sftp = nullptr; }
        if (handle) {
            libssh2_session_disconnect_ex(handle, SSH_DISCONNECT_BY_APPLICATION, "FORTIQ disconnect", "");
            libssh2_session_free(handle);
            handle = nullptr;
        }
        socket.abort();
        home.clear();
    }
};

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

    std::call_once(sshInitFlag, [] { sshInitResult = libssh2_init(0); });
    if (sshInitResult != 0) return fail(QStringLiteral("SSH library initialization failed."));

    d->socket.connectToHost(req.host, static_cast<quint16>(req.port));
    if (!d->socket.waitForConnected(10000)) {
        d->close();
        return fail(QStringLiteral("TCP connection failed or timed out."));
    }

    d->handle = libssh2_session_init();
    if (!d->handle) { d->close(); return fail(QStringLiteral("SSH session allocation failed.")); }
    libssh2_session_set_blocking(d->handle, 1);
    libssh2_session_set_timeout(d->handle, 15000);
    if (libssh2_session_handshake(d->handle,
            static_cast<libssh2_socket_t>(d->socket.socketDescriptor())) != 0) {
        const SshResult r = fail(sshError(d->handle, QStringLiteral("SSH handshake failed")));
        d->close();
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
    QByteArray password = req.password.toUtf8();
    int rc = 0;
    if (!req.keyPath.isEmpty()) {
        // QFile understands Android SAF grants; libssh2's native fopen does not.
        // The selected key stays in RAM only and is wiped after authentication.
        QFile keyFile(req.keyPath);
        constexpr qint64 maxKeyBytes = 1024 * 1024;
        if (!keyFile.open(QIODevice::ReadOnly)) {
            password.fill(char(0)); d->close();
            return fail(QStringLiteral("Cannot read the selected private key. Select it again if its document permission expired."));
        }
        QByteArray key = keyFile.read(maxKeyBytes + 1);
        const bool complete = keyFile.error() == QFileDevice::NoError && keyFile.atEnd();
        keyFile.close();
        if (!complete || key.isEmpty() || key.size() > maxKeyBytes) {
            key.fill(char(0)); password.fill(char(0)); d->close();
            return fail(QStringLiteral("Private key is empty, unreadable or larger than 1 MiB."));
        }
        rc = libssh2_userauth_publickey_frommemory(d->handle, user.constData(), static_cast<size_t>(user.size()),
            nullptr, 0, key.constData(), static_cast<size_t>(key.size()), password.isEmpty() ? nullptr : password.constData());
        key.fill(char(0));
    } else {
        rc = libssh2_userauth_password_ex(d->handle, user.constData(),
            static_cast<unsigned int>(user.size()), password.constData(),
            static_cast<unsigned int>(password.size()), nullptr);
    }
    password.fill(char(0));
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
        const SshResult r = fail(sshError(d->handle, QStringLiteral("SFTP unavailable")));
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
    return result;
}

SshResult SshSession::list(const QString &remoteDir)
{
    if (!isOpen()) return fail(QStringLiteral("SSH session is not open."));
    if (!remoteDir.startsWith('/'))
        return fail(QStringLiteral("Remote path is not absolute."));
    const QByteArray dirUtf8 = remoteDir.toUtf8();
    LIBSSH2_SFTP_HANDLE *dir = libssh2_sftp_opendir(d->sftp, dirUtf8.constData());
    if (!dir) {
        // A dead transport surfaces as a session-level error, not an SFTP status.
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->close();
            return fail(QStringLiteral("SSH connection lost."));
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
            libssh2_sftp_closedir(dir);
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
    return result;
}

SshResult SshSession::readFile(const QString &path, qint64 maxBytes)
{
    if (!isOpen()) return fail(QStringLiteral("SSH session is not open."));
    if (!path.startsWith('/') || maxBytes <= 0) return fail(QStringLiteral("Invalid remote file."));
    const QByteArray pathUtf8 = path.toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES attr{};
    if (libssh2_sftp_stat_ex(d->sftp, pathUtf8.constData(), static_cast<unsigned int>(pathUtf8.size()),
            LIBSSH2_SFTP_STAT, &attr) != 0) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->close();
            return fail(QStringLiteral("SSH connection lost."));
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

namespace {
SshResult transferFail(const QString &reason, const QString &code = {}) {
    SshResult r; r.message = reason; r.code = code; return r;
}
QString randomTag() {
    return QString::number(QRandomGenerator::global()->generate64(), 36);
}
// "report.pdf" -> "report (2).pdf"; dotfiles keep their leading dot.
QString numberedName(const QString &name, int n) {
    const int dot = name.lastIndexOf('.');
    if (dot <= 0) return QStringLiteral("%1 (%2)").arg(name).arg(n);
    return QStringLiteral("%1 (%2)%3").arg(name.left(dot)).arg(n).arg(name.mid(dot));
}
}

SshResult SshSession::download(const QString &remotePath, const QString &localPath, const TransferProgress &progress)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    const auto clean = cleanAbsolutePath(remotePath);
    if (!clean || localPath.isEmpty()) return transferFail(QStringLiteral("Invalid transfer paths."));
    const QByteArray pathUtf8 = clean->toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES attr{};
    int statResult;
#ifdef FORTIQ_PUBLICATION_TESTS
    if (publication_test::downloadStat)
        statResult = publication_test::downloadStat(d->handle, d->sftp, pathUtf8, &attr);
    else
#endif
        statResult = libssh2_sftp_stat_ex(d->sftp, pathUtf8.constData(), static_cast<unsigned int>(pathUtf8.size()),
                                         LIBSSH2_SFTP_STAT, &attr);
    if (statResult != 0) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->close();
            return transferFail(QStringLiteral("SSH connection lost before download started."), QStringLiteral("disconnected"));
        }
        return transferFail(QStringLiteral("Cannot read remote file (permission or path error; SFTP %1).")
                            .arg(libssh2_sftp_last_error(d->sftp)));
    }
    if ((attr.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && !LIBSSH2_SFTP_S_ISREG(attr.permissions))
        return transferFail(QStringLiteral("Only regular files can be downloaded."));
    const qint64 total = (attr.flags & LIBSSH2_SFTP_ATTR_SIZE) ? static_cast<qint64>(attr.filesize) : -1;

    LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open_ex(d->sftp, pathUtf8.constData(),
        static_cast<unsigned int>(pathUtf8.size()), LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE);
    if (!file) return transferFail(QStringLiteral("Cannot open remote file (permission error)."));

    const bool document = isContentDocument(localPath);
    QSaveFile native(localPath);
    QTemporaryFile staging;
    if (document) {
        const QString pathTemplate = documentStagingTemplate();
        if (pathTemplate.isEmpty()) {
            libssh2_sftp_close(file);
            return transferFail(QStringLiteral("Cannot prepare private download storage."));
        }
        staging.setFileTemplate(pathTemplate);
    }
    QFileDevice &out = document ? static_cast<QFileDevice &>(staging) : static_cast<QFileDevice &>(native);
    if (!(document ? staging.open() : native.open(QIODevice::WriteOnly))) {
        libssh2_sftp_close(file);
        return transferFail(QStringLiteral("Cannot create a private download staging file."));
    }
    auto cancelLocal = [&] { if (!document) native.cancelWriting(); };
    std::vector<char> buffer(256 * 1024);
    qint64 done = 0;
    while (true) {
        const auto n = libssh2_sftp_read(file, buffer.data(), buffer.size());
        if (n == 0) break;
        if (n < 0) {
            libssh2_sftp_close(file);
            cancelLocal();
            if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) d->close();
            return transferFail(QStringLiteral("Download interrupted; the local file was not created."));
        }
        if (out.write(buffer.data(), n) != n) {
            libssh2_sftp_close(file);
            cancelLocal();
            return transferFail(QStringLiteral("Local write failed (disk full?)."));
        }
        done += n;
        if (progress && !progress(done, total)) {
            libssh2_sftp_close(file);
            cancelLocal();
            return transferFail(QStringLiteral("Cancelled; the local file was not created."), QStringLiteral("cancelled"));
        }
    }
    libssh2_sftp_close(file);
    if (total >= 0 && done != total) {
        cancelLocal();
        return transferFail(QStringLiteral("Size mismatch (%1 of %2 bytes); the local file was not created.").arg(done).arg(total));
    }
    if (document) {
        if (!staging.flush()) return transferFail(QStringLiteral("Download staging flush failed; the selected document was not opened."));
        const auto result = exportDocument(staging, [&]() -> std::unique_ptr<QIODevice> {
            auto destination = std::make_unique<QFile>(localPath);
            if (!destination->open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
            return destination;
        }, [&] { return !progress || progress(done, done); });
        if (!result.ok) return transferFail(result.attempted
            ? QStringLiteral("Document export did not finish. The selected document may contain partial data; check it before retrying.")
            : QStringLiteral("Download was not exported; the selected document was not opened."),
            result.attempted ? QStringLiteral("unknown") : (result.cancelled ? QStringLiteral("cancelled") : QString()));
    } else if (!native.commit()) return transferFail(QStringLiteral("Cannot finalise the local file: %1").arg(native.errorString()));
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = localPath;
    r.bytes = done;
    r.message = QStringLiteral("Downloaded");
    return r;
}

SshResult SshSession::upload(const QString &localPath, const QString &remoteDir, const QString &name,
                             ConflictPolicy policy, const TransferProgress &progress)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    const auto dir = cleanAbsolutePath(remoteDir);
    const auto target = dir ? safeChildPath(*dir, name) : std::nullopt;
    if (!target) return transferFail(QStringLiteral("Invalid remote destination."));

    QFile in(localPath);
    if (!in.open(QIODevice::ReadOnly)) return transferFail(QStringLiteral("Cannot read the local file: %1").arg(in.errorString()));
    if (!isContentDocument(localPath) && !QFileInfo(localPath).isFile()) return transferFail(QStringLiteral("Only regular files can be uploaded."));
    const qint64 size = in.size();
    const qint64 total = isContentDocument(localPath) && (size <= 0 || in.isSequential()) ? -1 : size;

    auto exists = [this](const QString &path) {
        const QByteArray p = path.toUtf8();
        LIBSSH2_SFTP_ATTRIBUTES a{};
        return libssh2_sftp_stat_ex(d->sftp, p.constData(), static_cast<unsigned int>(p.size()), LIBSSH2_SFTP_LSTAT, &a) == 0;
    };
    QString finalPath = *target;
    std::optional<LIBSSH2_SFTP_ATTRIBUTES> replacement;
    if (policy == ConflictPolicy::Replace) {
        const QByteArray p = finalPath.toUtf8();
        LIBSSH2_SFTP_ATTRIBUTES attr{};
        if (libssh2_sftp_stat_ex(d->sftp, p.constData(), static_cast<unsigned int>(p.size()), LIBSSH2_SFTP_LSTAT, &attr) == 0) {
            const unsigned long needed = LIBSSH2_SFTP_ATTR_PERMISSIONS | LIBSSH2_SFTP_ATTR_UIDGID;
            if ((attr.flags & needed) != needed || !LIBSSH2_SFTP_S_ISREG(attr.permissions))
                return transferFail(QStringLiteral("Only a regular file with known permissions and owner can be replaced."));
            replacement = attr;
        } else if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL
                   || libssh2_sftp_last_error(d->sftp) != LIBSSH2_FX_NO_SUCH_FILE) {
            return transferFail(QStringLiteral("Cannot inspect the destination; replacement was not attempted."));
        }
    }
    if (exists(finalPath)) {
        if (policy == ConflictPolicy::Fail)
            return transferFail(QStringLiteral("« %1 » already exists in this folder.").arg(name), QStringLiteral("conflict"));
        if (policy == ConflictPolicy::KeepBoth) {
            for (int n = 2; n < 1000 && exists(finalPath); ++n) finalPath = *safeChildPath(*dir, numberedName(name, n));
            if (exists(finalPath)) return transferFail(QStringLiteral("No free name for this upload."), QStringLiteral("conflict"));
        }
    }

    // Unique, exclusively created temporary owned by this transfer.
    const QString tempPath = *safeChildPath(*dir, QStringLiteral(".%1.fortiq-upload-%2").arg(name.left(80), randomTag()));
    const QByteArray tempUtf8 = tempPath.toUtf8();
    LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open_ex(d->sftp, tempUtf8.constData(),
        static_cast<unsigned int>(tempUtf8.size()), LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL,
        0644, LIBSSH2_SFTP_OPENFILE);
    if (!file) return transferFail(QStringLiteral("Cannot create a file in this folder (permission error?)."));
    auto removeTemp = [this, &tempUtf8] {
        if (!d->sftp) return;
        libssh2_sftp_unlink_ex(d->sftp, tempUtf8.constData(), static_cast<unsigned int>(tempUtf8.size()));
    };

    // Capture the server's creation mode (including its umask) while empty,
    // then restrict the staging file before writing any source bytes.
    LIBSSH2_SFTP_ATTRIBUTES created{};
    const bool creationKnown = libssh2_sftp_fstat_ex(file, &created, 0) == 0
        && (created.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS);
    LIBSSH2_SFTP_ATTRIBUTES privateMode{};
    privateMode.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    privateMode.permissions = 0600;
    const bool restricted = creationKnown && setFileAttributes(file, &privateMode) == 0;
    LIBSSH2_SFTP_ATTRIBUTES secured{};
    const bool privateVerified = restricted && libssh2_sftp_fstat_ex(file, &secured, 0) == 0
        && (secured.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && (secured.permissions & 07777) == 0600;
    if (!privateVerified) {
        libssh2_sftp_close(file); removeTemp();
        return transferFail(QStringLiteral("Cannot secure the empty staging file; no content was uploaded."));
    }

    std::vector<char> buffer(256 * 1024);
    qint64 done = 0;
    while (true) {
        const qint64 n = in.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (n < 0) { libssh2_sftp_close(file); removeTemp(); return transferFail(QStringLiteral("Local read failed.")); }
        if (n == 0) break;
        qint64 off = 0;
        while (off < n) {
            const auto w = libssh2_sftp_write(file, buffer.data() + off, static_cast<size_t>(n - off));
            if (w <= 0) {
                const bool lost = libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL;
                libssh2_sftp_close(file);
                if (lost) {
                    d->close();
                    // Nothing was published; only the owned temporary may remain.
                    return transferFail(QStringLiteral("Connection lost during upload; nothing was published (a hidden temporary %1 may remain).")
                                            .arg(tempPath.section('/', -1)));
                }
                removeTemp();
                return transferFail(QStringLiteral("Remote write failed (quota or disk full?)."));
            }
            off += w;
        }
        done += n;
        if (progress && !progress(done, total)) {
            libssh2_sftp_close(file);
            removeTemp();
            return transferFail(QStringLiteral("Cancelled before publication; nothing changed on the server."), QStringLiteral("cancelled"));
        }
    }
    LIBSSH2_SFTP_ATTRIBUTES mine{};
    const bool mineOk = libssh2_sftp_fstat_ex(file, &mine, 0) == 0 && (mine.flags & LIBSSH2_SFTP_ATTR_UIDGID);
    bool metadataOk = mineOk && (!replacement || mine.uid == replacement->uid);
    if (metadataOk && replacement && mine.gid != replacement->gid) {
        LIBSSH2_SFTP_ATTRIBUTES group{};
        group.flags = LIBSSH2_SFTP_ATTR_UIDGID;
        group.uid = mine.uid; group.gid = replacement->gid;
        metadataOk = setFileAttributes(file, &group) == 0;
    }
    LIBSSH2_SFTP_ATTRIBUTES mode{};
    mode.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    mode.permissions = replacement ? replacement->permissions & 07777 : created.permissions & 0777;
    metadataOk = metadataOk && setFileAttributes(file, &mode) == 0;
    const bool flushed = flushFile(d->sftp, file);
    LIBSSH2_SFTP_ATTRIBUTES written{};
    const bool statOk = libssh2_sftp_fstat_ex(file, &written, 0) == 0;
    const bool closed = closeStagingFile(file) == 0;
    const unsigned long required = LIBSSH2_SFTP_ATTR_SIZE | LIBSSH2_SFTP_ATTR_PERMISSIONS | LIBSSH2_SFTP_ATTR_UIDGID;
    const bool metadataVerified = (written.flags & required) == required && (written.permissions & 07777) == mode.permissions
        && written.uid == (replacement ? replacement->uid : mine.uid)
        && written.gid == (replacement ? replacement->gid : mine.gid);
    if (!metadataOk || !metadataVerified || !flushed || !closed || (total >= 0 && done != total) || !statOk || static_cast<qint64>(written.filesize) != done) {
        removeTemp();
        return transferFail(QStringLiteral("Staging size, metadata, flush or close verification failed; nothing was published."));
    }
    if (progress && !progress(done, done)) {
        removeTemp();
        return transferFail(QStringLiteral("Cancelled before publication; the destination was not changed."), QStringLiteral("cancelled"));
    }

    // Replace is one POSIX rename: the old pathname stays visible until
    // publication. Fail/KeepBoth retain SFTP v3's no-overwrite semantics.
    const QByteArray finalUtf8 = finalPath.toUtf8();
#ifdef FORTIQ_PUBLICATION_TESTS
    if (publication_test::staged) publication_test::staged();
#endif
    if (replacement) {
        LIBSSH2_SFTP_ATTRIBUTES latest{};
        const unsigned long flags = LIBSSH2_SFTP_ATTR_PERMISSIONS | LIBSSH2_SFTP_ATTR_UIDGID;
        if (libssh2_sftp_stat_ex(d->sftp, finalUtf8.constData(), static_cast<unsigned int>(finalUtf8.size()),
                               LIBSSH2_SFTP_LSTAT, &latest) != 0
            || (latest.flags & flags) != flags || !LIBSSH2_SFTP_S_ISREG(latest.permissions)
            || (latest.permissions & 07777) != (replacement->permissions & 07777)
            || latest.uid != replacement->uid || latest.gid != replacement->gid) {
            removeTemp();
            return transferFail(QStringLiteral("Destination type, permissions or owner/group changed during upload."), QStringLiteral("conflict"));
        }
    }
    const int published = replacement
        ? replaceFile(d->handle, d->sftp, tempUtf8, finalUtf8)
        : libssh2_sftp_rename_ex(d->sftp, tempUtf8.constData(), static_cast<unsigned int>(tempUtf8.size()),
                                finalUtf8.constData(), static_cast<unsigned int>(finalUtf8.size()), 0);
    if (published != 0) {
        if (!isOpen() || (published != LIBSSH2_ERROR_METHOD_NOT_SUPPORTED
            && libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL)) {
            d->close();
            return transferFail(QStringLiteral("Connection lost while publishing; check the folder before retrying."), QStringLiteral("unknown"));
        }
        removeTemp();
        if (replacement)
            return transferFail(QStringLiteral("Atomic replacement was refused (unsupported extension or permissions); the original was not moved or removed."));
        return transferFail(QStringLiteral("Cannot publish « %1 » (name taken or permission denied); nothing was replaced.").arg(finalPath.section('/', -1)),
                            QStringLiteral("conflict"));
    }

    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = finalPath;
    r.bytes = done;
    r.message = QStringLiteral("Uploaded");
    return r;
}

QString SshSession::homeDirectory() const { return d->home; }

namespace {
bool validName(const QString &name) {
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/')
        && !name.contains(QChar::Null) && name.toUtf8().size() <= 255;
}
}

SshResult SshSession::makeDirectory(const QString &dir, const QString &name)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    const auto parent = cleanAbsolutePath(dir);
    if (!parent || !validName(name.trimmed())) return transferFail(QStringLiteral("Invalid folder name."));
    const QString target = *safeChildPath(*parent, name.trimmed());
    const QByteArray p = target.toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES a{};
    if (libssh2_sftp_stat_ex(d->sftp, p.constData(), static_cast<unsigned int>(p.size()), LIBSSH2_SFTP_LSTAT, &a) == 0)
        return transferFail(QStringLiteral("« %1 » already exists.").arg(name.trimmed()), QStringLiteral("conflict"));
    if (libssh2_sftp_mkdir_ex(d->sftp, p.constData(), static_cast<unsigned int>(p.size()), 0755) != 0) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->close();
            return transferFail(QStringLiteral("Connection lost; check whether the folder was created."), QStringLiteral("unknown"));
        }
        return transferFail(QStringLiteral("Cannot create the folder (permission error?)."));
    }
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = target;
    r.message = QStringLiteral("Folder created");
    return r;
}

SshResult SshSession::renameEntry(const QString &path, const QString &newName)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    const auto source = cleanAbsolutePath(path);
    if (!source || *source == "/" || !validName(newName.trimmed())) return transferFail(QStringLiteral("Invalid name."));
    const auto target = safeChildPath(parentPath(*source), newName.trimmed());
    if (!target) return transferFail(QStringLiteral("Invalid name."));
    if (*target == *source) return transferFail(QStringLiteral("The name is unchanged."));
    const QByteArray from = source->toUtf8(), to = target->toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES a{};
    if (libssh2_sftp_stat_ex(d->sftp, to.constData(), static_cast<unsigned int>(to.size()), LIBSSH2_SFTP_LSTAT, &a) == 0)
        return transferFail(QStringLiteral("« %1 » already exists.").arg(newName.trimmed()), QStringLiteral("conflict"));
    // SFTP v3 rename never replaces an existing name.
    if (libssh2_sftp_rename_ex(d->sftp, from.constData(), static_cast<unsigned int>(from.size()),
                               to.constData(), static_cast<unsigned int>(to.size()), 0) != 0) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->close();
            return transferFail(QStringLiteral("Connection lost; check the folder before retrying."), QStringLiteral("unknown"));
        }
        return transferFail(QStringLiteral("Cannot rename (permission error or name taken meanwhile)."));
    }
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = *target;
    r.message = QStringLiteral("Renamed");
    return r;
}

SshResult SshSession::moveToTrash(const QString &path)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    const auto source = cleanAbsolutePath(path);
    const QString trash = d->home + QStringLiteral("/.local/share/Trash");
    if (!source || *source == "/" || d->home.isEmpty() || *source == d->home
        || d->home.startsWith(*source + "/") || trash.startsWith(*source + "/") || *source == trash)
        return transferFail(QStringLiteral("This location cannot be moved to the Trash."));
    if (source->startsWith(trash + "/"))
        return transferFail(QStringLiteral("Already in the Trash. Permanent deletion is not available yet."));

    auto statPath = [this](const QString &p, LIBSSH2_SFTP_ATTRIBUTES *a) {
        const QByteArray b = p.toUtf8();
        return libssh2_sftp_stat_ex(d->sftp, b.constData(), static_cast<unsigned int>(b.size()), LIBSSH2_SFTP_LSTAT, a) == 0;
    };
    auto ensureDir = [this, &statPath](const QString &p) {
        LIBSSH2_SFTP_ATTRIBUTES a{};
        if (statPath(p, &a)) return LIBSSH2_SFTP_S_ISDIR(a.permissions);
        const QByteArray b = p.toUtf8();
        return libssh2_sftp_mkdir_ex(d->sftp, b.constData(), static_cast<unsigned int>(b.size()), 0700) == 0;
    };
    LIBSSH2_SFTP_ATTRIBUTES srcAttr{};
    if (!statPath(*source, &srcAttr)) return transferFail(QStringLiteral("The item no longer exists."));
    for (const QString &p : { d->home + "/.local", d->home + "/.local/share", trash, trash + "/files", trash + "/info" })
        if (!ensureDir(p)) return transferFail(QStringLiteral("Cannot prepare the Trash folder in your home."));

    // Reserve a unique name by exclusively creating its .trashinfo first,
    // as the specification requires; files/<name> is then the rename target.
    const QString base = source->section('/', -1);
    QString name = base;
    LIBSSH2_SFTP_HANDLE *info = nullptr;
    QByteArray infoPath;
    for (int n = 1; n < 1000 && !info; ++n) {
        name = n == 1 ? base : numberedName(base, n);
        LIBSSH2_SFTP_ATTRIBUTES a{};
        if (statPath(trash + "/files/" + name, &a)) continue;
        infoPath = (trash + "/info/" + name + ".trashinfo").toUtf8();
        info = libssh2_sftp_open_ex(d->sftp, infoPath.constData(), static_cast<unsigned int>(infoPath.size()),
            LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL, 0600, LIBSSH2_SFTP_OPENFILE);
    }
    if (!info) return transferFail(QStringLiteral("Cannot reserve a name in the Trash."));
    const QByteArray content = "[Trash Info]\nPath=" + QUrl::toPercentEncoding(*source, "/")
        + "\nDeletionDate=" + QDateTime::currentDateTime().toString(Qt::ISODate).left(19).toUtf8() + "\n";
    const auto wrote = libssh2_sftp_write(info, content.constData(), static_cast<size_t>(content.size()));
    const bool written = static_cast<qsizetype>(wrote) == content.size();
    libssh2_sftp_close(info);
    auto dropInfo = [this, &infoPath] {
        libssh2_sftp_unlink_ex(d->sftp, infoPath.constData(), static_cast<unsigned int>(infoPath.size()));
    };
    if (!written) { dropInfo(); return transferFail(QStringLiteral("Cannot write the Trash record.")); }

    const QByteArray from = source->toUtf8(), to = (trash + "/files/" + name).toUtf8();
    if (libssh2_sftp_rename_ex(d->sftp, from.constData(), static_cast<unsigned int>(from.size()),
                               to.constData(), static_cast<unsigned int>(to.size()), 0) != 0) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->close();
            return transferFail(QStringLiteral("Connection lost; check the folder and the Trash before retrying."), QStringLiteral("unknown"));
        }
        dropInfo();
        // No copy-and-delete fallback: that would not be reversible or atomic.
        return transferFail(QStringLiteral("Cannot move to the Trash (permission error, or the item is on another filesystem)."));
    }
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = trash + "/files/" + name;
    r.message = QStringLiteral("Moved to the Trash");
    return r;
}

namespace {
// Removes a tree without following symlinks. Returns false on the first
// failure; `removed` counts deleted entries.
bool removeTree(LIBSSH2_SFTP *sftp, const QString &path, int depth, qint64 &removed)
{
    if (depth > 128) return false;
    const QByteArray p = path.toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES a{};
    if (libssh2_sftp_stat_ex(sftp, p.constData(), static_cast<unsigned int>(p.size()), LIBSSH2_SFTP_LSTAT, &a) != 0)
        return false;
    if (!LIBSSH2_SFTP_S_ISDIR(a.permissions)) {
        if (libssh2_sftp_unlink_ex(sftp, p.constData(), static_cast<unsigned int>(p.size())) != 0) return false;
        ++removed;
        return true;
    }
    QStringList children;
    LIBSSH2_SFTP_HANDLE *dir = libssh2_sftp_opendir(sftp, p.constData());
    if (!dir) return false;
    char name[8192];
    while (true) {
        LIBSSH2_SFTP_ATTRIBUTES ca{};
        const int n = libssh2_sftp_readdir_ex(dir, name, sizeof(name), nullptr, 0, &ca);
        if (n == 0) break;
        if (n < 0) { libssh2_sftp_closedir(dir); return false; }
        const QByteArray raw(name, n);
        const QString child = QString::fromUtf8(raw);
        if (child == "." || child == "..") continue;
        if (child.toUtf8() != raw) { libssh2_sftp_closedir(dir); return false; } // non-UTF-8 name: refuse, not guess
        children << child;
    }
    libssh2_sftp_closedir(dir);
    for (const QString &child : children) {
        const auto childPath = safeChildPath(path, child);
        if (!childPath || !removeTree(sftp, *childPath, depth + 1, removed)) return false;
    }
    if (libssh2_sftp_rmdir_ex(sftp, p.constData(), static_cast<unsigned int>(p.size())) != 0) return false;
    ++removed;
    return true;
}
}

SshResult SshSession::restoreFromTrash(const QString &trashedPath, ConflictPolicy policy)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    const QString files = d->home + QStringLiteral("/.local/share/Trash/files");
    const auto source = cleanAbsolutePath(trashedPath);
    if (!source || parentPath(*source) != files) return transferFail(QStringLiteral("Only items at the top of the Trash can be restored."));
    const QString name = source->section('/', -1);
    const QString infoPath = d->home + QStringLiteral("/.local/share/Trash/info/") + name + QStringLiteral(".trashinfo");
    const SshResult info = readFile(infoPath, 64 * 1024);
    if (info.status != SshResult::Status::Ok) return transferFail(QStringLiteral("No Trash record for this item; its original location is unknown."));
    QString original;
    for (const QByteArray &line : info.data.split('\n'))
        if (line.startsWith("Path=")) original = QUrl::fromPercentEncoding(line.mid(5).trimmed());
    if (original.isEmpty()) return transferFail(QStringLiteral("The Trash record has no original path."));
    if (!original.startsWith('/')) original = d->home + '/' + original; // relative to the Trash's top dir
    const auto cleanOriginal = cleanAbsolutePath(original);
    if (!cleanOriginal || *cleanOriginal == "/") return transferFail(QStringLiteral("The Trash record has an invalid path."));

    auto exists = [this](const QString &path, bool wantDir = false) {
        const QByteArray b = path.toUtf8();
        LIBSSH2_SFTP_ATTRIBUTES a{};
        if (libssh2_sftp_stat_ex(d->sftp, b.constData(), static_cast<unsigned int>(b.size()), LIBSSH2_SFTP_LSTAT, &a) != 0) return false;
        return !wantDir || LIBSSH2_SFTP_S_ISDIR(a.permissions);
    };
    const QString folder = parentPath(*cleanOriginal);
    if (!exists(folder, true))
        return transferFail(QStringLiteral("The original folder %1 no longer exists.").arg(folder));
    QString target = *cleanOriginal;
    if (exists(target)) {
        if (policy != ConflictPolicy::KeepBoth)
            return transferFail(QStringLiteral("« %1 » already exists in %2.").arg(target.section('/', -1), folder), QStringLiteral("conflict"));
        const QString base = target.section('/', -1);
        for (int n = 2; n < 1000 && exists(target); ++n) target = *safeChildPath(folder, numberedName(base, n));
        if (exists(target)) return transferFail(QStringLiteral("No free name to restore to."), QStringLiteral("conflict"));
    }
    const QByteArray from = source->toUtf8(), to = target.toUtf8();
    if (libssh2_sftp_rename_ex(d->sftp, from.constData(), static_cast<unsigned int>(from.size()),
                               to.constData(), static_cast<unsigned int>(to.size()), 0) != 0) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->close();
            return transferFail(QStringLiteral("Connection lost; check the Trash and %1 before retrying.").arg(folder), QStringLiteral("unknown"));
        }
        return transferFail(QStringLiteral("Cannot restore (permission error, or another filesystem)."));
    }
    const QByteArray infoUtf8 = infoPath.toUtf8();
    libssh2_sftp_unlink_ex(d->sftp, infoUtf8.constData(), static_cast<unsigned int>(infoUtf8.size()));
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = target;
    r.message = QStringLiteral("Restored");
    return r;
}

SshResult SshSession::deleteFromTrash(const QString &trashedPath)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    const QString files = d->home + QStringLiteral("/.local/share/Trash/files");
    const auto source = cleanAbsolutePath(trashedPath);
    // Only whole items of the Trash: permanent deletion never reaches elsewhere.
    if (d->home.isEmpty() || !source || parentPath(*source) != files)
        return transferFail(QStringLiteral("Permanent deletion is only available for items in the Trash."));
    qint64 removed = 0;
    if (!removeTree(d->sftp, *source, 0, removed)) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL && libssh2_session_last_errno(d->handle) != 0) {
            d->close();
            return transferFail(QStringLiteral("Connection lost during deletion; part of the item may already be gone."), QStringLiteral("unknown"));
        }
        return transferFail(QStringLiteral("Could not delete everything (%1 entries removed; permission error?).").arg(removed));
    }
    const QByteArray info = (d->home + QStringLiteral("/.local/share/Trash/info/") + source->section('/', -1) + QStringLiteral(".trashinfo")).toUtf8();
    libssh2_sftp_unlink_ex(d->sftp, info.constData(), static_cast<unsigned int>(info.size()));
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.bytes = removed;
    r.message = QStringLiteral("Deleted permanently");
    return r;
}

SshResult SshSession::emptyTrash()
{
    if (!isOpen() || d->home.isEmpty()) return transferFail(QStringLiteral("SSH session is not open."));
    const QString trash = d->home + QStringLiteral("/.local/share/Trash");
    qint64 removed = 0;
    int failed = 0;
    for (const QString &sub : { QStringLiteral("files"), QStringLiteral("info") }) {
        const SshResult listing = list(trash + '/' + sub);
        if (listing.status != SshResult::Status::Ok) {
            if (!isOpen()) return transferFail(QStringLiteral("Connection lost while emptying the Trash."), QStringLiteral("unknown"));
            continue; // no such folder: nothing to empty
        }
        for (const QVariant &v : listing.entries) {
            const QString path = v.toMap().value("path").toString();
            if (parentPath(path) != trash + '/' + sub || !removeTree(d->sftp, path, 0, removed)) ++failed;
        }
    }
    if (!isOpen()) return transferFail(QStringLiteral("Connection lost while emptying the Trash."), QStringLiteral("unknown"));
    if (failed > 0) return transferFail(QStringLiteral("%1 item(s) could not be deleted (permission error?).").arg(failed));
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.bytes = removed;
    r.message = QStringLiteral("Trash emptied");
    return r;
}

namespace {
constexpr qint64 kEditLimit = 4 * 1024 * 1024;
QByteArray sha256(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
}

SshResult SshSession::saveFile(const QString &path, const QByteArray &data, const QByteArray &expectedSha256, bool force)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    const auto requested = cleanAbsolutePath(path);
    if (!requested || data.size() > kEditLimit) return transferFail(QStringLiteral("Invalid file or content too large."));

    // Save through a symlink to its target instead of replacing the link.
    QString target = *requested;
    {
        const QByteArray p = target.toUtf8();
        char resolved[4096];
        const int n = libssh2_sftp_symlink_ex(d->sftp, p.constData(), static_cast<unsigned int>(p.size()),
                                              resolved, sizeof(resolved), LIBSSH2_SFTP_REALPATH);
        if (n > 0) {
            const auto clean = cleanAbsolutePath(QString::fromUtf8(resolved, n));
            if (clean) target = *clean;
        }
    }
    const QByteArray targetUtf8 = target.toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES original{};
    if (libssh2_sftp_stat_ex(d->sftp, targetUtf8.constData(), static_cast<unsigned int>(targetUtf8.size()),
                             LIBSSH2_SFTP_LSTAT, &original) != 0)
        return transferFail(QStringLiteral("The file no longer exists on the server."), QStringLiteral("conflict"));
    if (!(original.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) || !LIBSSH2_SFTP_S_ISREG(original.permissions))
        return transferFail(QStringLiteral("Not a regular file or missing permissions metadata."));

    auto currentHash = [&]() -> std::optional<QByteArray> {
        const SshResult now = readFile(target, kEditLimit + 1);
        if (now.status != SshResult::Status::Ok || now.truncated) return std::nullopt;
        return sha256(now.data);
    };
    if (!force) {
        const auto hash = currentHash();
        if (!hash) return transferFail(QStringLiteral("Cannot re-read the file to check for changes."));
        if (*hash != expectedSha256)
            return transferFail(QStringLiteral("The file was changed on the server since it was opened."), QStringLiteral("conflict"));
    }

    // Exclusive temporary in the same folder (same filesystem => atomic rename).
    const QString dir = parentPath(target);
    const QString name = target.section('/', -1);
    const QString tempPath = *safeChildPath(dir, QStringLiteral(".%1.fortiq-save-%2").arg(name.left(80), randomTag()));
    const QByteArray tempUtf8 = tempPath.toUtf8();
    LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open_ex(d->sftp, tempUtf8.constData(),
        static_cast<unsigned int>(tempUtf8.size()), LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL,
        0600, LIBSSH2_SFTP_OPENFILE);
    if (!file) return transferFail(QStringLiteral("Cannot write in this folder (a safe save needs write access to the folder)."));
    auto removeTemp = [this, &tempUtf8] {
        if (!d->sftp) return;
        libssh2_sftp_unlink_ex(d->sftp, tempUtf8.constData(), static_cast<unsigned int>(tempUtf8.size()));
    };

    LIBSSH2_SFTP_ATTRIBUTES mine{};
    if (libssh2_sftp_fstat_ex(file, &mine, 0) != 0 || !(mine.flags & LIBSSH2_SFTP_ATTR_UIDGID)
        || !(original.flags & LIBSSH2_SFTP_ATTR_UIDGID) || mine.uid != original.uid) {
        libssh2_sftp_close(file);
        removeTemp();
        return transferFail(QStringLiteral("This file belongs to another user; saving it safely would change its owner. Not saved."));
    }
    qint64 off = 0;
    while (off < data.size()) {
        const auto w = libssh2_sftp_write(file, data.constData() + off, static_cast<size_t>(data.size() - off));
        if (w <= 0) {
            const bool lost = libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL;
            libssh2_sftp_close(file);
            if (lost) {
                d->close();
                return transferFail(QStringLiteral("Connection lost while saving; the original file was not changed."));
            }
            removeTemp();
            return transferFail(QStringLiteral("Write failed (quota or disk full?); the original file was not changed."));
        }
        off += w;
    }
    // Restore and verify metadata after writing; do not silently widen rights
    // or change group when the server refuses fsetstat.
    bool metadataOk = true;
    if (mine.gid != original.gid) {
        LIBSSH2_SFTP_ATTRIBUTES group{};
        group.flags = LIBSSH2_SFTP_ATTR_UIDGID;
        group.uid = mine.uid; group.gid = original.gid;
        metadataOk = setFileAttributes(file, &group) == 0;
    }
    LIBSSH2_SFTP_ATTRIBUTES keep{};
    keep.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    keep.permissions = original.permissions & 07777;
    metadataOk = metadataOk && setFileAttributes(file, &keep) == 0;
    const bool flushed = flushFile(d->sftp, file);
    LIBSSH2_SFTP_ATTRIBUTES written{};
    const unsigned long required = LIBSSH2_SFTP_ATTR_SIZE | LIBSSH2_SFTP_ATTR_PERMISSIONS | LIBSSH2_SFTP_ATTR_UIDGID;
    const bool verified = libssh2_sftp_fstat_ex(file, &written, 0) == 0 && (written.flags & required) == required
        && static_cast<qint64>(written.filesize) == data.size()
        && (written.permissions & 07777) == (original.permissions & 07777)
        && written.uid == original.uid && written.gid == original.gid;
    const bool closed = closeStagingFile(file) == 0;
    if (!metadataOk || !flushed || !verified || !closed) {
        removeTemp();
        return transferFail(QStringLiteral("Staging size, permissions, owner/group, flush or close verification failed; the original file was not changed."));
    }
#ifdef FORTIQ_PUBLICATION_TESTS
    if (publication_test::staged) publication_test::staged();
#endif

    // Re-check right before publishing: someone may have saved meanwhile.
    if (!force) {
        const auto hash = currentHash();
        if (!hash || *hash != expectedSha256) {
            removeTemp();
            return transferFail(QStringLiteral("The file was changed on the server during the save."), QStringLiteral("conflict"));
        }
    }
    LIBSSH2_SFTP_ATTRIBUTES latest{};
    const unsigned long identityFlags = LIBSSH2_SFTP_ATTR_PERMISSIONS | LIBSSH2_SFTP_ATTR_UIDGID;
    if (!d->sftp || libssh2_sftp_stat_ex(d->sftp, targetUtf8.constData(), static_cast<unsigned int>(targetUtf8.size()),
                                      LIBSSH2_SFTP_LSTAT, &latest) != 0
        || (latest.flags & identityFlags) != identityFlags || !LIBSSH2_SFTP_S_ISREG(latest.permissions)
        || (latest.permissions & 07777) != (original.permissions & 07777)
        || latest.uid != original.uid || latest.gid != original.gid) {
        removeTemp();
        return transferFail(QStringLiteral("The destination type, permissions or owner/group changed during the save."), QStringLiteral("conflict"));
    }
    const int published = replaceFile(d->handle, d->sftp, tempUtf8, targetUtf8);
    if (published != 0) {
        if (published != LIBSSH2_ERROR_METHOD_NOT_SUPPORTED
            && libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->close();
            return transferFail(QStringLiteral("Connection lost while publishing; reopen the file to see which version is on the server."),
                                QStringLiteral("unknown"));
        }
        removeTemp();
        // No non-atomic fallback that could leave the file half-replaced.
        return transferFail(QStringLiteral("Atomic replacement was refused (unsupported extension or permissions); the original file was not changed."));
    }
    const SshResult back = readFile(target, kEditLimit + 1);
    if (back.status != SshResult::Status::Ok || back.truncated || back.fileSize != data.size() || sha256(back.data) != sha256(data))
        return transferFail(QStringLiteral("Saved, but the read-back check differs; reopen the file."), QStringLiteral("unknown"));
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = target;
    r.data = sha256(data);
    r.bytes = data.size();
    r.message = QStringLiteral("Saved");
    return r;
}

SshResult SshSession::saveCopy(const QString &dirPath, const QString &name, const QByteArray &data)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    const auto dir = cleanAbsolutePath(dirPath);
    if (!dir || !safeChildPath(*dir, name) || data.size() > kEditLimit) return transferFail(QStringLiteral("Invalid destination."));
    auto exists = [this](const QString &p) {
        const QByteArray b = p.toUtf8();
        LIBSSH2_SFTP_ATTRIBUTES a{};
        return libssh2_sftp_stat_ex(d->sftp, b.constData(), static_cast<unsigned int>(b.size()), LIBSSH2_SFTP_LSTAT, &a) == 0;
    };
    QString target = *safeChildPath(*dir, name);
    for (int n = 2; n < 1000 && exists(target); ++n) target = *safeChildPath(*dir, numberedName(name, n));
    const QByteArray t = target.toUtf8();
    // Written to an exclusive temporary, then published by a rename that
    // never overwrites: a broken connection never leaves a half copy.
    const QByteArray temp = safeChildPath(*dir, QStringLiteral(".%1.fortiq-save-%2").arg(name.left(80), randomTag()))->toUtf8();
    LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open_ex(d->sftp, temp.constData(), static_cast<unsigned int>(temp.size()),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL, 0644, LIBSSH2_SFTP_OPENFILE);
    if (!file) return transferFail(QStringLiteral("Cannot create the copy (permission error)."));
    auto dropTemp = [this, &temp] { libssh2_sftp_unlink_ex(d->sftp, temp.constData(), static_cast<unsigned int>(temp.size())); };
    qint64 off = 0;
    while (off < data.size()) {
        const auto w = libssh2_sftp_write(file, data.constData() + off, static_cast<size_t>(data.size() - off));
        if (w < 0) {
            libssh2_sftp_close(file);
            dropTemp();
            return transferFail(QStringLiteral("Write failed; no copy was kept."));
        }
        off += w;
    }
    libssh2_sftp_close(file);
    if (libssh2_sftp_rename_ex(d->sftp, temp.constData(), static_cast<unsigned int>(temp.size()),
                               t.constData(), static_cast<unsigned int>(t.size()), 0) != 0) {
        dropTemp();
        return transferFail(QStringLiteral("The copy name was taken meanwhile; nothing was written."), QStringLiteral("conflict"));
    }
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = target;
    r.data = sha256(data);
    r.bytes = data.size();
    r.message = QStringLiteral("Copy saved");
    return r;
}

namespace {
// POSIX sh single-quoting: the value can never end the quote.
QByteArray shQuote(const QString &value) {
    QByteArray v = value.toUtf8();
    v.replace("'", "'\\''");
    return "'" + v + "'";
}
// find -iname is a glob: make the user's text literal.
QString globLiteral(const QString &text) {
    QString out;
    for (const QChar ch : text) {
        if (ch == '\\' || ch == '*' || ch == '?' || ch == '[' || ch == ']') out += '\\';
        out += ch;
    }
    return out;
}
}

SshResult SshSession::search(const QString &dirPath, const QString &query, const QString &mode, int limit,
                             const std::function<bool(int)> &keepGoing)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    const auto dir = cleanAbsolutePath(dirPath);
    const QString q = query.trimmed();
    if (!dir || q.isEmpty() || q.contains(QChar::Null) || q.contains('\n') || q.contains('\r') || q.size() > 256)
        return transferFail(QStringLiteral("Invalid search."));
    if (mode != "name" && mode != "content") return transferFail(QStringLiteral("Invalid search mode."));

    QByteArray script = "d=" + shQuote(*dir) + "\n";
    if (mode == "name") {
        script += "p=" + shQuote(QStringLiteral("*") + globLiteral(q) + QStringLiteral("*")) + "\n"
            "command -v find >/dev/null 2>&1 || { printf '#missing:find\\n'; exit 3; }\n"
            "if find / -maxdepth 0 -printf '' >/dev/null 2>&1; then\n"
            "  printf '#tool:find\\n'\n"
            "  exec find \"$d\" -xdev -path '*/.local/share/Trash' -prune -o -iname \"$p\" -printf '%y\\t%s\\t%T@\\t%p\\0' 2>/dev/null\n"
            "fi\n"
            "printf '#tool:find-basic\\n'\n"
            "exec find \"$d\" -xdev -path '*/.local/share/Trash' -prune -o -iname \"$p\" -print0 2>/dev/null\n";
    } else {
        script += "q=" + shQuote(q) + "\n"
            "if command -v rg >/dev/null 2>&1; then\n"
            "  printf '#tool:rg\\n'\n"
            "  exec rg -i -F -n --no-heading --null --hidden --no-ignore -g '!.git' --max-count 3 --max-columns 300"
            " --max-columns-preview --max-filesize 20M -e \"$q\" -- \"$d\" 2>/dev/null\n"
            "fi\n"
            "command -v grep >/dev/null 2>&1 || { printf '#missing:grep\\n'; exit 3; }\n"
            "printf '#tool:grep\\n'\n"
            "exec grep -r -I -n -i -F -Z -m 3 --exclude-dir=.git -e \"$q\" -- \"$d\" 2>/dev/null\n";
    }

    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(d->handle);
    if (!channel) {
        d->close();
        return transferFail(QStringLiteral("SSH connection lost."));
    }
    libssh2_channel_handle_extended_data2(channel, LIBSSH2_CHANNEL_EXTENDED_DATA_IGNORE);
    // The login shell only sees "sh -s"; the script and its data arrive on stdin.
    if (libssh2_channel_exec(channel, "sh -s") != 0) {
        libssh2_channel_free(channel);
        return transferFail(QStringLiteral("The server refused command execution."));
    }
    qsizetype sent = 0;
    while (sent < script.size()) {
        const auto w = libssh2_channel_write(channel, script.constData() + sent, static_cast<size_t>(script.size() - sent));
        if (w < 0) { libssh2_channel_free(channel); return transferFail(QStringLiteral("Cannot start the search.")); }
        sent += w;
    }
    libssh2_channel_send_eof(channel);

    // Short read timeout so cancellation is noticed while the tool is silent.
    libssh2_session_set_timeout(d->handle, 300);
    QByteArray pending;
    QString tool;
    QVariantList entries;
    bool stopped = false;
    bool lost = false;
    char buffer[65536];
    auto take = [&](const QByteArray &record) {
        if (record.isEmpty()) return;
        QVariantMap e;
        QString path;
        if (tool == "find") {
            const QList<QByteArray> f = record.split('\t');
            if (f.size() < 4) return;
            path = QString::fromUtf8(record.mid(f[0].size() + f[1].size() + f[2].size() + 3));
            if (path.toUtf8() != record.mid(f[0].size() + f[1].size() + f[2].size() + 3)) return;
            e.insert("directory", f[0] == "d");
            e.insert("link", f[0] == "l");
            e.insert("size", f[1].toDouble());
            e.insert("mtime", f[2].toDouble());
        } else if (tool == "find-basic") {
            path = QString::fromUtf8(record);
            if (path.toUtf8() != record) return;
        } else { // rg / grep: "path\0line:text"
            const int nul = record.indexOf('\0');
            if (nul <= 0) return;
            path = QString::fromUtf8(record.left(nul));
            if (path.toUtf8() != record.left(nul)) return;
            const QByteArray rest = record.mid(nul + 1);
            const int colon = rest.indexOf(':');
            e.insert("line", rest.left(colon).toInt());
            e.insert("text", QString::fromUtf8(rest.mid(colon + 1)).left(300));
        }
        if (!cleanAbsolutePath(path) || path == *dir) return;
        e.insert("path", path);
        e.insert("name", path.section('/', -1));
        entries.push_back(e);
    };
    while (!stopped) {
        const auto n = libssh2_channel_read(channel, buffer, sizeof(buffer));
        if (n == LIBSSH2_ERROR_TIMEOUT || n == LIBSSH2_ERROR_EAGAIN) {
            if (keepGoing && !keepGoing(static_cast<int>(entries.size()))) stopped = true;
            continue;
        }
        if (n < 0) { lost = true; break; }
        if (n == 0) { if (libssh2_channel_eof(channel)) break; continue; }
        pending.append(buffer, n);
        if (tool.isEmpty()) {
            const int nl = pending.indexOf('\n');
            if (nl < 0) continue;
            const QByteArray head = pending.left(nl);
            pending.remove(0, nl + 1);
            if (head.startsWith("#missing:")) {
                libssh2_channel_free(channel);
                libssh2_session_set_timeout(d->handle, 15000);
                return transferFail(QStringLiteral("« %1 » is not installed on the server.").arg(QString::fromUtf8(head.mid(9))));
            }
            tool = QString::fromUtf8(head.mid(6));
        }
        // find separates records with NUL; rg/grep with a newline after "path\0".
        const char sep = (tool == "find" || tool == "find-basic") ? '\0' : '\n';
        int at;
        while ((at = pending.indexOf(sep)) >= 0 && !stopped) {
            take(pending.left(at));
            pending.remove(0, at + 1);
            if (entries.size() >= limit) stopped = true;
        }
        if (keepGoing && !keepGoing(static_cast<int>(entries.size()))) stopped = true;
    }
    const bool limitReached = entries.size() >= limit;
    // Closing the channel ends the remote tool (SIGPIPE on its next write).
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
    libssh2_session_set_timeout(d->handle, 15000);
    if (lost) {
        d->close();
        return transferFail(QStringLiteral("SSH connection lost during the search."));
    }
    if (stopped && !limitReached) {
        SshResult r = transferFail(QStringLiteral("Search cancelled."), QStringLiteral("cancelled"));
        r.entries = entries;
        return r;
    }
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = *dir;
    r.entries = entries;
    r.truncated = limitReached;
    r.code = tool;
    r.message = QStringLiteral("%1 results").arg(entries.size());
    return r;
}

QByteArray SshSession::shellQuote(const QString &value) { return shQuote(value); }

SshResult SshSession::runScript(const QByteArray &script, qint64 maxStdout, const std::function<bool()> &keepGoing)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(d->handle);
    if (!channel) {
        d->close();
        return transferFail(QStringLiteral("SSH connection lost."));
    }
    if (libssh2_channel_exec(channel, "sh -s") != 0) {
        libssh2_channel_free(channel);
        return transferFail(QStringLiteral("The server refused command execution."));
    }
    qsizetype sent = 0;
    while (sent < script.size()) {
        const auto w = libssh2_channel_write(channel, script.constData() + sent, static_cast<size_t>(script.size() - sent));
        if (w < 0) { libssh2_channel_free(channel); return transferFail(QStringLiteral("Cannot start the command.")); }
        sent += w;
    }
    libssh2_channel_send_eof(channel);

    libssh2_session_set_timeout(d->handle, 200);
    QByteArray out, err;
    bool truncated = false, stopped = false, lost = false;
    char buffer[65536];
    while (true) {
        bool any = false;
        auto n = libssh2_channel_read(channel, buffer, sizeof(buffer));
        if (n > 0) {
            any = true;
            out.append(buffer, std::min<qint64>(n, maxStdout - out.size()));
            if (out.size() >= maxStdout) { truncated = true; break; }
        } else if (n < 0 && n != LIBSSH2_ERROR_TIMEOUT && n != LIBSSH2_ERROR_EAGAIN) { lost = true; break; }
        n = libssh2_channel_read_stderr(channel, buffer, sizeof(buffer));
        if (n > 0) { any = true; if (err.size() < 8192) err.append(buffer, std::min<qint64>(n, 8192 - err.size())); }
        else if (n < 0 && n != LIBSSH2_ERROR_TIMEOUT && n != LIBSSH2_ERROR_EAGAIN) { lost = true; break; }
        if (!any && libssh2_channel_eof(channel)) break;
        if (keepGoing && !keepGoing()) { stopped = true; break; }
    }
    int exitCode = -1;
    if (!truncated && !stopped && !lost) {
        libssh2_session_set_timeout(d->handle, 5000);
        libssh2_channel_wait_closed(channel);
        exitCode = libssh2_channel_get_exit_status(channel);
    }
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
    libssh2_session_set_timeout(d->handle, 15000);
    if (lost) {
        d->close();
        return transferFail(QStringLiteral("SSH connection lost."));
    }
    SshResult r;
    r.status = stopped ? SshResult::Status::Error : SshResult::Status::Ok;
    r.data = std::move(out);
    r.truncated = truncated;
    r.message = QString::fromUtf8(err);
    r.code = stopped ? QStringLiteral("cancelled") : QStringLiteral("exit:%1").arg(exitCode);
    return r;
}

SshResult SshClient::list(const ConnectionRequest &request)
{
    SshSession session;
    return session.open(request);
}
}
