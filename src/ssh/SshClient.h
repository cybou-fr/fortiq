#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantList>

#include "../core/SecureWipe.h"

#include <functional>
#include <memory>
#include <vector>

namespace fortiq {
struct ConnectionRequest {
    QString host;
    int port = 22;
    QString user;
    SecretString password; // wiped when the last holder dies (S-6); RAM only, never persisted or logged; key passphrase when keyPath is set
    QString keyPath;  // local OpenSSH key path or opaque Android content URI; empty = password auth
    QString expectedHostFingerprint;
    QString directory; // empty = remote SFTP home
};

// One directory entry in compact form. A huge folder is kept like this (about 150 bytes per entry instead of a QVariantMap of
// ten keys) and turned into the maps the QML side wants one page at a time (listedPage).
struct ListedEntry {
    QString name;
    QString mode;
    QString owner;
    qint64 size = 0;
    qint64 mtime = 0;
    bool directory = false;
    bool link = false;
};
using Listing = std::vector<ListedEntry>;
constexpr int kListPageSize = 5000;         // entries handed to the UI per page
constexpr int kMaxListedEntries = 300000;   // beyond this the listing is cut and flagged `truncated` (the server may hold millions)

struct SshResult {
    enum class Status { Ok, TrustRequired, Error };
    Status status = Status::Error;
    QString message;
    QString fingerprint;
    QString directory;
    QString home;
    QVariantList entries;
    // list(dir, pageSize): all entries of the folder (sorted: folders first, then by name), `entries` holds only the first page.
    std::shared_ptr<const Listing> listing;
    int total = 0;        // list(): number of entries in the whole folder (may exceed entries.size())
    QByteArray data;      // readFile(): bytes read, never written to disk
    qint64 fileSize = -1; // readFile(): full size reported by the server
    bool truncated = false;
    // Transfers: "conflict", "cancelled", "unknown" (outcome on the server not
    // known after a drop, or local document publication incomplete), "disconnected"
    // (download read-only preflight lost its transport), or empty. `directory` holds the final path on Ok.
    QString code;
    qint64 bytes = 0;
};

// Entries [offset, offset + count) of a sorted listing as the maps the UI uses ({name, directory, link, hidden, path, size, mtime, mode, owner}).
QVariantList listedPage(const QString &directory, const Listing &listing, int offset, int count);

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
    // Lists an absolute directory on the open session. pageSize 0 = every entry in `entries` (at most kMaxListedEntries);
    // pageSize > 0 = `entries` holds only the first page and `listing` the whole sorted folder in compact form (see listedPage).
    SshResult list(const QString &directory, int pageSize = 0);
    // Reads at most maxBytes of a regular file, read-only (SFTP READ open).
    SshResult readFile(const QString &path, qint64 maxBytes);
    // Remote regular file -> local file. Written through QSaveFile (local
    // temporary + atomic commit), size verified before commit. Android content
    // URIs stage first, then copy to the provider non-atomically; failure after
    // opening the provider is "unknown" and may leave a partial document.
    SshResult download(const QString &remotePath, const QString &localPath, const TransferProgress &progress);
    // Local file -> remoteDir/name. Exclusive unique temporary on the server,
    // size verified, then published without silently replacing: Fail reports
    // "conflict", KeepBoth picks "name (n).ext", Replace of an existing regular
    // file uses one atomic posix-rename, preserving mode/uid/gid (no backup
    // gap or non-atomic fallback). An initially absent name is never overwritten
    // if it appears meanwhile. Unsupported replacement fails before changing
    // the original; lost ack is "unknown". No content-version CAS for uploads.
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
    // Safe save of an edited file (SMART_FILES_SPEC: edit/save). The current
    // server bytes must still hash to expectedSha256 ("conflict" otherwise,
    // unless force); new bytes go to an exclusive temporary next to the file
    // with its permissions, are size-checked, the version is re-checked, then
    // published atomically with posix-rename@openssh.com and read back.
    // Symlinks are saved through to their target; files owned by another
    // user are refused (an atomic replace would change their owner).
    // Permissions and owner/group are verified before publication. Hash checks
    // are optimistic: SFTP has no atomic compare-and-swap against external writers.
    SshResult saveFile(const QString &path, const QByteArray &data, const QByteArray &expectedSha256, bool force);
    // Writes data as a new file in dir, "name (n).ext" if the name is taken.
    SshResult saveCopy(const QString &dir, const QString &name, const QByteArray &data);
    // Server-side search below `dir` (SMART_FILES_SPEC: remote search).
    // mode "name": GNU find, case-insensitive substring of the name;
    // mode "content": rg if installed, else grep -rIF, up to 3 lines per file.
    // The query is data: rejected if it holds NUL or a line break, otherwise
    // single-quoted into a POSIX sh script sent on stdin to `sh -s`.
    // Stops after `limit` results or when `keepGoing` returns false.
    // Entries: {path, name, directory, size, mtime, line, text}; `code` holds
    // the tool used ("find", "rg", "grep"); `truncated` = limit reached.
    SshResult search(const QString &dir, const QString &query, const QString &mode, int limit,
                     const std::function<bool(int found)> &keepGoing);
    // Runs a POSIX sh script sent on stdin to `sh -s` (the login shell never
    // parses it). stdout -> data (at most maxStdout bytes, then the channel is
    // closed: `truncated`), stderr -> message (first 8 KiB), exit status ->
    // code "exit:N". Callers must quote data with shellQuote().
    // The command has a deadline (timeoutMs, 0 = the session default, see setCommandTimeout): when it passes, the
    // command is stopped on the server too (best effort, by process group) and the result is Error with code
    // "timeout"; a cancelled or truncated run is stopped the same way, so nothing keeps running unseen.
    SshResult runScript(const QByteArray &script, qint64 maxStdout, const std::function<bool()> &keepGoing = {}, int timeoutMs = 0);
    // Default deadline of runScript() on this session (10 minutes). Tests shorten it.
    void setCommandTimeout(int ms);
    // Interactive PTY on this worker-owned connection; no automatic replay.
    SshResult startTerminal(int columns, int rows);
    // readOutput=false (backpressure: the screen still has unprocessed output) only sends input and leaves the remote output
    // waiting in the SSH window, which stops the remote program; reading resumes with the next call that asks for it.
    SshResult terminalExchange(const QByteArray &input, int columns, int rows, bool readOutput = true);
    static QByteArray shellQuote(const QString &value);
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
