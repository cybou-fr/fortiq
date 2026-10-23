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
    // source: "journal" | "user" | "unit" | "userunit" | "file"
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
    // Incident inspector: events around an anchor and a deterministic synopsis.
    Q_PROPERTY(bool inspectorOpen READ inspectorOpen NOTIFY inspectorChanged)
    Q_PROPERTY(bool inspectorBusy READ inspectorBusy NOTIFY inspectorChanged)
    Q_PROPERTY(int inspectorWindow READ inspectorWindow NOTIFY inspectorChanged)
    Q_PROPERTY(int inspectorAnchor READ inspectorAnchor NOTIFY inspectorChanged)
    Q_PROPERTY(QVariantList inspectorEvents READ inspectorEvents NOTIFY inspectorChanged)
    Q_PROPERTY(QVariantMap synopsis READ synopsis NOTIFY inspectorChanged)
    Q_PROPERTY(QString inspectorError READ inspectorError NOTIFY inspectorChanged)
    // Read-only service view for the selected unit.
    Q_PROPERTY(bool serviceBusy READ serviceBusy NOTIFY serviceChanged)
    Q_PROPERTY(QVariantMap serviceInfo READ serviceInfo NOTIFY serviceChanged)
    Q_PROPERTY(QVariantList serviceHistory READ serviceHistory NOTIFY serviceChanged)
    Q_PROPERTY(QVariantList serviceDays READ serviceDays NOTIFY serviceChanged)
    Q_PROPERTY(QString serviceError READ serviceError NOTIFY serviceChanged)

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
    bool inspectorOpen() const { return m_inspectorOpen; }
    bool inspectorBusy() const { return m_inspectorBusy; }
    int inspectorWindow() const { return m_inspectorWindow; }
    int inspectorAnchor() const { return m_inspectorAnchor; }
    QVariantList inspectorEvents() const { return m_inspectorEvents; }
    QVariantMap synopsis() const { return m_synopsis; }
    QString inspectorError() const { return m_inspectorError; }
    bool serviceBusy() const { return m_serviceBusy; }
    QVariantMap serviceInfo() const { return m_serviceInfo; }
    QVariantList serviceHistory() const { return m_serviceHistory; }
    QVariantList serviceDays() const { return m_serviceDays; }
    QString serviceError() const { return m_serviceError; }

    // Loads the unit and /var/log file lists for the sidebar.
    Q_INVOKABLE void loadSources();
    Q_INVOKABLE void select(const QString &source, const QString &target = {});
    // range: "15min" | "1h" | "24h" | "7d" | "boot"
    Q_INVOKABLE void setRange(const QString &range);
    // 0..7 syslog priority threshold (3 = errors, 4 = warnings, 6 = info, 7 = all).
    Q_INVOKABLE void setMaxPriority(int priority);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void setFollowing(bool on);
    // Opens the inspector around an event of the timeline: every unit of the
    // current journal scope (system or user), severities 0..6, within
    // ±windowSeconds; for a file source, the loaded lines in that window.
    Q_INVOKABLE void inspect(const QVariantMap &event, int windowSeconds);
    Q_INVOKABLE void setInspectorWindow(int windowSeconds);
    Q_INVOKABLE void closeInspector();
    // Plain-text report of the synopsis with method and event references.
    Q_INVOKABLE QString inspectorReport() const;
    // Status (systemctl show) and 7-day lifecycle history of the selected
    // unit (source "unit" or "userunit"). Never starts, stops or edits it.
    Q_INVOKABLE void loadService();

    // Deterministic grouping key: numbers, hex, UUIDs, IPs masked. Public for tests.
    static QString templateOf(const QString &message);
    static QVariantList groupPatterns(const QVariantList &events);
    static QVariantMap parseJournalLine(const QByteArray &line);
    // `systemctl show --timestamp=unix` key=value output -> display map.
    static QVariantMap parseServiceShow(const QByteArray &text);
    // Lifecycle events (start, stop, exit, failure, restart, start limit)
    // from journal events, newest first, plus per-day counts for 7 days.
    static QVariantList lifecycleHistory(const QVariantList &events);
    static QVariantList dailyCounts(const QVariantList &history, qint64 nowMs);
    // Deterministic synopsis of time-ordered events around `anchor`:
    // {observed, inferred, unknown, next}, each a list of
    // {text, refs: [event indices], action?, arg?}. No AI; inferred items
    // are temporal/textual hypotheses and say so.
    static QVariantMap synthesize(const QVariantList &events, int anchor, int windowSeconds,
                                  bool limited, bool truncated, bool fileSource);

signals:
    void changed();
    void sourcesChanged();
    void inspectorChanged();
    void serviceChanged();

private:
    friend class LogsControllerTest;
    bool accepts(int token) const;
    void applySources(const SshResult &r, int token, int seq);
    void applyService(const SshResult &r, int token, int seq, bool userScope);
    void run(bool append);
    void poll();
    void apply(const SshResult &r, bool append, int seq);
    void recompute();
    void disconnected();
    // Tasks (TaskManager): reads are transient (no line once done, session change = cancelled), the live follow is one task for as long as it runs.
    int beginRead(const QString &kind, const QString &title);
    void endRead(int taskId, bool current, const SshResult &result);
    void beginFollowTask();
    void endFollowTask(bool cancelled);
    QByteArray journalScript(bool append) const;
    QByteArray fileScript(bool append) const;

    RemoteController *m_remote;
    QThreadPool m_pool;
    std::shared_ptr<std::unique_ptr<SshSession>> m_session;
    QTimer m_followTimer;
    int m_followTask = 0;
    int m_seq = 0;
    int m_sourcesSeq = 0;
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
    QByteArray m_filePending;
    bool m_fileSkipLine = false;
    bool m_fileHistoryLimited = false;
    QVariantList m_events;
    QVariantList m_patterns;
    QVariantList m_counts;
    QVariantList m_units;
    QVariantList m_files;
    void runInspector();
    QVariantMap m_anchorEvent;
    bool m_inspectorOpen = false;
    bool m_inspectorBusy = false;
    int m_inspectorWindow = 300;
    int m_inspectorAnchor = -1;
    int m_inspectorSeq = 0;
    QVariantList m_inspectorEvents;
    QVariantMap m_synopsis;
    QString m_inspectorError;
    bool m_serviceBusy = false;
    int m_serviceSeq = 0;
    QVariantMap m_serviceInfo;
    QVariantList m_serviceHistory;
    QVariantList m_serviceDays;
    QString m_serviceError;
};
}
