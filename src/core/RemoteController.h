#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>

#include <atomic>
#include <functional>
#include <memory>

namespace fortiq {
class SshSession;
struct SshResult;
struct ConnectionRequest;
class TaskManager;

class RemoteController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString currentDirectory READ currentDirectory NOTIFY changed)
    Q_PROPERTY(QString homeDirectory READ homeDirectory NOTIFY changed)
    Q_PROPERTY(QString trashDirectory READ trashDirectory NOTIFY changed)
    Q_PROPERTY(bool operationBusy READ operationBusy NOTIFY operationBusyChanged)
    Q_PROPERTY(QString serverLabel READ serverLabel NOTIFY changed)
    Q_PROPERTY(QString pendingFingerprint READ pendingFingerprint NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY changed)
    Q_PROPERTY(int lastLatencyMs READ lastLatencyMs NOTIFY changed)
    // File viewer (RAM only; nothing is written to the device).
    Q_PROPERTY(bool previewOpen READ previewOpen NOTIFY previewChanged)
    Q_PROPERTY(bool previewBusy READ previewBusy NOTIFY previewChanged)
    Q_PROPERTY(QString previewPath READ previewPath NOTIFY previewChanged)
    Q_PROPERTY(QString previewKind READ previewKind NOTIFY previewChanged)
    Q_PROPERTY(QString previewText READ previewText NOTIFY previewChanged)
    Q_PROPERTY(QString previewImage READ previewImage NOTIFY previewChanged)
    Q_PROPERTY(QString previewInfo READ previewInfo NOTIFY previewChanged)
    // Editing: complete UTF-8 text files only (not truncated, not binary).
    Q_PROPERTY(bool previewEditable READ previewEditable NOTIFY previewChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    // state: queued | running | conflict | done | error | cancelled | unknown
    // Server-side search (third SSH connection, so browsing stays live).
    Q_PROPERTY(bool searchActive READ searchActive NOTIFY searchChanged)
    Q_PROPERTY(bool searchBusy READ searchBusy NOTIFY searchChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY searchChanged)
    Q_PROPERTY(QString searchMode READ searchMode NOTIFY searchChanged)
    Q_PROPERTY(QString searchScope READ searchScope NOTIFY searchChanged)
    Q_PROPERTY(QString searchInfo READ searchInfo NOTIFY searchChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)
    // Saved servers: bootstrap only (name, host, port, user, key path). No
    // password, no working data. Pins stay keyed by host:port.
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)
    Q_PROPERTY(QString activeServerId READ activeServerId NOTIFY changed)
    // Protected store for remembered passwords / key passphrases (Windows Credential Manager).
    Q_PROPERTY(bool secretStoreAvailable READ secretStoreAvailable CONSTANT)
    Q_PROPERTY(QString rememberedHost READ rememberedHost CONSTANT)
    Q_PROPERTY(QString rememberedUser READ rememberedUser CONSTANT)
    Q_PROPERTY(int rememberedPort READ rememberedPort CONSTANT)
    Q_PROPERTY(QString rememberedKeyPath READ rememberedKeyPath CONSTANT)

