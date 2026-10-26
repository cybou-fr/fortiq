#include "SshTunnel.h"

#include "../../core/SecureWipe.h"
#include "../../ssh/SshAlgorithms.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QTcpSocket>
#include <QThread>

#include <libssh2.h>

#include <algorithm>
#include <cstdio>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#endif

namespace fortiq::gateway {

namespace {
constexpr int kChunk = 32768;                 // one read / write step
constexpr qint64 kPostedLimit = 1 << 20;      // bytes delivered to the owner and not yet processed, per channel
constexpr int kMaxExec = 3;                   // sshd's MaxSessions defaults to 10: keep room for the others
constexpr int kMaxErrText = 4096;

// FORTIQ_GATEWAY_TRACE=1 prints the transport events of the tunnel (no payload, no secret) to stderr: for diagnosing a server or network.
bool traceOn() { static const bool on = qEnvironmentVariableIsSet("FORTIQ_GATEWAY_TRACE"); return on; }
#define TRACE(...) do { if (traceOn()) { fprintf(stderr, "[tunnel] " __VA_ARGS__); fputc(10, stderr); } } while (0)

std::once_flag g_initFlag;
int g_initRc = -1;

// Errors that concern ONE channel (it is closed, the session lives on); every other error means the transport is gone.
bool channelScope(int rc) {
    switch (rc) {
    case LIBSSH2_ERROR_CHANNEL_FAILURE: case LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED: case LIBSSH2_ERROR_CHANNEL_UNKNOWN:
    case LIBSSH2_ERROR_CHANNEL_CLOSED: case LIBSSH2_ERROR_CHANNEL_EOF_SENT: case LIBSSH2_ERROR_CHANNEL_OUTOFORDER:
    case LIBSSH2_ERROR_CHANNEL_WINDOW_EXCEEDED: case LIBSSH2_ERROR_CHANNEL_PACKET_EXCEEDED: case LIBSSH2_ERROR_REQUEST_DENIED:
        return true;
    default:
        return false;
    }
}
bool cleanClose(int rc) { return rc == LIBSSH2_ERROR_CHANNEL_CLOSED || rc == LIBSSH2_ERROR_CHANNEL_EOF_SENT; }

bool socketHealthy(libssh2_socket_t fd) {
    char c;
#ifdef Q_OS_WIN
    const int n = ::recv(fd, &c, 1, MSG_PEEK);
    if (n == 0) return false;
    if (n < 0) return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    const auto n = ::recv(fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n == 0) return false;
    if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
    return true;
}

// Waits for the socket (or the timeout). Returns true when it is readable (or in error / hung up).
bool waitSocket(libssh2_socket_t fd, bool wantWrite, int timeoutMs) {
#ifdef Q_OS_WIN
    WSAPOLLFD p;
    p.fd = fd;
    p.events = static_cast<SHORT>(POLLIN | (wantWrite ? POLLOUT : 0));
    p.revents = 0;
    const int rc = WSAPoll(&p, 1, timeoutMs);
#else
    pollfd p;
    p.fd = fd;
    p.events = static_cast<short>(POLLIN | (wantWrite ? POLLOUT : 0));
    p.revents = 0;
    const int rc = ::poll(&p, 1, timeoutMs);
#endif
    if (rc < 0) { QThread::msleep(1); return false; }
    return rc > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR));
}

QString errorText(LIBSSH2_SESSION *s, const QString &prefix) {
    // Never echo server text: the code is enough for a person and for a log.
    return prefix + QStringLiteral(" (SSH code %1)").arg(libssh2_session_last_errno(s));
}
}  // namespace

