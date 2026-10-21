#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantList>

#include <memory>

namespace fortiq {
struct ConnectionRequest {
    QString host;
    int port = 22;
    QString user;
    QString password; // RAM only, never persisted or logged; key passphrase when keyPath is set
    QString keyPath;  // local OpenSSH private key file; empty = password auth
    QString expectedHostFingerprint;
    QString directory; // empty = remote SFTP home
};

struct SshResult {
    enum class Status { Ok, TrustRequired, Error };
    Status status = Status::Error;
    QString message;
    QString fingerprint;
    QString directory;
    QString home;
    QVariantList entries;
    QByteArray data;      // readFile(): bytes read, never written to disk
    qint64 fileSize = -1; // readFile(): full size reported by the server
    bool truncated = false;
};

// One authenticated SSH connection with an SFTP channel, reused for every
// listing. Blocking by design: all calls must come from ONE worker thread
// (the controller's single-thread pool), because the TCP socket lives there.
class SshSession {
public:
    SshSession();
    ~SshSession();
    SshSession(const SshSession &) = delete;
    SshSession &operator=(const SshSession &) = delete;

    // Handshake, host-key pin check BEFORE authentication, auth, SFTP, then
    // lists req.directory (or home). On TrustRequired/Error the session is closed.
    SshResult open(const ConnectionRequest &req);
    // Lists an absolute directory on the open session.
    SshResult list(const QString &directory);
    // Reads at most maxBytes of a regular file, read-only (SFTP READ open).
    SshResult readFile(const QString &path, qint64 maxBytes);
    bool isOpen() const;
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

class SshClient {
public:
    // One-shot connect + list + close (used by tests and simple checks).
    static SshResult list(const ConnectionRequest &request);
};
}
