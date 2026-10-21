#include "SshClient.h"
#include "../core/RemotePath.h"

#include <QCryptographicHash>
#include <QTcpSocket>
#include <QByteArray>
#include <QVariantMap>
#include <algorithm>
#include <mutex>

#include <libssh2.h>
#include <libssh2_sftp.h>

namespace fortiq {
namespace {
// libssh2's global init is not per-request. No concurrent libssh2_exit calls.
std::once_flag sshInitFlag;
int sshInitResult = -1;
struct Session {
    LIBSSH2_SESSION *handle = nullptr;
    LIBSSH2_SFTP *sftp = nullptr;
    ~Session() {
        if (sftp) libssh2_sftp_shutdown(sftp);
        if (handle) {
            libssh2_session_disconnect_ex(handle, SSH_DISCONNECT_BY_APPLICATION, "FORTIQ disconnect", "");
            libssh2_session_free(handle);
        }
    }
};
QString sshError(LIBSSH2_SESSION *session, const QString &prefix) {
    char *msg = nullptr;
    int len = 0;
    libssh2_session_last_error(session, &msg, &len, 0);
    // Never echo unknown server text or credential data into the UI.
    return prefix + QStringLiteral(" (SSH code %1)").arg(libssh2_session_last_errno(session));
}
SshResult fail(const QString &reason) {
    SshResult result; result.message = reason; return result;
}
}

SshResult SshClient::list(const ConnectionRequest &req)
{
    if (req.host.trimmed().isEmpty() || req.user.trimmed().isEmpty() ||
        req.port < 1 || req.port > 65535 || req.password.isEmpty()) {
        return fail(QStringLiteral("Provide host, port, user and password."));
    }
    if (!req.directory.isEmpty() && !req.directory.startsWith('/'))
        return fail(QStringLiteral("Invalid remote directory."));

    std::call_once(sshInitFlag, [] { sshInitResult = libssh2_init(0); });
    if (sshInitResult != 0) return fail(QStringLiteral("SSH library initialization failed."));

    QTcpSocket socket;
    socket.connectToHost(req.host, static_cast<quint16>(req.port));
    if (!socket.waitForConnected(10000))
        return fail(QStringLiteral("TCP connection failed or timed out."));

    Session session;
    session.handle = libssh2_session_init();
    if (!session.handle) return fail(QStringLiteral("SSH session allocation failed."));
    libssh2_session_set_blocking(session.handle, 1);
    libssh2_session_set_timeout(session.handle, 15000);
    if (libssh2_session_handshake(session.handle,
            static_cast<libssh2_socket_t>(socket.socketDescriptor())) != 0)
        return fail(sshError(session.handle, QStringLiteral("SSH handshake failed")));

    size_t hostKeyLength = 0;
    int keyType = 0;
    const char *hostKey = libssh2_session_hostkey(session.handle, &hostKeyLength, &keyType);
    if (!hostKey || hostKeyLength == 0)
        return fail(QStringLiteral("Server did not provide a usable host key."));
    const QByteArray digest = QCryptographicHash::hash(
        QByteArray(hostKey, static_cast<qsizetype>(hostKeyLength)), QCryptographicHash::Sha256);
    const QString fingerprint = QStringLiteral("SHA256:") + QString::fromLatin1(
        digest.toBase64(QByteArray::OmitTrailingEquals));
    if (req.expectedHostFingerprint.isEmpty()) {
        SshResult result;
        result.status = SshResult::Status::TrustRequired;
        result.fingerprint = fingerprint;
        result.message = QStringLiteral("Check the SSH host fingerprint with a trusted source before accepting it.");
        return result;
    }
    if (fingerprint != req.expectedHostFingerprint) {
        return fail(QStringLiteral("SSH HOST KEY CHANGED. Connection blocked. Verify server identity out of band."));
    }

    const QByteArray user = req.user.toUtf8();
    const QByteArray password = req.password.toUtf8();
    if (libssh2_userauth_password_ex(session.handle, user.constData(),
           static_cast<unsigned int>(user.size()), password.constData(),
           static_cast<unsigned int>(password.size()), nullptr) != 0)
        return fail(QStringLiteral("SSH authentication failed (password)."));

    session.sftp = libssh2_sftp_init(session.handle);
    if (!session.sftp) return fail(sshError(session.handle, QStringLiteral("SFTP unavailable")));

    QString remoteDir = req.directory;
    if (remoteDir.isEmpty()) {
        char buffer[4096];
        const int n = libssh2_sftp_realpath(session.sftp, ".", buffer, sizeof(buffer));
        if (n <= 0) return fail(QStringLiteral("Cannot resolve remote home directory."));
        remoteDir = QString::fromUtf8(buffer, n);
    }
    if (!remoteDir.startsWith('/'))
        return fail(QStringLiteral("Remote path is not absolute."));
    const QByteArray dirUtf8 = remoteDir.toUtf8();
    LIBSSH2_SFTP_HANDLE *dir = libssh2_sftp_opendir(session.sftp, dirUtf8.constData());
    if (!dir) return fail(QStringLiteral("Cannot open remote folder (permission or path error)."));

    QVariantList entries;
    char name[8192];
    while (true) {
        LIBSSH2_SFTP_ATTRIBUTES attr{};
        const int n = libssh2_sftp_readdir_ex(dir, name, sizeof(name), nullptr, 0, &attr);
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
        const bool isDirectory = (attr.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
            LIBSSH2_SFTP_S_ISDIR(attr.permissions);
        const bool isLink = (attr.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
            LIBSSH2_SFTP_S_ISLNK(attr.permissions);
        QVariantMap entry;
        entry.insert("name", fileName);
        entry.insert("directory", isDirectory);
        entry.insert("link", isLink);
        entry.insert("path", *child);
        entry.insert("size", static_cast<qulonglong>((attr.flags & LIBSSH2_SFTP_ATTR_SIZE) ? attr.filesize : 0));
        entries.push_back(entry);
        if (entries.size() > 3000) {
            libssh2_sftp_closedir(dir);
            return fail(QStringLiteral("Directory is too large for the first milestone (3000 entries)."));
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
    result.fingerprint = fingerprint;
    result.directory = remoteDir;
    result.entries = entries;
    result.message = QStringLiteral("Connected to Linux over verified SSH/SFTP.");
    return result;
}
}
