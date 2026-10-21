#include "SshClient.h"
#include "../core/RemotePath.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpSocket>
#include <QVariantMap>
#include <algorithm>
#include <mutex>
#include <vector>

#include <libssh2.h>
#include <libssh2_sftp.h>

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
        // The key is read by libssh2 from disk; FORTIQ never copies it.
        const QByteArray keyFile = QFile::encodeName(req.keyPath);
        rc = libssh2_userauth_publickey_fromfile_ex(d->handle, user.constData(),
            static_cast<unsigned int>(user.size()), nullptr, keyFile.constData(),
            password.isEmpty() ? nullptr : password.constData());
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
    if (libssh2_sftp_stat_ex(d->sftp, pathUtf8.constData(), static_cast<unsigned int>(pathUtf8.size()),
            LIBSSH2_SFTP_STAT, &attr) != 0)
        return transferFail(QStringLiteral("Cannot read remote file (permission or path error)."));
    if ((attr.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && !LIBSSH2_SFTP_S_ISREG(attr.permissions))
        return transferFail(QStringLiteral("Only regular files can be downloaded."));
    const qint64 total = (attr.flags & LIBSSH2_SFTP_ATTR_SIZE) ? static_cast<qint64>(attr.filesize) : -1;

    LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open_ex(d->sftp, pathUtf8.constData(),
        static_cast<unsigned int>(pathUtf8.size()), LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE);
    if (!file) return transferFail(QStringLiteral("Cannot open remote file (permission error)."));

    QSaveFile out(localPath);
    if (!out.open(QIODevice::WriteOnly)) {
        libssh2_sftp_close(file);
        return transferFail(QStringLiteral("Cannot write the local file: %1").arg(out.errorString()));
    }
    std::vector<char> buffer(256 * 1024);
    qint64 done = 0;
    while (true) {
        const auto n = libssh2_sftp_read(file, buffer.data(), buffer.size());
        if (n == 0) break;
        if (n < 0) {
            libssh2_sftp_close(file);
            out.cancelWriting();
            if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) d->close();
            return transferFail(QStringLiteral("Download interrupted; the local file was not created."));
        }
        if (out.write(buffer.data(), n) != n) {
            libssh2_sftp_close(file);
            out.cancelWriting();
            return transferFail(QStringLiteral("Local write failed (disk full?)."));
        }
        done += n;
        if (progress && !progress(done, total)) {
            libssh2_sftp_close(file);
            out.cancelWriting();
            return transferFail(QStringLiteral("Cancelled; the local file was not created."), QStringLiteral("cancelled"));
        }
    }
    libssh2_sftp_close(file);
    if (total >= 0 && done != total) {
        out.cancelWriting();
        return transferFail(QStringLiteral("Size mismatch (%1 of %2 bytes); the local file was not created.").arg(done).arg(total));
    }
    if (!out.commit()) return transferFail(QStringLiteral("Cannot finalise the local file: %1").arg(out.errorString()));
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
    if (!QFileInfo(localPath).isFile()) return transferFail(QStringLiteral("Only regular files can be uploaded."));
    const qint64 total = in.size();

    auto exists = [this](const QString &path) {
        const QByteArray p = path.toUtf8();
        LIBSSH2_SFTP_ATTRIBUTES a{};
        return libssh2_sftp_stat_ex(d->sftp, p.constData(), static_cast<unsigned int>(p.size()), LIBSSH2_SFTP_LSTAT, &a) == 0;
    };
    QString finalPath = *target;
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
        libssh2_sftp_unlink_ex(d->sftp, tempUtf8.constData(), static_cast<unsigned int>(tempUtf8.size()));
    };

    std::vector<char> buffer(256 * 1024);
    qint64 done = 0;
    while (true) {
        const qint64 n = in.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (n < 0) { libssh2_sftp_close(file); removeTemp(); return transferFail(QStringLiteral("Local read failed.")); }
        if (n == 0) break;
        qint64 off = 0;
        while (off < n) {
            const auto w = libssh2_sftp_write(file, buffer.data() + off, static_cast<size_t>(n - off));
            if (w < 0) {
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
    libssh2_sftp_fsync(file); // best effort: not every server supports fsync@openssh.com
    LIBSSH2_SFTP_ATTRIBUTES written{};
    const bool statOk = libssh2_sftp_fstat_ex(file, &written, 0) == 0;
    libssh2_sftp_close(file);
    if (!statOk || !(written.flags & LIBSSH2_SFTP_ATTR_SIZE) || static_cast<qint64>(written.filesize) != total) {
        removeTemp();
        return transferFail(QStringLiteral("Size verification failed; nothing was published."));
    }

    // Publish. SFTP v3 rename never replaces an existing name, so a file that
    // appeared meanwhile is reported as a conflict instead of being lost.
    const QByteArray finalUtf8 = finalPath.toUtf8();
    auto rename = [this](const QByteArray &from, const QByteArray &to) {
        return libssh2_sftp_rename_ex(d->sftp, from.constData(), static_cast<unsigned int>(from.size()),
                                      to.constData(), static_cast<unsigned int>(to.size()), 0);
    };
    QByteArray backupUtf8;
    if (policy == ConflictPolicy::Replace && exists(finalPath)) {
        backupUtf8 = safeChildPath(*dir, QStringLiteral(".%1.fortiq-replaced-%2").arg(name.left(80), randomTag()))->toUtf8();
        if (rename(finalUtf8, backupUtf8) != 0) {
            removeTemp();
            return transferFail(QStringLiteral("Cannot set the existing file aside; nothing was replaced."));
        }
    }
    if (rename(tempUtf8, finalUtf8) != 0) {
        if (!isOpen() || libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->close();
            return transferFail(QStringLiteral("Connection lost while publishing; check the folder before retrying."), QStringLiteral("unknown"));
        }
        if (!backupUtf8.isEmpty()) rename(backupUtf8, finalUtf8); // restore the original
        removeTemp();
        return transferFail(QStringLiteral("« %1 » appeared on the server meanwhile; nothing was replaced.").arg(finalPath.section('/', -1)),
                            QStringLiteral("conflict"));
    }
    if (!backupUtf8.isEmpty())
        libssh2_sftp_unlink_ex(d->sftp, backupUtf8.constData(), static_cast<unsigned int>(backupUtf8.size()));

    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = finalPath;
    r.bytes = done;
    r.message = QStringLiteral("Uploaded");
    return r;
}

SshResult SshClient::list(const ConnectionRequest &request)
{
    SshSession session;
    return session.open(request);
}
}