public:
    explicit RemoteController(QObject *parent = nullptr);
    ~RemoteController() override;
    bool busy() const { return m_busy; }
    bool connected() const { return m_connected; }
    QString statusText() const { return m_status; }
    QString currentDirectory() const { return m_currentDirectory; }
    QString homeDirectory() const { return m_home; }
    QString trashDirectory() const { return m_home.isEmpty() ? QString() : m_home + QStringLiteral("/.local/share/Trash/files"); }
    bool operationBusy() const { return m_operationBusy; }
    QString serverLabel() const { return m_connected ? m_user + "@" + m_host : QString(); }
    QString pendingFingerprint() const { return m_pendingFingerprint; }
    QVariantList entries() const { return m_entries; }
    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }
    int lastLatencyMs() const { return m_latencyMs; }
    // For sibling services (Logs) that open their own connection to the same
    // pinned server: credentials stay in RAM, the token changes per server.
    ConnectionRequest connectionRequest() const;
    int connectionToken() const { return m_generation; }
    bool previewOpen() const { return !m_previewPath.isEmpty(); }
    bool previewBusy() const { return m_previewBusy; }
    QString previewPath() const { return m_previewPath; }
    QString previewKind() const { return m_previewKind; }
    QString previewText() const { return m_previewText; }
    QString previewImage() const { return m_previewImage; }
    QString previewInfo() const { return m_previewInfo; }
    bool previewEditable() const { return m_previewKind == QLatin1String("text") && !m_editHash.isEmpty(); }
    bool saving() const { return m_saving; }
    QVariantList servers() const;
    QString activeServerId() const { return m_activeServerId; }
    bool searchActive() const { return m_searchActive; }
    bool searchBusy() const { return m_searchBusy; }
    QString searchQuery() const { return m_searchQuery; }
    QString searchMode() const { return m_searchMode; }
    QString searchScope() const { return m_searchScope; }
    QString searchInfo() const { return m_searchInfo; }
    QVariantList searchResults() const { return m_searchResults; }
    // Every transfer, operation, save and search of this server is a task of the shared
    // TaskManager (the application's, or a private one when none was given).
    TaskManager *tasks() const { return m_tasks; }
    void setTaskManager(TaskManager *tasks);
    QString rememberedHost() const;
    QString rememberedUser() const;
    int rememberedPort() const;
    QString rememberedKeyPath() const;

    // keyPath empty = password auth; otherwise `password` is the key passphrase (may be empty).
    Q_INVOKABLE void connectServer(QString host, int port, QString user, QString password, QString keyPath = {});
    Q_INVOKABLE void trustHost();
    // Adds or updates a saved server; returns its id, or "" when invalid.
    Q_INVOKABLE QString saveServer(const QVariantMap &profile);
    Q_INVOKABLE void removeServer(const QString &id);
    // Connects to a saved server; password is the SSH password, or the key
    // passphrase when the profile has a key path (may be empty).
    Q_INVOKABLE void connectTo(const QString &id, const QString &password);
    // Same, and after a successful login stores the secret in the protected store
    // (never in QSettings or a file). A store failure is reported; the login itself stands.
    Q_INVOKABLE void connectToRemembering(const QString &id, const QString &password, bool remember);
    // Connects with the secret remembered for this server; reports an error if it cannot be read.
    Q_INVOKABLE void connectToSaved(const QString &id);
    // Deletes the remembered secret of this server.
    Q_INVOKABLE void forgetSecret(const QString &id);
    bool secretStoreAvailable() const;
    // Any clean absolute folder; the server enforces permissions.
    Q_INVOKABLE void navigate(const QString &path);
    Q_INVOKABLE void openDirectory(const QString &path) { navigate(path); }
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void goHome();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void disconnectServer();
    // Opens a remote file read-only in the viewer: text (first 1 MiB),
    // image (up to 16 MiB) or a hex dump of the first bytes for binaries.
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void closePreview();
    // Saves edited text over the open file (force: overwrite a version that
    // changed on the server, only after the user chose to), or as a copy
    // "name (n)" next to it. Each ends with saveFinished.
    Q_INVOKABLE void saveFile(const QString &text, bool force = false);
    Q_INVOKABLE void saveFileCopy(const QString &text);
    // Transfers run one at a time on a second SSH connection to the same
    // pinned server, so browsing stays responsive.
    Q_INVOKABLE void download(const QString &remotePath, const QUrl &localFile);
    Q_INVOKABLE void upload(const QVariantList &localFiles, const QString &remoteDir = {});
    // policy: "replace" (after explicit confirmation), "keepBoth" or "skip".
    Q_INVOKABLE void resolveConflict(int id, const QString &policy);
    Q_INVOKABLE void cancelTransfer(int id);          // = TaskManager::requestCancel
    Q_INVOKABLE void clearFinishedTransfers();        // = TaskManager::clearFinished
    // File operations in the current folder; each ends with operationFinished.
    Q_INVOKABLE void createFolder(const QString &name);
    Q_INVOKABLE void renameEntry(const QString &path, const QString &newName);
    Q_INVOKABLE void trashEntry(const QString &path);
    // Trash actions. keepBoth restores as "name (n)" when the name is taken.
    Q_INVOKABLE void restoreEntry(const QString &trashedPath, bool keepBoth = false);
    Q_INVOKABLE void deletePermanently(const QString &trashedPath);
    Q_INVOKABLE void emptyTrash();
    // mode: "name" or "content"; searches below the current folder.
    Q_INVOKABLE void startSearch(const QString &query, const QString &mode);
    Q_INVOKABLE void cancelSearch();
    Q_INVOKABLE void clearSearch();

