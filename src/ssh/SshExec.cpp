#include "SshInternal.h"
#include <QElapsedTimer>

namespace fortiq {
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
    if (!isOpen()) return transferFail(QStringLiteral("La session SSH n'est pas ouverte."));
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
            "  exec find -H \"$d\" -xdev -path '*/.local/share/Trash' -prune -o -iname \"$p\" -printf '%y\\t%s\\t%T@\\t%p\\0' 2>/dev/null\n"
            "fi\n"
            "printf '#tool:find-basic\\n'\n"
            "exec find -H \"$d\" -xdev -path '*/.local/share/Trash' -prune -o -iname \"$p\" -print0 2>/dev/null\n";
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
        d->closeLost();
        return transferFail(QStringLiteral("Connexion SSH perdue."));
    }
    libssh2_channel_handle_extended_data2(channel, LIBSSH2_CHANNEL_EXTENDED_DATA_IGNORE);
    // The login shell only sees "sh -s"; the script and its data arrive on stdin.
    if (libssh2_channel_exec(channel, "sh -s") != 0) {
        libssh2_channel_free(channel);
        return transferFail(QStringLiteral("Le serveur a refusé d'exécuter la commande."));
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
        d->closeLost();
        return transferFail(QStringLiteral("Connexion SSH perdue pendant la recherche."));
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

void SshSession::setCommandTimeout(int ms) { d->commandTimeoutMs = ms > 0 ? ms : 600000; }

namespace {
// Stops what a finished-with channel left running on the server. The command was started as
// "sh -s fortiq-run-<token>", so its process (and, through sshd's setsid, its whole group) can be found by that token.
void stopRemoteRun(LIBSSH2_SESSION *session, const QByteArray &token)
{
    const QByteArray script =
        "p=$(pgrep -f '[s]h -s " + token + "' | head -n 1); [ -n \"$p\" ] || exit 0\n"
        "g=$(ps -o pgid= -p \"$p\" | tr -d ' '); me=$(ps -o pgid= -p $$ | tr -d ' ')\n"
        "if [ -n \"$g\" ] && [ \"$g\" != \"$me\" ] && [ \"$g\" != 1 ]; then kill -s TERM -- \"-$g\" 2>/dev/null; sleep 1; kill -s KILL -- \"-$g\" 2>/dev/null\n"
        "else pkill -TERM -P \"$p\" 2>/dev/null; kill -TERM \"$p\" 2>/dev/null; fi\nexit 0\n";
    libssh2_session_set_timeout(session, 8000);
    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(session);
    if (!ch) return;
    const QByteArray command = "sh -c " + shQuote(QString::fromLatin1(script));
    if (libssh2_channel_exec(ch, command.constData()) == 0) {
        char sink[256];
        while (libssh2_channel_read(ch, sink, sizeof(sink)) > 0) {}
        libssh2_channel_wait_closed(ch);
    }
    libssh2_channel_free(ch);
}
}  // namespace

SshResult SshSession::runScript(const QByteArray &script, qint64 maxStdout, const std::function<bool()> &keepGoing, int timeoutMs)
{
    if (!isOpen()) return transferFail(QStringLiteral("La session SSH n'est pas ouverte."));
    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(d->handle);
    if (!channel) {
        d->closeLost();
        return transferFail(QStringLiteral("Connexion SSH perdue."));
    }
    const QByteArray token = "fortiq-run-" + randomTag().toLatin1();
    if (libssh2_channel_exec(channel, ("sh -s " + token).constData()) != 0) {
        libssh2_channel_free(channel);
        return transferFail(QStringLiteral("Le serveur a refusé d'exécuter la commande."));
    }
    QElapsedTimer clock;
    clock.start();
    const qint64 deadline = timeoutMs > 0 ? timeoutMs : d->commandTimeoutMs;
    qsizetype sent = 0;
    while (sent < script.size()) {
        const auto w = libssh2_channel_write(channel, script.constData() + sent, static_cast<size_t>(script.size() - sent));
        if (w < 0) { libssh2_channel_free(channel); return transferFail(QStringLiteral("Cannot start the command.")); }
        sent += w;
    }
    libssh2_channel_send_eof(channel);

    libssh2_session_set_timeout(d->handle, 200);
    QByteArray out, err;
    bool truncated = false, stopped = false, lost = false, timedOut = false;
    char buffer[65536];
    while (true) {
        bool any = false;
        if (clock.elapsed() > deadline) { timedOut = true; break; }
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
    if (!lost && (timedOut || stopped || truncated)) stopRemoteRun(d->handle, token);
    if (!truncated && !stopped && !lost && !timedOut) {
        libssh2_session_set_timeout(d->handle, 5000);
        libssh2_channel_wait_closed(channel);
        exitCode = libssh2_channel_get_exit_status(channel);
    }
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
    libssh2_session_set_timeout(d->handle, 15000);
    if (lost) {
        d->closeLost();
        return transferFail(QStringLiteral("Connexion SSH perdue."));
    }
    SshResult r;
    r.status = (stopped || timedOut) ? SshResult::Status::Error : SshResult::Status::Ok;
    r.data = std::move(out);
    r.truncated = truncated;
    r.message = timedOut ? QStringLiteral("La commande a dépassé le délai (%1 s) et a été arrêtée sur le serveur.").arg(deadline / 1000)
                         : QString::fromUtf8(err);
    r.code = timedOut ? QStringLiteral("timeout") : stopped ? QStringLiteral("cancelled") : QStringLiteral("exit:%1").arg(exitCode);
    if (!stopped && !timedOut) LinkMonitor::reportOk(d->key);
    return r;
}
}  // namespace fortiq
