#pragma once

#include "MailAccount.h"
#include "MailAttachment.h"
#include "MailInfo.h"
#include "MailSend.h"

#include <QHash>
#include <QObject>
#include <QUrl>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>

namespace fortiq {
class RemoteController;
class SshSession;

// Mail app (read + tags): `notmuch` on the selected server over one persistent SSH
// session. Nothing is sent, deleted or moved; tags are the only changes (unread,
// flagged, inbox = archive, deleted = trash marker). Bound to the server session:
// a new server clears everything. See docs/MAIL.md.
class MailController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)           // disconnected | loading | ready | error
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(QVariantList folders READ folders NOTIFY changed)
    Q_PROPERTY(QString folderKey READ folderKey NOTIFY changed)
    Q_PROPERTY(QString query READ query NOTIFY changed)
    Q_PROPERTY(QVariantList threads READ threads NOTIFY threadsChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY threadsChanged)
    Q_PROPERTY(QString selectedThreadId READ selectedThreadId NOTIFY threadChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY threadChanged)
    Q_PROPERTY(bool threadBusy READ threadBusy NOTIFY threadChanged)
    Q_PROPERTY(QString threadStatus READ threadStatus NOTIFY threadChanged)
    Q_PROPERTY(QString configPath READ configPath NOTIFY changed)
    Q_PROPERTY(bool markReadOnOpen READ markReadOnOpen WRITE setMarkReadOnOpen NOTIFY changed)
    // sending
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)            // msmtp accounts of the server: {name, from}
    Q_PROPERTY(QString displayName READ displayName WRITE setDisplayName NOTIFY accountsChanged)
    Q_PROPERTY(QString sendState READ sendState NOTIFY sendChanged)                   // idle | sending | sent | warning | failed | unknown
    Q_PROPERTY(QString sendStatus READ sendStatus NOTIFY sendChanged)
    // account wizard and tests
    Q_PROPERTY(QString attachStatus READ attachStatus NOTIFY attachChanged)
    Q_PROPERTY(bool actionBusy READ actionBusy NOTIFY actionChanged)
    Q_PROPERTY(QString actionStatus READ actionStatus NOTIFY actionChanged)
    Q_PROPERTY(QString actionOutput READ actionOutput NOTIFY actionChanged)           // text of the last test / sync / write
    Q_PROPERTY(QVariantMap setupState READ setupState NOTIFY setupChanged)            // what the server has (never secrets)
    Q_PROPERTY(QVariantList setupFiles READ setupFiles NOTIFY setupChanged)           // exact files previewed, awaiting the user's consent

public:
    explicit MailController(RemoteController *remote, QObject *parent = nullptr);
    ~MailController() override;

    bool connected() const;
    QString state() const { return m_state; }
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    QString version() const { return m_version; }
    QVariantList folders() const { return m_folders; }
    QString folderKey() const { return m_folderKey; }
    QString query() const { return m_query; }
    QVariantList threads() const { return m_threadsVariant; }
    bool hasMore() const { return m_hasMore; }
    QString selectedThreadId() const { return m_selectedThread; }
    QVariantList messages() const { return m_messagesVariant; }
    bool threadBusy() const { return m_threadBusy; }
    QString threadStatus() const { return m_threadStatus; }
    QString configPath() const { return m_configPath; }
    bool markReadOnOpen() const { return m_markRead; }
    void setMarkReadOnOpen(bool on);
    QVariantList accounts() const { return m_accountsVariant; }
    QString displayName() const { return m_displayName; }
    void setDisplayName(const QString &name);
    QString sendState() const { return m_sendState; }
    QString sendStatus() const { return m_sendStatus; }
    bool actionBusy() const { return m_actionBusy; }
    QString attachStatus() const { return m_attachStatus; }
    QString actionStatus() const { return m_actionStatus; }
    QString actionOutput() const { return m_actionOutput; }
    QVariantMap setupState() const { return m_setupStateVariant; }
    QVariantList setupFiles() const { return setupFilesToVariant(m_previewFiles); }

    Q_INVOKABLE void setWindowActive(bool active);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openFolder(const QString &key);
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void openThread(const QString &threadId);
    Q_INVOKABLE void closeThread();
    // Only these tags may be changed from the UI: unread, flagged, inbox, deleted.
    Q_INVOKABLE void setTag(const QString &threadId, const QString &tag, bool on);
    Q_INVOKABLE void setConfigPath(const QString &path);

    // ---- sending ----
    // draft: account, to, cc, bcc, subject, body, inReplyTo. The message is built here and sent by msmtp
    // on the server; the call returns an error text at once when the draft is invalid (nothing is sent).
    Q_INVOKABLE QString send(const QVariantMap &draft);
    Q_INVOKABLE QVariantMap replyDraft(const QVariantMap &message) const;
    Q_INVOKABLE void resetSend();

    // ---- account wizard ----
    Q_INVOKABLE void probeSetup(const QString &accountName);      // reads what exists on the server (no secrets)
    // Validates the form and builds the exact files; nothing is written. Returns an error text, empty on success.
    Q_INVOKABLE QString previewSetup(const QVariantMap &spec);
    // Writes the previewed files named in `keys` (the user's consent). Anything not previewed is refused.
    Q_INVOKABLE void applySetup(const QStringList &keys);
    Q_INVOKABLE void clearSetup();                                  // forgets the preview (and the password in it)
    Q_INVOKABLE void testSmtp(const QString &account);
    Q_INVOKABLE void testImap(const QString &account);
    Q_INVOKABLE void syncNow(const QString &account);               // mbsync (pull only) then notmuch new
    Q_INVOKABLE void cancelAction();

    // ---- attachments (they stay on the server) ----
    // Writes the decoded part into ~/Downloads on the server (never overwrites: "name (1).ext").
    Q_INVOKABLE void saveAttachmentToServer(const QString &messageId, int part, const QString &name);
    // Extracts to a temporary copy on the server, downloads it to `localFile` as an ordinary download task, then removes the copy.
    Q_INVOKABLE void downloadAttachment(const QString &messageId, int part, const QString &name, const QUrl &localFile);
    Q_INVOKABLE QString attachmentFileName(const QString &name) const;

