#include "MailController.h"

#include "../core/RemoteController.h"
#include "../ssh/SshClient.h"

#include <QFutureWatcher>
#include <QSettings>
#include <QtConcurrentRun>

namespace fortiq {

namespace {
constexpr int kPageSize = 100;
constexpr qint64 kMaxSearchOutput = 2 * 1024 * 1024;
constexpr qint64 kMaxThreadOutput = 4 * 1024 * 1024;
const QStringList kEditableTags { QStringLiteral("unread"), QStringLiteral("flagged"), QStringLiteral("inbox"), QStringLiteral("deleted") };
}  // namespace

struct MailController::Outcome {
    SshResult run;
    QString openError;
    bool cancelled = false;
    bool parsedOk = false;
    MailOverview overview;
    QList<MailThread> threads;
    QList<MailMessage> messages;
};

MailController::MailController(RemoteController *remote, QObject *parent)
    : QObject(parent), m_remote(remote), m_session(std::make_shared<std::unique_ptr<SshSession>>()) {
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
    QSettings s;
    m_configPath = s.value("mail/notmuchConfig").toString();
    m_markRead = s.value("mail/markReadOnOpen", true).toBool();
    m_state = QStringLiteral("disconnected");
    m_status = QStringLiteral("Aucun serveur connecté");
    connect(remote, &RemoteController::changed, this, &MailController::onRemoteChanged);
}

MailController::~MailController() {
    if (m_cancel) m_cancel->store(true);
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_pool.waitForDone();
}

bool MailController::connected() const { return m_remote->connected(); }

void MailController::setMarkReadOnOpen(bool on) {
    m_markRead = on;
    QSettings{}.setValue("mail/markReadOnOpen", on);
    emit changed();
}

void MailController::setConfigPath(const QString &path) {
    m_configPath = path.trimmed();
    QSettings{}.setValue("mail/notmuchConfig", m_configPath);
    emit changed();
    refresh();
}

void MailController::resetAll(const QString &state, const QString &status) {
    m_queue.clear();
    m_running = false;
    m_busy = m_threadBusy = false;
    m_folders.clear(); m_threads.clear(); m_threadsVariant.clear(); m_messagesVariant.clear();
    m_selectedThread.clear(); m_threadStatus.clear(); m_version.clear(); m_query.clear();
    m_folderKey = QStringLiteral("inbox");
    m_hasMore = false; m_offset = 0;
    m_state = state;
    m_status = status;
    emit changed(); emit threadsChanged(); emit threadChanged();
}

void MailController::onRemoteChanged() {
    if (m_remote->connected() && m_remote->connectionToken() == m_token) return;
    if (m_cancel) m_cancel->store(true);
    ++m_seq;
    m_token = m_remote->connected() ? m_remote->connectionToken() : -1;
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_session = std::make_shared<std::unique_ptr<SshSession>>();
    resetAll(QStringLiteral("disconnected"), QStringLiteral("Source déconnectée ou changée."));
    if (m_active && m_remote->connected()) refresh();
}

void MailController::setWindowActive(bool active) {
    const bool opening = active && !m_active;
    m_active = active;
    if (opening && m_remote->connected() && m_state != QStringLiteral("ready")) refresh();
}

void MailController::refresh() {
    if (!m_remote->connected()) return;
    m_token = m_remote->connectionToken();
    if (m_state != QStringLiteral("ready")) { m_state = QStringLiteral("loading"); m_status = QStringLiteral("Lecture de la base notmuch…"); emit changed(); }
    Job overview { Kind::Overview, buildMailOverviewScript(m_configPath), {}, {}, {}, m_seq, 0 };
    enqueue(overview);
    startSearch(m_query, m_folderKey);
}

void MailController::openFolder(const QString &key) {
    startSearch(QString(), key);
}

void MailController::search(const QString &text) {
    startSearch(text.trimmed(), m_folderKey);
}

void MailController::startSearch(const QString &text, const QString &folderKey) {
    if (!m_remote->connected()) return;
    QString folderQuery = QStringLiteral("*");
    for (const QVariant &f : m_folders)
        if (f.toMap().value("key").toString() == folderKey) folderQuery = f.toMap().value("query").toString();
    if (m_folders.isEmpty()) {   // first load, before the overview arrived
        static const QHash<QString, QString> defaults { { "inbox", "tag:inbox" }, { "unread", "tag:unread" }, { "flagged", "tag:flagged" }, { "sent", "tag:sent" }, { "deleted", "tag:deleted" }, { "all", "*" } };
        folderQuery = defaults.value(folderKey, QStringLiteral("*"));
    }
    QString effective = folderQuery;
    if (!text.isEmpty()) effective = folderQuery == QLatin1String("*") ? text : QStringLiteral("(%1) and (%2)").arg(folderQuery, text);
    const QString invalid = validateMailQuery(effective);
    m_folderKey = folderKey;
    m_query = text;
    if (!invalid.isEmpty()) { m_status = invalid; emit changed(); return; }
    m_offset = 0;
    m_hasMore = false;
    m_threads.clear(); m_threadsVariant.clear();
    ++m_searchGen;
    emit threadsChanged();
    emit changed();
    Job job { Kind::Search, buildMailSearchScript(effective, kPageSize, 0, m_configPath), effective, {}, {}, m_seq, m_searchGen };
    enqueue(job);
}

void MailController::loadMore() {
    if (!m_hasMore || m_busy || !m_remote->connected()) return;
    QString folderQuery = QStringLiteral("*");
    for (const QVariant &f : m_folders)
        if (f.toMap().value("key").toString() == m_folderKey) folderQuery = f.toMap().value("query").toString();
    QString effective = folderQuery;
    if (!m_query.isEmpty()) effective = folderQuery == QLatin1String("*") ? m_query : QStringLiteral("(%1) and (%2)").arg(folderQuery, m_query);
    Job job { Kind::More, buildMailSearchScript(effective, kPageSize, m_offset, m_configPath), effective, {}, {}, m_seq, m_searchGen };
    enqueue(job);
}

void MailController::openThread(const QString &threadId) {
    if (!m_remote->connected() || !validMailThreadId(threadId)) return;
    m_selectedThread = threadId;
    m_messagesVariant.clear();
    m_threadStatus = QStringLiteral("Lecture du message…");
    ++m_threadGen;
    emit threadChanged();
    enqueue(Job { Kind::Thread, buildMailThreadScript(threadId, m_configPath), threadId, {}, {}, m_seq, m_threadGen });
}

void MailController::closeThread() {
    ++m_threadGen;
    m_selectedThread.clear(); m_messagesVariant.clear(); m_threadStatus.clear();
    emit threadChanged();
}

void MailController::setTag(const QString &threadId, const QString &tag, bool on) {
    if (!m_remote->connected() || !kEditableTags.contains(tag) || !validMailThreadId(threadId)) return;
    const QStringList add = on ? QStringList { tag } : QStringList(), remove = on ? QStringList() : QStringList { tag };
    enqueue(Job { Kind::Tag, buildMailTagScript(threadId, add, remove, m_configPath), threadId, add, remove, m_seq, 0 });
}

void MailController::enqueue(const Job &job) {
    if (job.script.isEmpty()) return;
    if (job.kind == Kind::Search) m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [](const Job &j) { return j.kind == Kind::Search || j.kind == Kind::More; }), m_queue.end());
    if (job.kind == Kind::Thread) m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [](const Job &j) { return j.kind == Kind::Thread; }), m_queue.end());
    if (job.kind == Kind::Overview) m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [](const Job &j) { return j.kind == Kind::Overview; }), m_queue.end());
    m_queue.push_back(job);
    pump();
}

