#pragma once

#include "DbInfo.h"

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

// Bases de données app (read-only): SQLite files and PostgreSQL servers queried
// through `sqlite3` / `psql` on the selected Linux server. Profiles belong to a
// saved SSH server; passwords live only in the client's protected store.
// Nothing is written to a database by FORTIQ. See docs/DATABASES.md.
class DbController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY changed)               // a saved server is connected
    Q_PROPERTY(bool secretStoreAvailable READ secretStoreAvailable CONSTANT)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)   // those of the connected server
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QStringList columns READ columns NOTIFY resultChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY resultChanged)
    Q_PROPERTY(QVariantList numeric READ numeric NOTIFY resultChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY resultChanged)
    Q_PROPERTY(QString resultInfo READ resultInfo NOTIFY resultChanged)
    Q_PROPERTY(QVariantList schema READ schema NOTIFY schemaChanged)
    Q_PROPERTY(QString schemaStatus READ schemaStatus NOTIFY schemaChanged)
    // What the connected database role may do (see docs/DATABASES.md): level ok | note | warning | danger | unknown.
    Q_PROPERTY(QVariantMap roleCheck READ roleCheck NOTIFY roleChanged)
    Q_PROPERTY(int timeoutSec READ timeoutSec NOTIFY limitsChanged)
    Q_PROPERTY(int maxRows READ maxRows NOTIFY limitsChanged)

public:
    explicit DbController(RemoteController *remote, QObject *parent = nullptr);
    ~DbController() override;

    bool canEdit() const;
    bool secretStoreAvailable() const;
    QVariantList profiles() const;
    QString selectedId() const { return m_selectedId; }
    QVariantMap selected() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QStringList columns() const { return m_result.columns; }
    QVariantList rows() const { return m_rows; }
    QVariantList numeric() const;
    bool truncated() const { return m_result.truncated; }
    QString resultInfo() const { return m_resultInfo; }
    QVariantList schema() const { return m_schema; }
    QString schemaStatus() const { return m_schemaStatus; }
    QVariantMap roleCheck() const { return m_roleCheck; }
    int timeoutSec() const { return m_limits.timeoutSec; }
    int maxRows() const { return m_limits.maxRows; }

    // `password`: empty keeps the stored one (unless the target changed, which drops it);
    // `clearPassword` removes it. Returns an error text, empty on success.
    Q_INVOKABLE QString saveProfile(const QVariantMap &profile, const QString &password, bool clearPassword);
    Q_INVOKABLE void removeProfile(const QString &id);
    Q_INVOKABLE void select(const QString &id);
    Q_INVOKABLE void runQuery(const QString &sql);
    Q_INVOKABLE void cancelQuery();
    Q_INVOKABLE void loadSchema();
    Q_INVOKABLE void checkRole();
    Q_INVOKABLE void setLimits(int timeoutSec, int maxRows);
    // "SELECT * FROM <quoted object> " text for the editor (identifiers from the database are quoted).
    Q_INVOKABLE QString selectAllFor(const QVariantMap &object) const;
    Q_INVOKABLE void setWindowActive(bool active);

signals:
    void changed();
    void profilesChanged();
    void resultChanged();
    void schemaChanged();
    void limitsChanged();
    void roleChanged();

private:
    void onRemoteChanged();
    void onServersChanged();
    void resetSession(bool clearSelection);
    QList<DbProfile> allProfiles() const;
    void storeProfiles(const QList<DbProfile> &list);
    bool findProfile(const QString &id, DbProfile *out) const;
    // Runs a built script on the worker. mode: 0 = user query, 1 = schema, 2 = role check (the last two are task-less).
    void execute(const QByteArray &script, int mode, const DbProfile &profile, const QString &nullMarker, int taskId);

    RemoteController *m_remote;
    QThreadPool m_pool;
    std::shared_ptr<std::unique_ptr<SshSession>> m_session;
    std::shared_ptr<std::atomic_bool> m_cancel;
    int m_seq = 0, m_token = -1, m_task = 0;
    bool m_busy = false, m_active = false;
    QString m_selectedId, m_status, m_error, m_resultInfo, m_schemaStatus;
    DbLimits m_limits;
    DbResult m_result;
    QVariantList m_rows, m_schema;
    QVariantMap m_roleCheck;
};
}  // namespace fortiq
