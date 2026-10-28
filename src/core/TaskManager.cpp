#include "TaskManager.h"

#include <QDateTime>

namespace fortiq {

TaskManager::TaskManager(QObject *parent) : QObject(parent) {}

TaskManager::Task *TaskManager::find(int id) {
    for (Task &t : m_tasks) if (t.id == id) return &t;
    return nullptr;
}
const TaskManager::Task *TaskManager::find(int id) const {
    for (const Task &t : m_tasks) if (t.id == id) return &t;
    return nullptr;
}

QString TaskManager::stateName(State state) {
    switch (state) {
    case State::Queued: return QStringLiteral("queued");
    case State::Running: return QStringLiteral("running");
    case State::Cancelling: return QStringLiteral("cancelling");
    case State::Cancelled: return QStringLiteral("cancelled");
    case State::Succeeded: return QStringLiteral("succeeded");
    case State::Failed: return QStringLiteral("failed");
    case State::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

int TaskManager::create(const Spec &spec) {
    Task t;
    t.id = m_nextId++;
    t.spec = spec;
    t.total = spec.total;
    t.createdMs = QDateTime::currentMSecsSinceEpoch();
    m_tasks.push_back(t);
    emit tasksChanged();
    return t.id;
}

int TaskManager::begin(const Spec &spec) {
    const int id = create(spec);
    start(id);
    return id;
}

bool TaskManager::start(int id) {
    Task *t = find(id);
    if (!t || t->state != State::Queued || !t->decision.isEmpty()) return false;
    t->state = State::Running;
    emit tasksChanged();
    return true;
}

bool TaskManager::setProgress(int id, qint64 done, qint64 total) {
    Task *t = find(id);
    if (!t || (t->state != State::Running && t->state != State::Cancelling)) return false;
    if (t->done == done && t->total == total) return true;
    t->done = done;
    t->total = total;
    emit tasksChanged();
    return true;
}

bool TaskManager::setMessage(int id, const QString &message) {
    Task *t = find(id);
    if (!t || isTerminal(t->state)) return false;
    if (t->message == message) return true;
    t->message = message;
    emit tasksChanged();
    return true;
}

bool TaskManager::requestDecision(int id, const QString &kind, const QString &message) {
    Task *t = find(id);
    if (!t || t->state != State::Running || kind.isEmpty()) return false;
    t->state = State::Queued;
    t->decision = kind;
    t->message = message;
    emit tasksChanged();
    return true;
}

bool TaskManager::resolveDecision(int id) {
    Task *t = find(id);
    if (!t || t->state != State::Queued || t->decision.isEmpty()) return false;
    t->decision.clear();
    t->message.clear();
    t->done = 0;
    emit tasksChanged();
    return true;
}

bool TaskManager::finish(Task &t, State state, const QString &message, const QString &code) {
    t.state = state;
    t.message = message;
    t.code = code;
    t.decision.clear();
    t.finishedMs = QDateTime::currentMSecsSinceEpoch();
    const int id = t.id;
    const bool vanish = t.spec.transient && (state == State::Succeeded || state == State::Cancelled);
    emit tasksChanged();
    emit finished(id, stateName(state));
    if (vanish) {
        m_tasks.removeIf([id](const Task &x) { return x.id == id; });   // `t` is gone after this line
        emit tasksChanged();
    }
    return true;
}

bool TaskManager::succeed(int id, const QString &message, qint64 bytes) {
    Task *t = find(id);
    if (!t || (t->state != State::Running && t->state != State::Cancelling)) return false;
    if (bytes >= 0) { t->done = bytes; t->total = bytes; }
    return finish(*t, State::Succeeded, message, {});
}

bool TaskManager::fail(int id, const QString &message, const QString &code) {
    Task *t = find(id);
    if (!t || isTerminal(t->state)) return false;
    return finish(*t, State::Failed, message, code);
}

bool TaskManager::markUnknown(int id, const QString &message) {
    Task *t = find(id);
    // A queued task may already be executing remotely before its asynchronous
    // start notification reaches the GUI. Unknown must remain representable.
    if (!t || isTerminal(t->state)) return false;
    return finish(*t, State::Unknown, message, QStringLiteral("unknown"));
}

bool TaskManager::markCancelled(int id, const QString &message) {
    Task *t = find(id);
    if (!t || isTerminal(t->state)) return false;
    return finish(*t, State::Cancelled, message, QStringLiteral("cancelled"));
}

bool TaskManager::requestCancel(int id) {
    Task *t = find(id);
    if (!t || isTerminal(t->state) || t->state == State::Cancelling) return false;
    // Non-cancellable remote mutations must never be reported as safely
    // cancelled merely because their queued start signal has not arrived.
    if (!t->spec.cancellable) return false;
    if (t->state == State::Queued) {
        // Cancellable queued jobs are stopped through their owning controller.
        emit cancelRequested(id);
        markCancelled(id, QStringLiteral("Annulé."));
        return true;
    }
    t->state = State::Cancelling;
    emit tasksChanged();
    emit cancelRequested(id);
    return true;
}

void TaskManager::clearFinished() {
    const qsizetype before = m_tasks.size();
    m_tasks.removeIf([](const Task &t) { return isTerminal(t.state); });
    if (m_tasks.size() != before) emit tasksChanged();
}

bool TaskManager::exists(int id) const { return find(id) != nullptr; }

std::optional<TaskManager::State> TaskManager::state(int id) const {
    const Task *t = find(id);
    if (!t) return std::nullopt;
    return t->state;
}

QList<int> TaskManager::activeIds() const {
    QList<int> out;
    for (const Task &t : m_tasks) if (!isTerminal(t.state)) out << t.id;
    return out;
}
QList<int> TaskManager::activeIds(int generation) const {
    QList<int> out;
    for (const Task &t : m_tasks) if (!isTerminal(t.state) && t.spec.generation == generation) out << t.id;
    return out;
}

int TaskManager::activeCount() const {
    int n = 0;
    for (const Task &t : m_tasks) n += !isTerminal(t.state);
    return n;
}

QVariantMap TaskManager::toMap(const Task &t) {
    return QVariantMap {
        { "id", t.id }, { "kind", t.spec.kind }, { "title", t.spec.title }, { "source", t.spec.source },
        { "destination", t.spec.destination }, { "state", stateName(t.state) },
        { "active", !isTerminal(t.state) }, { "cancellable", t.spec.cancellable },
        { "decision", t.decision }, { "message", t.message }, { "code", t.code },
        { "done", static_cast<double>(t.done) }, { "total", static_cast<double>(t.total) },
        { "generation", t.spec.generation }, { "guards", t.spec.guards }, { "session", t.spec.session }, { "transient", t.spec.transient },
        { "created", static_cast<double>(t.createdMs) }, { "finished", static_cast<double>(t.finishedMs) } };
}

QVariantMap TaskManager::task(int id) const {
    const Task *t = find(id);
    return t ? toMap(*t) : QVariantMap();
}

QVariantList TaskManager::tasks() const {
    QVariantList out;
    for (const Task &t : m_tasks) out.push_back(toMap(t));
    return out;
}

QVariantList TaskManager::guardingTasks() const {
    QVariantList out;
    for (const Task &t : m_tasks) if (!isTerminal(t.state) && t.spec.guards) out.push_back(toMap(t));
    return out;
}

int TaskManager::guardingCount() const {
    int n = 0;
    for (const Task &t : m_tasks) n += !isTerminal(t.state) && t.spec.guards;
    return n;
}

}  // namespace fortiq
