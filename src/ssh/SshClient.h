#pragma once

#include <QString>
#include <QVariantList>

namespace fortiq {
struct ConnectionRequest {
    QString host;
    int port = 22;
    QString user;
    QString password; // RAM only, never persisted or logged
    QString expectedHostFingerprint;
    QString directory; // empty = remote SFTP home
};

struct SshResult {
    enum class Status { Ok, TrustRequired, Error };
    Status status = Status::Error;
    QString message;
    QString fingerprint;
    QString directory;
    QVariantList entries;
};

class SshClient {
public:
    // A blocking call by design: always invoked in QtConcurrent's worker pool.
    // The initial milestone reconnects per listing; persistent channels,
    // cancellability and key-based auth are deliberately still pending.
    static SshResult list(const ConnectionRequest &request);
};
}
