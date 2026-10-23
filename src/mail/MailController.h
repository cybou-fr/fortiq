#pragma once

#include "MailInfo.h"

#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
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

signals:
    void changed();
    void threadsChanged();
    void threadChanged();

private:
    enum class Kind { Overview, Search, More, Thread, Tag };
    struct Job {
        Kind kind;
        QByteArray script;
        QString key;          // thread id for Thread / Tag
        QStringList add, remove;
        int seq = 0, gen = 0;
    };
    struct Outcome;

    void onRemoteChanged();
    void resetAll(const QString &state, const QString &status);
    void enqueue(const Job &job);
    void pump();
    void finish(const Job &job, const Outcome &out);
    void startSearch(const QString &query, const QString &folderKey);
    void applyLocalTags(const QString &threadId, const QStringList &add, const QStringList &remove);

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
};
}  // namespace fortiq
