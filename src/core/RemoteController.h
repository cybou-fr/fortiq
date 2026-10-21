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
    // Transfer queue: [{id, upload, name, remote, local, state, done, total, message}]
    // state: queued | running | conflict | done | error | cancelled | unknown
    Q_PROPERTY(QVariantList transfers READ transfers NOTIFY transfersChanged)
    Q_PROPERTY(int activeTransfers READ activeTransfers NOTIFY transfersChanged)
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
    bool previewOpen() const { return !m_previewPath.isEmpty(); }
    bool previewBusy() const { return m_previewBusy; }
    QString previewPath() const { return m_previewPath; }
    QString previewKind() const { return m_previewKind; }
    QString previewText() const { return m_previewText; }
    QString previewImage() const { return m_previewImage; }
    QString previewInfo() const { return m_previewInfo; }
    bool previewEditable() const { return m_previewKind == QLatin1String("text") && !m_editHash.isEmpty(); }
    bool saving() const { return m_saving; }
    QVariantList transfers() const;
    int activeTransfers() const;
    QString rememberedHost() const;
    QString rememberedUser() const;
    int rememberedPort() const;
    QString rememberedKeyPath() const;

    // keyPath empty = password auth; otherwise `password` is the key passphrase (may be empty).
    Q_INVOKABLE void connectServer(QString host, int port, QString user, QString password, QString keyPath = {});
    Q_INVOKABLE void trustHost();
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
    Q_INVOKABLE void cancelTransfer(int id);
    Q_INVOKABLE void clearFinishedTransfers();
    // File operations in the current folder; each ends with operationFinished.
    Q_INVOKABLE void createFolder(const QString &name);
    Q_INVOKABLE void renameEntry(const QString &path, const QString &newName);
    Q_INVOKABLE void trashEntry(const QString &path);
    // Trash actions. keepBoth restores as "name (n)" when the name is taken.
    Q_INVOKABLE void restoreEntry(const QString &trashedPath, bool keepBoth = false);
    Q_INVOKABLE void deletePermanently(const QString &trashedPath);
    Q_INVOKABLE void emptyTrash();

signals:
    void changed();
    void previewChanged();
    void transfersChanged();
    void operationBusyChanged();
    void savingChanged();
    // code: "", "conflict" (changed on the server) or "unknown".
    void saveFinished(bool ok, const QString &message, const QString &code, const QString &path);
    // code: "", "conflict" or "unknown" (outcome not known after a drop).
    void operationFinished(bool ok, const QString &message, const QString &code, const QString &path);

private:
    enum class Kind { Open, List };
    void request(Kind kind, const QString &directory, bool record);
    void scrubPassword();
    void runOperation(const QString &doneMessage, std::function<SshResult(SshSession &)> op);
    struct Transfer;
    void startTransfer(int id, int policy);
    Transfer *findTransfer(int id);
    QString pinKey() const;

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

    struct Transfer {
        int id = 0;
        bool upload = false;
        QString name;
        QString remote; // download: source file; upload: target folder
        QString local;
        QString state = QStringLiteral("queued");
        QString message;
        qint64 done = 0;
        qint64 total = -1;
        std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
    };
    QList<Transfer> m_transfers;
    int m_nextTransferId = 1;
    QThreadPool m_transferPool;
    std::shared_ptr<std::unique_ptr<SshSession>> m_transferSession;
};
}
