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
}