signals:
    void changed();
    void previewChanged();
    void operationBusyChanged();
    void savingChanged();
    void serversChanged();
    void searchChanged();
    // code: "", "conflict" (changed on the server) or "unknown".
    void saveFinished(bool ok, const QString &message, const QString &code, const QString &path);
    // code: "", "conflict" or "unknown" (outcome not known after a drop).
    void operationFinished(bool ok, const QString &message, const QString &code, const QString &path);

private:
    friend class LogsControllerTest;
    enum class Kind { Open, List };
    void request(Kind kind, const QString &directory, bool record);
    void scrubPassword();
    void runOperation(const QString &kind, const QString &title, const QString &destination,
                      const QString &doneMessage, std::function<SshResult(SshSession &)> op);
    struct Transfer;
    void startTransfer(int id, int policy);
    Transfer *findTransfer(int id);
    int createTransferTask(const QString &kind, const QString &name, const QString &destination);
    void onTaskCancelRequested(int id);
    QString pinKey() const;
    QVariantList loadProfiles() const;
    void storeProfiles(const QVariantList &profiles);

    QThreadPool m_pool; // one persistent worker: the SSH socket lives there
    std::shared_ptr<std::unique_ptr<SshSession>> m_session;
    bool m_busy = false;
    bool m_connected = false;
    int m_generation = 0;
    int m_port = 22;
    int m_latencyMs = 0;
    QString m_host;
    QString m_user;
    QString m_password;
    bool m_rememberPending = false;   // store m_password for m_activeServerId after the next successful login
    void setSecretSaved(const QString &id, bool saved);
    QString m_keyPath;
    QString m_pendingFingerprint;
    QString m_status = QStringLiteral("No Linux server connected.");
    QString m_currentDirectory;
    QString m_home;
    QString m_pendingDirectory;
    QVariantList m_entries;
    QStringList m_back;
    QStringList m_forward;
    // RAM-only listing cache for instant back/forward; cleared on disconnect.
    QHash<QString, QVariantList> m_cache;
    bool m_operationBusy = false;
    QString m_activeServerId;
    bool m_saving = false;
    QByteArray m_editHash; // SHA-256 of the bytes the editor was opened from
    bool m_editCrlf = false;
    bool m_editBom = false;
    void runSave(const QString &text, int mode);
    int m_previewSeq = 0;
    bool m_previewBusy = false;
    QString m_previewPath;
    QString m_previewKind;
    QString m_previewText;
    QString m_previewImage;
    QString m_previewInfo;

    // Job details of a transfer task (the task itself lives in the TaskManager).
    struct Transfer {
        int id = 0;     // task id
        bool upload = false;
        QString name;
        QString remote; // download: source file; upload: target folder
        QString local;
        std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
    };
    QHash<int, Transfer> m_transferJobs;
    TaskManager *m_tasks = nullptr;
    std::unique_ptr<TaskManager> m_ownedTasks;
    int m_searchTaskId = 0;
    QThreadPool m_transferPool;
    QThreadPool m_searchPool;
    std::shared_ptr<std::unique_ptr<SshSession>> m_searchSession;
    std::shared_ptr<std::atomic_bool> m_searchCancel = std::make_shared<std::atomic_bool>(false);
    int m_searchSeq = 0;
    bool m_searchActive = false;
    bool m_searchBusy = false;
    QString m_searchQuery;
    QString m_searchMode;
    QString m_searchScope;
    QString m_searchInfo;
    QVariantList m_searchResults;
    std::shared_ptr<std::unique_ptr<SshSession>> m_transferSession;
};
}
