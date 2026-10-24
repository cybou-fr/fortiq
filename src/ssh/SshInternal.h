#pragma once
// Internal to the SSH session implementation (SshSession*.cpp, SshTerminal.cpp, ...).
// Not part of the public API: SshClient.h is.
#include "SshClient.h"
#include "LinkMonitor.h"
#include "../core/RemotePath.h"
#include "../core/LocalDocument.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QUrl>
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
namespace ssh_detail {
inline SshResult fail(const QString &reason) {
    SshResult result; result.message = reason; return result;
}
inline int replaceFile(LIBSSH2_SESSION *session, LIBSSH2_SFTP *sftp, const QByteArray &from, const QByteArray &to) {
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
inline bool flushFile(LIBSSH2_SFTP *sftp, LIBSSH2_SFTP_HANDLE *file) {
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
inline int setFileAttributes(LIBSSH2_SFTP_HANDLE *file, LIBSSH2_SFTP_ATTRIBUTES *attr) {
#ifdef FORTIQ_PUBLICATION_TESTS
    if (publication_test::attributes) return publication_test::attributes(file, attr);
#endif
    return libssh2_sftp_fstat_ex(file, attr, 1);
}
inline int closeStagingFile(LIBSSH2_SFTP_HANDLE *file) {
#ifdef FORTIQ_PUBLICATION_TESTS
    if (publication_test::close) return publication_test::close(file);
#endif
    return libssh2_sftp_close(file);
}

inline SshResult transferFail(const QString &reason, const QString &code = {}) {
    SshResult r; r.message = reason; r.code = code; return r;
}
inline QString randomTag() {
    return QString::number(QRandomGenerator::global()->generate64(), 36);
}
// "report.pdf" -> "report (2).pdf"; dotfiles keep their leading dot.
inline QString numberedName(const QString &name, int n) {
    const int dot = name.lastIndexOf('.');
    if (dot <= 0) return QStringLiteral("%1 (%2)").arg(name).arg(n);
    return QStringLiteral("%1 (%2)%3").arg(name.left(dot)).arg(n).arg(name.mid(dot));
}
}  // namespace ssh_detail
using namespace ssh_detail;

struct SshSession::Impl {
    QTcpSocket socket;
    LIBSSH2_SESSION *handle = nullptr;
    LIBSSH2_SFTP *sftp = nullptr;
    QString fingerprint;
    QString home;
    LIBSSH2_CHANNEL *terminal = nullptr;
    QString key;          // LinkMonitor key of the server this session talks to
    QList<QByteArray> terminalWrites;
    qsizetype terminalOffset = 0;
    qsizetype terminalQueued = 0;
    int terminalColumns = 0, terminalRows = 0;

    void close() {
        if (terminal) { libssh2_channel_free(terminal); terminal = nullptr; }
        terminalWrites.clear(); terminalOffset = terminalQueued = 0;
        if (sftp) { libssh2_sftp_shutdown(sftp); sftp = nullptr; }
        if (handle) {
            libssh2_session_disconnect_ex(handle, SSH_DISCONNECT_BY_APPLICATION, "FORTIQ disconnect", "");
            libssh2_session_free(handle);
            handle = nullptr;
        }
        socket.abort();
        home.clear();
    }
    // The transport is dead: close, remember it, and tell the rest of the client (LinkMonitor).
    void closeLost() {
        close();
        LinkMonitor::reportLost(key, QStringLiteral("Connexion SSH perdue."));
    }
};

}  // namespace fortiq