void MailController::pump() {
    if (m_running || m_queue.isEmpty() || !m_remote->connected()) return;
    const Job job = m_queue.takeFirst();
    if (job.seq != m_seq) { pump(); return; }
    m_running = true;
    if (job.kind == Kind::Thread) m_threadBusy = true; else m_busy = true;
    emit changed(); emit threadChanged();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_cancel;
    auto slot = m_session;
    const ConnectionRequest request = m_remote->connectionRequest();
    auto *watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, job] {
        const Outcome out = watcher->result();
        watcher->deleteLater();
        if (job.seq != m_seq || !m_remote->connected()) return;   // session changed: resetAll already cleared the state
        m_running = false;
        finish(job, out);
        pump();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, request, job, cancel] {
        Outcome out;
        if (cancel->load()) { out.cancelled = true; return out; }
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(request);   // host pin re-checked
            if (opened.status != SshResult::Status::Ok) { out.openError = opened.message; return out; }
        }
        const qint64 cap = job.kind == Kind::Thread ? kMaxThreadOutput : kMaxSearchOutput;
        out.run = session.runScript(job.script, cap, [cancel] { return !cancel->load(); });
        if (out.run.status != SshResult::Status::Ok || out.run.truncated || out.run.code != QLatin1String("exit:0")) return out;
        switch (job.kind) {
        case Kind::Overview: out.overview = parseMailOverview(out.run.data); out.parsedOk = out.overview.ok; break;
        case Kind::Search: case Kind::More: out.threads = parseMailSearch(out.run.data, &out.parsedOk); break;
        case Kind::Thread: out.messages = parseMailThread(out.run.data, &out.parsedOk); break;
        case Kind::Tag: out.parsedOk = true; break;
        }
        return out;
    }));
}

