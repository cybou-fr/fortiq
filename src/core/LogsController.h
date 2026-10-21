#pragma once

#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <memory>

namespace fortiq {
class RemoteController;
class SshSession;
struct SshResult;

// Logs app backend: reads the systemd journal (journalctl -o json) and plain
// files under /var/log over its own SSH connection, read-only, bounded to the
// last 2000 events. Grouping and counts are deterministic, no AI. Log text is
// untrusted data: it is only ever displayed as plain text.
class LogsController : public QObject {
    Q_OBJECT
    // source: "journal" | "user" | "unit" | "file"
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(QString target READ target NOTIFY changed)
    Q_PROPERTY(QString range READ range NOTIFY changed)
    Q_PROPERTY(int maxPriority READ maxPriority NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool following READ following NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed) // "", "limited", "missing", "denied", "error", "disconnected"
    Q_PROPERTY(QString stateText READ stateText NOTIFY changed)
    Q_PROPERTY(QString coverage READ coverage NOTIFY changed)
    Q_PROPERTY(QString lastUpdate READ lastUpdate NOTIFY changed)
    Q_PROPERTY(QVariantList events READ events NOTIFY changed)
    Q_PROPERTY(QVariantList patterns READ patterns NOTIFY changed)
    Q_PROPERTY(QVariantList counts READ counts NOTIFY changed)
    Q_PROPERTY(QVariantList units READ units NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY sourcesChanged)

public:
    explicit LogsController(RemoteController *remote, QObject *parent = nullptr);
    ~LogsController() override;

    QString source() const { return m_source; }
    QString target() const { return m_target; }
    QString range() const { return m_range; }
    int maxPriority() const { return m_maxPriority; }
    bool busy() const { return m_busy; }
    bool following() const { return m_following; }
    QString state() const { return m_state; }
    QString stateText() const { return m_stateText; }
    QString coverage() const { return m_coverage; }
    QString lastUpdate() const { return m_lastUpdate; }
    QVariantList events() const { return m_events; }
    QVariantList patterns() const { return m_patterns; }
    QVariantList counts() const { return m_counts; }
    QVariantList units() const { return m_units; }
    QVariantList files() const { return m_files; }

    // Loads the unit and /var/log file lists for the sidebar.
    Q_INVOKABLE void loadSources();
    Q_INVOKABLE void select(const QString &source, const QString &target = {});
    // range: "15min" | "1h" | "24h" | "7d" | "boot"
    Q_INVOKABLE void setRange(const QString &range);
    // 0..7 syslog priority threshold (3 = errors, 4 = warnings, 6 = info, 7 = all).
    Q_INVOKABLE void setMaxPriority(int priority);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void setFollowing(bool on);

    // Deterministic grouping key: numbers, hex, UUIDs, IPs masked. Public for tests.
    static QString templateOf(const QString &message);
    static QVariantList groupPatterns(const QVariantList &events);
    static QVariantMap parseJournalLine(const QByteArray &line);

signals:
    void changed();
    void sourcesChanged();

private:
    void run(bool append);
    void poll();
    void apply(const SshResult &r, bool append, int seq);
    void recompute();
    void disconnected();
    QByteArray journalScript(bool append) const;
    QByteArray fileScript(bool append) const;

    RemoteController *m_remote;
    QThreadPool m_pool;
    std::shared_ptr<std::unique_ptr<SshSession>> m_session;
    QTimer m_followTimer;
    int m_seq = 0;
    int m_token = -1;
    QString m_source = QStringLiteral("journal");
    QString m_target;
    QString m_range = QStringLiteral("1h");
    int m_maxPriority = 6;
    bool m_busy = false;
    bool m_following = false;
    QString m_state;
    QString m_stateText;
    QString m_coverage;
    QString m_lastUpdate;
    QString m_cursor;     // journal: last __CURSOR seen
    qint64 m_fileOffset = 0;
    QString m_fileInode;
    QVariantList m_events;
    QVariantList m_patterns;
    QVariantList m_counts;
    QVariantList m_units;
    QVariantList m_files;
};
}