namespace {
struct Cmd {
    enum class Type { Open, Write, Shutdown, Reading, Close, Exec } type = Type::Close;
    quint64 id = 0;
    QString host;
    int port = 0;
    QByteArray data;
    bool on = true;
    int maxOut = 0;
};

struct Chan {
    enum class St { Queued, Starting, Open, Closing };
    quint64 id = 0;
    bool isExec = false;
    St st = St::Queued;
    LIBSSH2_CHANNEL *ch = nullptr;
    QByteArray hostUtf8;
    int port = 0;
    std::deque<QByteArray> outq;
    qsizetype headOff = 0;
    qsizetype retryLen = 0;        // libssh2 wants the same buffer and length again after EAGAIN
    bool eofRequested = false, eofSent = false;
    bool reading = true;
    bool silent = false;           // closed by the caller: no event
    bool userClosed = false;       // closed while the open was still in flight
    bool retried = false;
    qint64 started = 0;            // ms, when the open began (timeouts)
    std::shared_ptr<std::atomic<qint64>> posted = std::make_shared<std::atomic<qint64>>(0);
    // exec
    QByteArray out, err;
    int maxOut = 0;
    bool truncated = false;
};
}  // namespace

struct SshTunnel::Impl : public QThread {
    SshTunnel *owner = nullptr;
    ConnectionRequest req;
    std::mutex mu;
    std::condition_variable cv;
    std::deque<Cmd> cmds;
    bool stopFlag = false;
    std::atomic<int> state{0};
    std::atomic<int> connectCalls{0};
    std::atomic<int> sessions{0};
    std::atomic<bool> lose{false};
    std::atomic<int> openTimeoutMs{15000};
    std::atomic<int> execTimeoutMs{20000};
    quint64 nextId = 1;   // owner thread only

    // worker thread only
    QTcpSocket *sock = nullptr;
    LIBSSH2_SESSION *sess = nullptr;
    std::map<quint64, Chan> chans;
    std::deque<quint64> openQueue;
    quint64 opening = 0;
    int activeExec = 0;
    QElapsedTimer clock;
    qint64 lastConnectFail = -100000;
    QString lastConnectMsg;
    qint64 lastRx = 0, lastPeek = 0;
    bool wantWrite = false;

    ~Impl() override = default;

    qint64 now() const { return clock.elapsed(); }
    libssh2_socket_t fd() const { return static_cast<libssh2_socket_t>(sock->socketDescriptor()); }

    template <class F> void post(F &&f) { QMetaObject::invokeMethod(owner, std::forward<F>(f), Qt::QueuedConnection); }
    void postState(TunnelBackend::State s, const QString &message) {
        state = static_cast<int>(s);
        post([this, s, message] { emit owner->stateChanged(s, message); });
    }
    void postFailed(quint64 id, const QString &msg) { post([this, id, msg] { emit owner->channelFailed(id, msg); }); }
    void postExecDone(quint64 id, bool ok, const QByteArray &out, const QString &problem) {
        post([this, id, ok, out, problem] { emit owner->execFinished(id, ok, out, problem); });
    }

    void push(Cmd c) {
        { std::lock_guard<std::mutex> lk(mu); cmds.push_back(std::move(c)); }
        cv.notify_one();
    }

    // ---------------------------------------------------------------- connection
    void dropSocketAndSession(bool graceful) {
        if (sess) {
            libssh2_session_set_blocking(sess, 1);
            libssh2_session_set_timeout(sess, 1000);
            if (graceful) libssh2_session_disconnect_ex(sess, SSH_DISCONNECT_BY_APPLICATION, "FORTIQ disconnect", "");
            libssh2_session_free(sess);
            sess = nullptr;
        }
        if (sock) { sock->abort(); delete sock; sock = nullptr; }
    }

    void failQueued(const QString &msg) {
        std::vector<quint64> dead;
        for (auto &kv : chans) if (kv.second.st == Chan::St::Queued) dead.push_back(kv.first);
        for (const quint64 id : dead) {
            Chan &c = chans[id];
            if (c.isExec) postExecDone(id, false, {}, msg); else postFailed(id, msg);
            if (c.isExec && activeExec > 0 && opening == id) --activeExec;
            chans.erase(id);
        }
        openQueue.clear();
        opening = 0;
    }