signals:
    void changed();
    void threadsChanged();
    void threadChanged();
    void accountsChanged();
    void sendChanged();
    void actionChanged();
    void attachChanged();
    void setupChanged();

private:
    enum class Kind { Overview, Search, More, Thread, Tag, Accounts };
    struct Job {
        Kind kind;
        QByteArray script;
        QString key;          // thread id for Thread / Tag
        QStringList add, remove;
        int seq = 0, gen = 0;
    };
    struct Outcome;
    struct ActionOutcome;

    void onRemoteChanged();
    void resetAll(const QString &state, const QString &status);
    void enqueue(const Job &job);
    void pump();
    void finish(const Job &job, const Outcome &out);
    void startSearch(const QString &query, const QString &folderKey);
    void applyLocalTags(const QString &threadId, const QStringList &add, const QStringList &remove);
    // One long action at a time (send, write, test, sync) on its own SSH session and thread.
    void runAction(const QString &kind, const QString &title, const QString &destination, bool cancellable, const QByteArray &script, qint64 maxOutput,
                   std::function<void(const ActionOutcome &, int taskId)> done);
    void setActionStatus(const QString &status, const QString &output = QString());
    void setAttachStatus(const QString &text);
    void cleanupTemp(const QString &directory);

    RemoteController *m_remote;
    QThreadPool m_pool;
    std::shared_ptr<std::unique_ptr<SshSession>> m_session;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QList<Job> m_queue;
    bool m_running = false;
    int m_seq = 0, m_token = -1, m_searchGen = 0, m_threadGen = 0;
    bool m_busy = false, m_active = false, m_threadBusy = false, m_hasMore = false, m_markRead = true;
    QString m_state, m_status, m_version, m_folderKey = QStringLiteral("inbox"), m_query, m_selectedThread, m_threadStatus, m_configPath;
    QVariantList m_folders, m_threadsVariant, m_messagesVariant;
    QList<MailThread> m_threads;
    int m_offset = 0;
    // sending / wizard
    QThreadPool m_actionPool;
    std::shared_ptr<std::unique_ptr<SshSession>> m_actionSession;
    std::shared_ptr<std::atomic_bool> m_actionCancel;
    int m_actionSeq = 0, m_actionTask = 0;
    bool m_actionBusy = false;
    QHash<int, QString> m_tempCopies;   // download task id -> temporary directory on the server
    QString m_attachStatus;
    QString m_displayName, m_sendState = QStringLiteral("idle"), m_sendStatus, m_actionStatus, m_actionOutput;
    QVariantList m_accountsVariant;
    QList<MailSendAccount> m_accounts;
    MailSetupState m_setupState;
    QVariantMap m_setupStateVariant;
    QList<MailSetupFile> m_previewFiles;
    QString m_previewAccount;
};
}  // namespace fortiq
