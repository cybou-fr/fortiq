#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace fortiq {

// One model of "something the user asked a server to do" for every app
// (ARCHITECTURE §4): transfers, file operations, saves, searches. It only
// tracks state; the app that owns the work performs it and reports here.
//
//   Queued ──start──▶ Running ──┬─▶ Succeeded
//     │                 │       ├─▶ Failed
//     │                 │       ├─▶ Unknown   (a mutation may have happened; never retried blindly)
//     │                 │       ├─▶ Cancelled
//     │                 ▼       │
//     │            Cancelling ──┴─▶ (any terminal state: the work decides what really happened)
//     └─▶ Cancelled / Failed (before it ever ran)
//
// Terminal states are final. A cancel request is not a guarantee: it only
// asks the owner to stop (signal cancelRequested); tasks that cannot be
// stopped are not cancellable. A task that needs the user (for example a name
// conflict) goes back to Queued with a `decision` set until it is resolved.
class TaskManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY tasksChanged)
    Q_PROPERTY(int count READ count NOTIFY tasksChanged)

public:
    enum class State { Queued, Running, Cancelling, Cancelled, Succeeded, Failed, Unknown };
    Q_ENUM(State)

    struct Spec {
        QString kind;          // "upload", "download", "createFolder", "save", "search", ...
        QString title;         // what the user sees: file or folder name, search text
        QString source;        // server label ("user@host") or local origin
        QString destination;   // path / folder the action targets
        bool cancellable = true;
        int generation = 0;    // server session the task is bound to (never redirected)
        qint64 total = -1;     // bytes or items; -1 = indeterminate
    };

    explicit TaskManager(QObject *parent = nullptr);

    // Creates a Queued task and returns its id (> 0, unique for the process).
    int create(const Spec &spec);

    // --- owner reports; each returns false when the transition is not allowed ---
    bool start(int id);                                        // Queued (no pending decision) -> Running
    bool setProgress(int id, qint64 done, qint64 total);       // Running / Cancelling only
    bool setMessage(int id, const QString &message);           // not terminal
    bool requestDecision(int id, const QString &kind, const QString &message); // Running -> Queued, waiting for the user
    bool resolveDecision(int id);                              // clears the decision; stays Queued, progress reset
    bool succeed(int id, const QString &message, qint64 bytes = -1);  // Running / Cancelling
    bool fail(int id, const QString &message, const QString &code = {}); // Queued / Running / Cancelling
    bool markUnknown(int id, const QString &message);          // Running / Cancelling
    bool markCancelled(int id, const QString &message);        // any non-terminal state

    // --- user / UI ---
    // Queued: cancelled at once. Running + cancellable: Cancelling and cancelRequested(id)
    // is emitted so the owner stops the work. Otherwise refused (false).
    Q_INVOKABLE bool requestCancel(int id);
    // Removes finished (terminal) tasks from the list.
    Q_INVOKABLE void clearFinished();

    // --- queries ---
    Q_INVOKABLE QVariantMap task(int id) const;   // empty map when unknown
    std::optional<State> state(int id) const;     // nullopt for an unknown id
    bool exists(int id) const;
    QList<int> activeIds() const;                 // non-terminal tasks
    QList<int> activeIds(int generation) const;   // ... of one server session
    QVariantList tasks() const;
    int activeCount() const;
    int count() const { return m_tasks.size(); }

    static QString stateName(State state);
    static bool isTerminal(State state) {
        return state == State::Cancelled || state == State::Succeeded || state == State::Failed || state == State::Unknown;
    }

signals:
    void tasksChanged();
    void cancelRequested(int id);
    void finished(int id, const QString &state);   // once per task, when it reaches a terminal state

private:
    struct Task {
        int id = 0;
        Spec spec;
        State state = State::Queued;
        qint64 done = 0;
        qint64 total = -1;
        QString message;
        QString code;
        QString decision;      // "" or e.g. "conflict"
        qint64 createdMs = 0;
        qint64 finishedMs = 0;
    };
    Task *find(int id);
    const Task *find(int id) const;
    bool finish(Task &t, State state, const QString &message, const QString &code);
    static QVariantMap toMap(const Task &t);

    QList<Task> m_tasks;
    int m_nextId = 1;
};

}  // namespace fortiq
