#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantList>

#include <functional>
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
    // Transfers: "conflict", "cancelled", "unknown" (outcome on the server not
    // known after a drop), or empty. `directory` holds the final path on Ok.
    QString code;
    qint64 bytes = 0;
};

// Called after every chunk; return false to cancel before publication.
using TransferProgress = std::function<bool(qint64 done, qint64 total)>;
enum class ConflictPolicy { Fail, Replace, KeepBoth };

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
    // Remote regular file -> local file. Written through QSaveFile (local
    // temporary + atomic commit), size verified before commit.
    SshResult download(const QString &remotePath, const QString &localPath, const TransferProgress &progress);
    // Local file -> remoteDir/name. Exclusive unique temporary on the server,
    // size verified, then published without silently replacing: Fail reports
    // "conflict", KeepBoth picks "name (n).ext", Replace keeps the original
    // under a backup name until the new file is in place.
    SshResult upload(const QString &localPath, const QString &remoteDir, const QString &name,
                     ConflictPolicy policy, const TransferProgress &progress);
    // New folder `name` in `dir` (mode 0755 before umask); never reuses an
    // existing name ("conflict").
    SshResult makeDirectory(const QString &dir, const QString &name);
    // Renames within the same folder; refuses an existing target ("conflict").
    SshResult renameEntry(const QString &path, const QString &newName);
    // Moves a file or folder to the user's freedesktop.org Trash
    // (~/.local/share/Trash: files/ + info/<name>.trashinfo), the format used
    // by gio, trash-cli and desktop file managers, so it stays restorable.
    // Never falls back to deletion; a folder on another filesystem fails.
    SshResult moveToTrash(const QString &path);
    // Restores Trash/files/<name> to the Path in its .trashinfo. An occupied
    // original name is a "conflict" unless KeepBoth picks "name (n)"; a
    // missing original folder is an error (nothing is recreated silently).
    SshResult restoreFromTrash(const QString &trashedPath, ConflictPolicy policy);
    // Permanent deletion, only for items inside Trash/files (recursive for
    // folders, symlinks removed, never followed) and their .trashinfo.
    SshResult deleteFromTrash(const QString &trashedPath);
    // Permanently deletes everything in Trash/files and Trash/info.
    SshResult emptyTrash();
    QString homeDirectory() const;
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
