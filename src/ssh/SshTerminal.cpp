#include "SshInternal.h"

namespace fortiq {
SshResult SshSession::startTerminal(int columns, int rows) {
    if (!isOpen() || d->terminal || columns < 2 || columns > 240 || rows < 2 || rows > 100)
        return fail(QStringLiteral("Terminal unavailable or invalid dimensions."));
    auto *channel = libssh2_channel_open_session(d->handle);
    if (!channel) return fail(QStringLiteral("Cannot open SSH terminal channel."));
    if (libssh2_channel_request_pty_ex(channel, "xterm-256color", 14, nullptr, 0, columns, rows, 0, 0)
        || libssh2_channel_shell(channel)) {
        libssh2_channel_free(channel);
        return fail(QStringLiteral("The server refused the interactive PTY shell."));
    }
    d->terminal = channel; d->terminalColumns = columns; d->terminalRows = rows;
    libssh2_session_set_timeout(d->handle, 1500);
    SshResult r; r.status = SshResult::Status::Ok; return r;
}

SshResult SshSession::terminalExchange(const QByteArray &input, int columns, int rows) {
    if (!d->terminal) return fail(QStringLiteral("Terminal is closed."));
    if (columns < 2 || columns > 240 || rows < 2 || rows > 100 || d->terminalQueued + input.size() > 65536)
        return fail(QStringLiteral("Terminal input limit exceeded."));
    if (!input.isEmpty()) { d->terminalWrites.append(input); d->terminalQueued += input.size(); }
    SshResult result; result.status = SshResult::Status::Ok;
    bool failed = false;
    if (columns != d->terminalColumns || rows != d->terminalRows) {
        const int rc = libssh2_channel_request_pty_size(d->terminal, columns, rows);
        if (rc == 0) { d->terminalColumns = columns; d->terminalRows = rows; }
        else failed = true;
    }
    libssh2_session_set_blocking(d->handle, 0);
    // Keep the exact buffer/length stable over EAGAIN; new input is queued separately.
    for (int n = 0; !failed && n < 8 && !d->terminalWrites.isEmpty(); ++n) {
        const auto &bytes = d->terminalWrites.first();
        const auto rc = libssh2_channel_write(d->terminal, bytes.constData() + d->terminalOffset, bytes.size() - d->terminalOffset);
        if (rc == LIBSSH2_ERROR_EAGAIN || rc == 0) break;
        if (rc < 0) { failed = true; break; }
        d->terminalOffset += rc; d->terminalQueued -= rc;
        if (d->terminalOffset == bytes.size()) { d->terminalWrites.removeFirst(); d->terminalOffset = 0; }
    }
    char buffer[8192];
    for (int n = 0; !failed && n < 8; ++n) {
        const auto rc = libssh2_channel_read(d->terminal, buffer, sizeof(buffer));
        if (rc == LIBSSH2_ERROR_EAGAIN || rc == 0) break;
        if (rc < 0) { failed = true; break; }
        result.data.append(buffer, rc);
    }
    const bool ended = libssh2_channel_eof(d->terminal);
    const int exitCode = ended ? libssh2_channel_get_exit_status(d->terminal) : 0;
    libssh2_session_set_blocking(d->handle, 1);
    if (failed) { d->closeLost(); return fail(QStringLiteral("Connexion du terminal SSH perdue. La saisie n'a pas été rejouée.")); }
    if (ended) { result.code = QStringLiteral("exit:%1").arg(exitCode); d->close(); }
    return result;
}
}  // namespace fortiq