    bool connectNow() {
        postState(TunnelBackend::State::Connecting, QString());
        auto failConnect = [this](const QString &msg) {
            dropSocketAndSession(false);
            lastConnectFail = now();
            lastConnectMsg = msg;
            postState(TunnelBackend::State::Disconnected, msg);
            failQueued(msg);
            return false;
        };
        if (req.host.trimmed().isEmpty() || req.user.trimmed().isEmpty() || req.port < 1 || req.port > 65535
            || (req.password.isEmpty() && req.keyPath.isEmpty()))
            return failConnect(QStringLiteral("Hôte, port, utilisateur et mot de passe ou clé sont nécessaires."));
        if (req.expectedHostFingerprint.isEmpty())
            return failConnect(QStringLiteral("Empreinte de la clé d'hôte non épinglée : le Browser ne se connecte qu'à un serveur déjà vérifié."));
        std::call_once(g_initFlag, [] { g_initRc = libssh2_init(0); });
        if (g_initRc != 0) return failConnect(QStringLiteral("Échec d'initialisation de la bibliothèque SSH."));

        sock = new QTcpSocket;
        ++connectCalls;
        sock->connectToHost(req.host, static_cast<quint16>(req.port));
        if (!sock->waitForConnected(10000)) return failConnect(QStringLiteral("Serveur injoignable (connexion TCP impossible ou délai dépassé)."));
        sock->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
        sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);

        sess = libssh2_session_init();
        if (!sess) return failConnect(QStringLiteral("Échec de création de la session SSH."));
        libssh2_session_set_blocking(sess, 1);
        libssh2_session_set_timeout(sess, 15000);
        if (!applyAlgorithmPolicy(sess)) return failConnect(QStringLiteral("Cannot apply the SSH algorithm policy."));
        if (libssh2_session_handshake(sess, fd()) != 0) return failConnect(errorText(sess, QStringLiteral("Échec de la négociation SSH")));

        size_t keyLen = 0;
        int keyType = 0;
        const char *hostKey = libssh2_session_hostkey(sess, &keyLen, &keyType);
        if (!hostKey || keyLen == 0) return failConnect(QStringLiteral("Le serveur n'a pas fourni de clé d'hôte utilisable."));
        const QByteArray digest = QCryptographicHash::hash(QByteArray(hostKey, static_cast<qsizetype>(keyLen)), QCryptographicHash::Sha256);
        const QString fingerprint = QStringLiteral("SHA256:") + QString::fromLatin1(digest.toBase64(QByteArray::OmitTrailingEquals));
        if (fingerprint != req.expectedHostFingerprint)   // BEFORE any credential leaves this machine
            return failConnect(QStringLiteral("SSH HOST KEY CHANGED. Connection blocked. Verify server identity out of band."));

        const QByteArray user = req.user.toUtf8();
        SecretBytes password = req.password.toUtf8();
        int rc = 0;
        if (!req.keyPath.isEmpty()) {
            QFile keyFile(req.keyPath);
            constexpr qint64 maxKeyBytes = 1024 * 1024;
            if (!keyFile.open(QIODevice::ReadOnly)) return failConnect(QStringLiteral("Clé privée illisible."));
            SecretBytes key = keyFile.read(maxKeyBytes + 1);
            const bool complete = keyFile.error() == QFileDevice::NoError && keyFile.atEnd();
            keyFile.close();
            if (!complete || key.isEmpty() || key.size() > maxKeyBytes) return failConnect(QStringLiteral("Clé privée vide, illisible ou trop grande."));
            rc = libssh2_userauth_publickey_frommemory(sess, user.constData(), static_cast<size_t>(user.size()), nullptr, 0,
                    key.constData(), static_cast<size_t>(key.size()), password.isEmpty() ? nullptr : password.constData());
            key.wipeNow();
        } else {
            rc = libssh2_userauth_password_ex(sess, user.constData(), static_cast<unsigned int>(user.size()), password.constData(),
                    static_cast<unsigned int>(password.size()), nullptr);
        }
        password.wipeNow();
        if (rc != 0) return failConnect(QStringLiteral("Authentification SSH refusée."));