namespace {
QString failureText(const SshResult &run, const QString &openError) {
    if (!openError.isEmpty()) return openError;
    if (run.status != SshResult::Status::Ok) return run.message.isEmpty() ? QStringLiteral("Connexion SSH perdue.") : run.message;
    if (run.truncated) return QStringLiteral("Réponse trop grande (limite dépassée).");
    const int exitCode = run.code.startsWith(QLatin1String("exit:")) ? run.code.mid(5).toInt() : -1;
    return describeMailFailure(exitCode, run.message);
}
}  // namespace

void MailController::finish(const Job &job, const Outcome &out) {
    const bool failed = out.cancelled || !out.openError.isEmpty() || out.run.status != SshResult::Status::Ok || out.run.truncated || out.run.code != QLatin1String("exit:0") || !out.parsedOk;
    if (job.kind == Kind::Thread) m_threadBusy = false; else m_busy = false;
    const bool runFailed = !out.openError.isEmpty() || out.run.status != SshResult::Status::Ok || out.run.truncated || out.run.code != QLatin1String("exit:0");
    const QString problem = out.cancelled ? QString() : (runFailed ? failureText(out.run, out.openError) : (out.parsedOk ? QString() : QStringLiteral("Réponse de notmuch illisible.")));
    switch (job.kind) {
    case Kind::Overview:
        if (failed) { m_state = QStringLiteral("error"); m_status = out.overview.problem.isEmpty() ? problem : out.overview.problem; }
        else { m_folders = out.overview.folders; m_version = out.overview.version; m_state = QStringLiteral("ready"); m_status = m_version; }
        emit changed();
        break;
    case Kind::Search: case Kind::More: {
        if (job.gen != m_searchGen) break;   // an older search: a newer one is queued or running
        if (failed) {
            if (m_state != QStringLiteral("error")) m_status = problem;
            if (job.kind == Kind::Search) { m_state = QStringLiteral("error"); }
            emit changed();
            break;
        }
        if (job.kind == Kind::Search) m_threads.clear();
        m_threads += out.threads;
        m_offset += static_cast<int>(out.threads.size());
        m_hasMore = out.threads.size() >= kPageSize;
        m_threadsVariant = threadsToVariant(m_threads);
        if (m_state == QStringLiteral("loading") || m_state == QStringLiteral("error")) m_state = QStringLiteral("ready");
        m_status = m_threads.isEmpty() ? QStringLiteral("Aucun message.") : m_version;
        emit threadsChanged(); emit changed();
        break;
    }
    case Kind::Thread: {
        if (job.gen != m_threadGen || job.key != m_selectedThread) break;
        if (failed) { m_threadStatus = problem; emit threadChanged(); break; }
        m_messagesVariant = messagesToVariant(out.messages);
        m_threadStatus = out.messages.isEmpty() ? QStringLiteral("Fil vide.") : QString();
        emit threadChanged();
        bool anyUnread = false;
        for (const MailMessage &m : out.messages) anyUnread |= m.tags.contains(QLatin1String("unread"));
        if (m_markRead && anyUnread) setTag(job.key, QStringLiteral("unread"), false);
        break;
    }
    case Kind::Tag:
        if (failed) { m_status = QStringLiteral("Étiquette non modifiée : ") + problem; emit changed(); break; }
        applyLocalTags(job.key, job.add, job.remove);
        enqueue(Job { Kind::Overview, buildMailOverviewScript(m_configPath), {}, {}, {}, m_seq, 0 });
        break;
    }
}

void MailController::applyLocalTags(const QString &threadId, const QStringList &add, const QStringList &remove) {
    auto patch = [&](QStringList tags) { for (const QString &t : remove) tags.removeAll(t); for (const QString &t : add) if (!tags.contains(t)) tags << t; tags.sort(); return tags; };
    for (int i = 0; i < m_threads.size(); ++i) {
        if (m_threads[i].id != threadId) continue;
        m_threads[i].tags = patch(m_threads[i].tags);
        m_threads[i].unread = m_threads[i].tags.contains(QLatin1String("unread"));
        m_threads[i].flagged = m_threads[i].tags.contains(QLatin1String("flagged"));
        // Archiving from the inbox or restoring from the trash removes the row from the current folder.
        if ((m_folderKey == QLatin1String("inbox") && remove.contains(QLatin1String("inbox"))) || (m_folderKey == QLatin1String("deleted") && remove.contains(QLatin1String("deleted"))) ||
            (m_folderKey == QLatin1String("flagged") && remove.contains(QLatin1String("flagged")))) {
            m_threads.removeAt(i);
            if (m_selectedThread == threadId) closeThread();
        }
        break;
    }
    m_threadsVariant = threadsToVariant(m_threads);
    if (m_selectedThread == threadId) {
        for (QVariant &v : m_messagesVariant) {
            QVariantMap m = v.toMap();
            QStringList tags = patch(m.value("tags").toStringList());
            m.insert("tags", tags);
            m.insert("unread", tags.contains(QLatin1String("unread")));
            v = m;
        }
        emit threadChanged();
    }
    emit threadsChanged();
}

}  // namespace fortiq
