#pragma once
// Compiled only into fortiq_publication_tests, never the application.
#include <QByteArray>
#include <functional>
#include <libssh2.h>
#include <libssh2_sftp.h>

namespace fortiq::publication_test {
inline std::function<int(LIBSSH2_SESSION *, LIBSSH2_SFTP *, const QByteArray &, const QByteArray &)> replace;
inline std::function<void()> staged;
inline std::function<int(LIBSSH2_SFTP_HANDLE *, LIBSSH2_SFTP_ATTRIBUTES *)> attributes;
inline std::function<int(LIBSSH2_SFTP_HANDLE *)> flush;
inline std::function<int(LIBSSH2_SFTP_HANDLE *)> close;
inline std::function<int(LIBSSH2_SESSION *, LIBSSH2_SFTP *, const QByteArray &, LIBSSH2_SFTP_ATTRIBUTES *)> downloadStat;
// Fault injection for one named SFTP mutation ("mkdir", "rename", "trash", "restore", "copy", "unlink", "rmdir"):
// 0 = run normally, 1 = server refuses (protocol error, nothing done), 2 = request lost (nothing done, link error),
// 3 = acknowledgement lost (the server DID the work, the client sees a link error).
inline std::function<int(const char *op)> fault;
}