        libssh2_session_set_blocking(sess, 0);
        libssh2_keepalive_config(sess, 1, 30);
        ++sessions;
        lastRx = lastPeek = now();
        lastConnectMsg.clear();
        postState(TunnelBackend::State::Connected, QString());
        return true;
    }

    // The transport is gone: every channel ends with a reason, requests that never started get one more try on a new session.
    void transportLost(const QString &reason) {
        if (!sess && !sock) return;
        TRACE("transport lost: %s", reason.toUtf8().constData());
        dropSocketAndSession(false);
        std::deque<quint64> requeue;
        std::vector<quint64> erase;
        for (auto &kv : chans) {
            Chan &c = kv.second;
            const bool neverStarted = c.st == Chan::St::Queued;
            if (neverStarted && !c.retried && !c.userClosed) {   // e.g. a connection that died while idle: reconnect and try again
                c.retried = true;
                requeue.push_back(c.id);
                continue;
            }
            if (c.st == Chan::St::Closing || c.silent || c.userClosed) { erase.push_back(c.id); continue; }
            if (c.isExec) postExecDone(c.id, false, c.out, reason);
            else if (neverStarted) postFailed(c.id, reason);
            else { const quint64 id = c.id; post([this, id, reason] { emit owner->channelClosed(id, reason); }); }
            erase.push_back(c.id);
        }
        for (const quint64 id : erase) chans.erase(id);
        openQueue = requeue;
        opening = 0;
        activeExec = 0;
        for (auto &kv : chans) kv.second.ch = nullptr;
        lastConnectFail = -100000;
        postState(TunnelBackend::State::Disconnected, reason);
    }

    // ---------------------------------------------------------------- channels
    void beginClose(Chan &c, bool emitClosed, const QString &reason) {
        if (c.st == Chan::St::Closing) return;
        if (emitClosed && !c.silent) {
            const quint64 id = c.id;
            if (c.isExec) postExecDone(id, true, c.out, QString());
            else post([this, id, reason] { emit owner->channelClosed(id, reason); });
        }
        c.silent = true;
        c.st = Chan::St::Closing;
        c.outq.clear();
    }

    bool stepOpen() {
        if (!opening) {
            for (std::size_t i = 0; i < openQueue.size();) {
                auto found = chans.find(openQueue[i]);
                if (found == chans.end()) { openQueue.erase(openQueue.begin() + static_cast<std::ptrdiff_t>(i)); continue; }
                if (found->second.isExec && activeExec >= kMaxExec) { ++i; continue; }
                opening = openQueue[i];
                found->second.started = now();
                if (found->second.isExec) ++activeExec;
                openQueue.erase(openQueue.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        if (!opening) return false;
        auto it = chans.find(opening);
        if (it == chans.end()) { opening = 0; return true; }
        Chan &c = it->second;
        LIBSSH2_CHANNEL *ch = c.isExec ? libssh2_channel_open_session(sess)
                                       : libssh2_channel_direct_tcpip_ex(sess, c.hostUtf8.constData(), c.port, "127.0.0.1", 0);
        if (ch) {
            c.ch = ch;
            opening = 0;
            if (c.userClosed) { c.silent = true; c.st = Chan::St::Closing; return true; }
            if (c.isExec) c.st = Chan::St::Starting;
            else {
                c.st = Chan::St::Open;
                const quint64 id = c.id;
                post([this, id] { emit owner->channelOpened(id); });
            }
            return true;
        }
        const int rc = libssh2_session_last_errno(sess);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            const int limit = c.isExec ? execTimeoutMs.load() : openTimeoutMs.load();
            if (now() - c.started > limit) transportLost(QStringLiteral("Le serveur ne répond plus (délai d'ouverture dépassé)."));
            return false;
        }
        if (channelScope(rc)) {
            const QString msg = errorText(sess, c.isExec ? QStringLiteral("Le serveur a refusé le canal") : QStringLiteral("Le serveur n'a pas pu joindre l'adresse"));
            const quint64 id = c.id;
            const bool exec = c.isExec;
            if (exec) { postExecDone(id, false, {}, msg); --activeExec; } else if (!c.userClosed) postFailed(id, msg);
            chans.erase(it);
            opening = 0;
            return true;
        }
        transportLost(errorText(sess, QStringLiteral("Connexion SSH perdue")));
        return true;
    }

    // Returns true when something moved.
    bool stepChan(Chan &c) {
        switch (c.st) {
        case Chan::St::Queued: return false;
        case Chan::St::Starting: {
            if (now() - c.started > execTimeoutMs.load()) { chanDone(c, false, QStringLiteral("Délai dépassé sur le serveur.")); return true; }
            const int rc = libssh2_channel_process_startup(c.ch, "exec", 4, "sh -s", 5);
            if (rc == LIBSSH2_ERROR_EAGAIN) return false;
            if (rc != 0) { chanError(c, rc); return true; }
            c.st = Chan::St::Open;
            return true;
        }
        case Chan::St::Open: return ioChan(c);
        case Chan::St::Closing: return closeStep(c);
        }
        return false;
    }

    void chanDone(Chan &c, bool ok, const QString &problem) {
        if (c.isExec && !c.silent) { c.silent = true; postExecDone(c.id, ok, c.out, problem); }
        beginClose(c, !c.isExec, problem);
    }

    void chanError(Chan &c, int rc) {
        if (channelScope(rc)) {
            if (c.isExec) chanDone(c, false, errorText(sess, QStringLiteral("Canal fermé")));
            else beginClose(c, true, cleanClose(rc) ? QString() : errorText(sess, QStringLiteral("Canal fermé par le serveur")));
        } else {
            transportLost(errorText(sess, QStringLiteral("Connexion SSH perdue")));
        }
    }

    bool closeStep(Chan &c) {
        if (c.ch) {
            int rc = libssh2_channel_close(c.ch);
            if (rc == LIBSSH2_ERROR_EAGAIN) return false;
            if (rc != 0 && !channelScope(rc)) { transportLost(errorText(sess, QStringLiteral("Connexion SSH perdue"))); return true; }
            rc = libssh2_channel_free(c.ch);
            if (rc == LIBSSH2_ERROR_EAGAIN) return false;
            c.ch = nullptr;
        }
        if (c.isExec && activeExec > 0) --activeExec;
        return true;   // the caller erases it
    }

    bool ioChan(Chan &c) {
        bool moved = false;
        // ---- write (stdin of an exec channel, request bytes of a tunnel)
        qint64 written = 0;
        while (!c.outq.empty() && !c.eofSent) {
            const QByteArray &front = c.outq.front();
            const qsizetype avail = front.size() - c.headOff;
            const qsizetype len = c.retryLen ? c.retryLen : (std::min<qsizetype>)(avail, kChunk);
            const auto n = libssh2_channel_write_ex(c.ch, 0, front.constData() + c.headOff, static_cast<size_t>(len));
            if (n == LIBSSH2_ERROR_EAGAIN) { c.retryLen = len; wantWrite = true; break; }
            c.retryLen = 0;
            if (n < 0) { chanError(c, static_cast<int>(n)); return true; }
            c.headOff += n;
            written += n;
            moved = true;
            if (c.headOff >= front.size()) { c.outq.pop_front(); c.headOff = 0; }
        }
        if (written > 0 && !c.isExec) {
            const quint64 id = c.id;
            post([this, id, written] { emit owner->channelWritten(id, written); });
        }
        if (c.outq.empty() && c.eofRequested && !c.eofSent) {
            const int rc = libssh2_channel_send_eof(c.ch);
            if (rc == 0) { c.eofSent = true; moved = true; }
            else if (rc != LIBSSH2_ERROR_EAGAIN) { chanError(c, rc); return true; }
        }
        // ---- read
        if (c.reading && c.posted->load() < kPostedLimit) {
            for (int i = 0; i < 8; ++i) {
                char buf[kChunk];
                const auto n = libssh2_channel_read_ex(c.ch, 0, buf, sizeof(buf));
                if (n == LIBSSH2_ERROR_EAGAIN || n == 0) break;
                if (n < 0) { chanError(c, static_cast<int>(n)); return true; }
                moved = true;
                if (c.isExec) {
                    if (c.out.size() + static_cast<qint64>(n) > c.maxOut) { c.truncated = true; c.out.append(buf, static_cast<qsizetype>(std::max<qint64>(0, static_cast<qint64>(c.maxOut) - c.out.size()))); chanDone(c, true, QStringLiteral("truncated")); return true; }
                    c.out.append(buf, n);
                } else {
                    const quint64 id = c.id;
                    QByteArray chunk(buf, static_cast<qsizetype>(n));
                    c.posted->fetch_add(n);
                    auto posted = c.posted;
                    post([this, id, chunk, posted] { posted->fetch_sub(chunk.size()); emit owner->channelData(id, chunk); });
                }
            }
        }
        if (c.isExec) {   // stderr is kept short, only to make a failure explicable
            char buf[1024];
            const auto n = libssh2_channel_read_ex(c.ch, SSH_EXTENDED_DATA_STDERR, buf, sizeof(buf));
            if (n > 0) { moved = true; if (c.err.size() < 4096) c.err.append(buf, n); }
            if (now() - c.started > execTimeoutMs.load()) { chanDone(c, false, QStringLiteral("Délai dépassé sur le serveur.")); return true; }
        }
        // ---- the server is done (EOF with nothing left to read, or a close)
        if (traceOn() && c.isExec) TRACE("exec io: eof=%d", libssh2_channel_eof(c.ch));
        if (libssh2_channel_eof(c.ch) == 1) {
            if (c.isExec) {
                const int status = libssh2_channel_get_exit_status(c.ch);
                chanDone(c, true, status ? QStringLiteral("exit:%1").arg(status) : QString());
            } else {
                beginClose(c, true, QString());
            }
            return true;
        }
        return moved;
    }

    // ---------------------------------------------------------------- commands
    void apply(Cmd &cmd) {
        switch (cmd.type) {
        case Cmd::Type::Open: {
            Chan c;
            c.id = cmd.id;
            c.hostUtf8 = cmd.host.toUtf8();
            c.port = cmd.port;
            chans.emplace(c.id, std::move(c));
            openQueue.push_back(cmd.id);
            return;
        }
        case Cmd::Type::Exec: {
            Chan c;
            c.id = cmd.id;
            c.isExec = true;
            c.maxOut = cmd.maxOut;
            c.outq.push_back(cmd.data);
            c.eofRequested = true;
            chans.emplace(c.id, std::move(c));
            openQueue.push_back(cmd.id);
            return;
        }
        default: break;
        }
        auto it = chans.find(cmd.id);
        if (it == chans.end()) return;
        Chan &c = it->second;
        switch (cmd.type) {
        case Cmd::Type::Write: if (c.st != Chan::St::Closing && !c.eofRequested) c.outq.push_back(cmd.data); break;
        case Cmd::Type::Shutdown: c.eofRequested = true; break;
        case Cmd::Type::Reading: c.reading = cmd.on; break;
        case Cmd::Type::Close:
            if (c.st == Chan::St::Queued) {
                if (opening == c.id) { c.userClosed = true; c.silent = true; }   // the open is in flight: finish it, then close
                else {
                    openQueue.erase(std::remove(openQueue.begin(), openQueue.end(), c.id), openQueue.end());
                    chans.erase(it);
                }
            } else {
                c.silent = true;
                c.userClosed = true;
                beginClose(c, false, QString());
            }
            break;
        default: break;
        }
    }

    bool pump() {
        bool moved = stepOpen();
        if (!sess) return moved;
        std::vector<quint64> ids;
        ids.reserve(chans.size());
        for (const auto &kv : chans) ids.push_back(kv.first);
        for (const quint64 id : ids) {
            auto it = chans.find(id);
            if (it == chans.end() || !sess) continue;
            Chan &c = it->second;
            const bool wasClosing = c.st == Chan::St::Closing;
            const bool did = stepChan(c);
            moved |= did;
            if (!sess) return true;
            if (wasClosing && did) chans.erase(id);   // closeStep finished: the channel is freed
        }
        if (!chans.empty()) {
            int next = 0;
            libssh2_keepalive_send(sess, &next);
        }
        return moved;
    }

    void run() override {
        clock.start();
        for (;;) {
            std::deque<Cmd> batch;
            {
                std::unique_lock<std::mutex> lk(mu);
                if (cmds.empty() && !stopFlag && chans.empty()) cv.wait_for(lk, std::chrono::milliseconds(sess ? 200 : 1000));
                if (stopFlag) break;
                batch.swap(cmds);
            }
            for (Cmd &c : batch) apply(c);
            if (lose.exchange(false) && sess && sock) {   // the next call on the session fails like a cut cable (the descriptor stays ours)
#ifdef Q_OS_WIN
                ::shutdown(fd(), SD_BOTH);
#else
                ::shutdown(fd(), SHUT_RDWR);
#endif
            }

            if (!sess) {
                bool wanted = false;
                for (const auto &kv : chans) if (kv.second.st == Chan::St::Queued) { wanted = true; break; }
                if (!wanted) {
                    // channels left over from a lost session are gone already; closing ones just vanish
                    chans.clear();
                    continue;
                }
                if (now() - lastConnectFail < 1500 && !lastConnectMsg.isEmpty()) failQueued(lastConnectMsg);
                else if (!connectNow()) continue;
            }
            const bool moved = pump();
            if (!sess) continue;
            if (chans.empty()) {
                if (now() - lastPeek > 1000) {
                    lastPeek = now();
                    if (!socketHealthy(fd())) transportLost(QStringLiteral("Connexion SSH perdue (fermée par le serveur)."));
                }
                continue;
            }
            if (moved) { lastRx = now(); continue; }
            const bool readable = waitSocket(fd(), wantWrite, 3);
            wantWrite = false;
            if (readable) {
                lastRx = now();
                // Readable but libssh2 had nothing to deliver: either data for channels that are paused (backpressure) or the end of
                // the connection, which libssh2 does not report by itself while it only polls channels. Look at the socket.
                if (!socketHealthy(fd())) transportLost(QStringLiteral("Connexion SSH fermée par le serveur."));
                else QThread::usleep(300);
            }
            else if (now() - lastRx > 90000) transportLost(QStringLiteral("Le serveur ne répond plus (aucun trafic depuis 90 s)."));
        }
        // shutdown: the owner is gone or going; close politely, tell nobody
        if (sess) {
            for (auto &kv : chans) if (kv.second.ch) { libssh2_session_set_blocking(sess, 1); libssh2_session_set_timeout(sess, 1000); libssh2_channel_free(kv.second.ch); kv.second.ch = nullptr; }
            dropSocketAndSession(true);
        }
        if (sock) { sock->abort(); delete sock; sock = nullptr; }
    }
};

// ---------------------------------------------------------------- public API (owner thread)
SshTunnel::SshTunnel(const ConnectionRequest &request, QObject *parent) : TunnelBackend(parent), d(std::make_unique<Impl>()) {
    d->owner = this;
    d->req = request;
    d->start();
}

SshTunnel::~SshTunnel() {
    { std::lock_guard<std::mutex> lk(d->mu); d->stopFlag = true; }
    d->cv.notify_all();
    d->wait();
}

quint64 SshTunnel::openChannel(const QString &ip, int port) {
    Cmd c;
    c.type = Cmd::Type::Open;
    c.id = d->nextId++;
    c.host = ip;
    c.port = port;
    d->push(c);
    return c.id;
}
void SshTunnel::write(quint64 id, const QByteArray &data) {
    if (data.isEmpty()) return;
    Cmd c; c.type = Cmd::Type::Write; c.id = id; c.data = data;
    d->push(std::move(c));
}
void SshTunnel::shutdownWrite(quint64 id) { Cmd c; c.type = Cmd::Type::Shutdown; c.id = id; d->push(std::move(c)); }
void SshTunnel::setReading(quint64 id, bool on) { Cmd c; c.type = Cmd::Type::Reading; c.id = id; c.on = on; d->push(std::move(c)); }
void SshTunnel::closeChannel(quint64 id) { Cmd c; c.type = Cmd::Type::Close; c.id = id; d->push(std::move(c)); }
quint64 SshTunnel::exec(const QByteArray &script, int maxOut) {
    Cmd c;
    c.type = Cmd::Type::Exec;
    c.id = d->nextId++;
    c.data = script;
    c.maxOut = maxOut;
    d->push(c);
    return c.id;
}
TunnelBackend::State SshTunnel::state() const { return static_cast<State>(d->state.load()); }
int SshTunnel::connectCalls() const { return d->connectCalls.load(); }
int SshTunnel::sessionsEstablished() const { return d->sessions.load(); }
void SshTunnel::simulateTransportLoss() { d->lose = true; d->cv.notify_all(); }
void SshTunnel::setTimeouts(int channelOpenMs, int execMs) { d->openTimeoutMs = channelOpenMs; d->execTimeoutMs = execMs; }

}  // namespace fortiq::gateway
