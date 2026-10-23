#include "MailController.h"

#include "../core/RemoteController.h"
#include "../core/TaskManager.h"
#include "../ssh/SshClient.h"

#include <QFutureWatcher>
#include <QRandomGenerator>
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
    QList<MailSendAccount> accounts;
};

struct MailController::ActionOutcome {
    SshResult run;
    QString openError;
    bool cancelled = false;
};

MailController::MailController(RemoteController *remote, QObject *parent)
    : QObject(parent), m_remote(remote), m_session(std::make_shared<std::unique_ptr<SshSession>>()) {
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
    m_actionPool.setMaxThreadCount(1);
    m_actionPool.setExpiryTimeout(-1);
    m_actionSession = std::make_shared<std::unique_ptr<SshSession>>();
    QSettings s;
    m_displayName = s.value("mail/displayName").toString();
    m_configPath = s.value("mail/notmuchConfig").toString();
    m_markRead = s.value("mail/markReadOnOpen", true).toBool();
    m_state = QStringLiteral("disconnected");
    m_status = QStringLiteral("Aucun serveur connecté");
    connect(remote, &RemoteController::changed, this, &MailController::onRemoteChanged);
    if (remote->tasks())
        connect(remote->tasks(), &TaskManager::cancelRequested, this, [this](int id) { if (id == m_actionTask && m_actionCancel) m_actionCancel->store(true); });
}

MailController::~MailController() {
    if (m_cancel) m_cancel->store(true);
    if (m_actionCancel) m_actionCancel->store(true);
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    auto actionSlot = m_actionSession;
    (void)QtConcurrent::run(&m_actionPool, [actionSlot] { actionSlot->reset(); });
    m_pool.waitForDone();
    m_actionPool.waitForDone();
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
    m_accounts.clear(); m_accountsVariant.clear();
    m_previewFiles.clear(); m_previewAccount.clear(); m_setupState = MailSetupState(); m_setupStateVariant.clear();
    m_actionBusy = false; m_sendState = QStringLiteral("idle"); m_sendStatus.clear(); m_actionStatus.clear(); m_actionOutput.clear();
    m_state = state;
    m_status = status;
    emit changed(); emit threadsChanged(); emit threadChanged(); emit accountsChanged(); emit sendChanged(); emit actionChanged(); emit setupChanged();
}

void MailController::onRemoteChanged() {
    if (m_remote->connected() && m_remote->connectionToken() == m_token) return;
    if (m_cancel) m_cancel->store(true);
    if (m_actionCancel) m_actionCancel->store(true);
    ++m_seq; ++m_actionSeq;
    auto actionSlot = m_actionSession;
    (void)QtConcurrent::run(&m_actionPool, [actionSlot] { actionSlot->reset(); });
    m_actionSession = std::make_shared<std::unique_ptr<SshSession>>();
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
    enqueue(Job { Kind::Accounts, buildMailAccountsScript(), {}, {}, {}, m_seq, 0 });
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
        case Kind::Accounts: out.accounts = parseMailAccounts(out.run.data); out.parsedOk = true; break;
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
    case Kind::Accounts:
        if (!failed) {
            m_accounts = out.accounts;
            m_accountsVariant = sendAccountsToVariant(m_accounts);
            emit accountsChanged();
        }
        break;
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

// ---------------------------------------------------------------- sending and account wizard
namespace {
QString randomHex(int bytes) {
    QString out;
    for (int i = 0; i < bytes; ++i) out += QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(256), 2, 16, QLatin1Char('0'));
    return out;
}

QString runResultText(const SshResult &run) {
    QString text = QString::fromUtf8(run.data).trimmed();
    if (run.truncated) text += QStringLiteral("\n[sortie tronquée]");
    if (text.isEmpty()) text = run.message.trimmed();
    return text;
}

int exitCodeOf(const SshResult &run) {
    return run.code.startsWith(QLatin1String("exit:")) ? run.code.mid(5).toInt() : -1;
}
}  // namespace

void MailController::setDisplayName(const QString &name) {
    m_displayName = name.trimmed().left(100);
    QSettings{}.setValue("mail/displayName", m_displayName);
    emit accountsChanged();
}

void MailController::setActionStatus(const QString &status, const QString &output) {
    m_actionStatus = status;
    m_actionOutput = output;
    emit actionChanged();
}

void MailController::runAction(const QString &kind, const QString &title, const QString &destination, bool cancellable, const QByteArray &script, qint64 maxOutput,
                               std::function<void(const ActionOutcome &, int)> done) {
    if (m_actionBusy || !m_remote->connected() || script.isEmpty()) return;
    const int token = m_remote->connectionToken(), seq = m_actionSeq;
    m_actionBusy = true;
    m_actionCancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_actionCancel;
    int taskId = 0;
    if (TaskManager *tasks = m_remote->tasks()) {
        TaskManager::Spec spec;
        spec.kind = kind;
        spec.title = title;
        spec.source = m_remote->serverLabel();
        spec.destination = destination;
        spec.cancellable = cancellable;
        spec.generation = token;
        taskId = m_actionTask = tasks->create(spec);
        tasks->start(taskId);
    }
    emit actionChanged();
    auto slot = m_actionSession;
    const ConnectionRequest request = m_remote->connectionRequest();
    auto *watcher = new QFutureWatcher<ActionOutcome>(this);
    connect(watcher, &QFutureWatcher<ActionOutcome>::finished, this, [this, watcher, seq, token, taskId, done] {
        const ActionOutcome out = watcher->result();
        watcher->deleteLater();
        if (seq != m_actionSeq || !m_remote->connected() || token != m_remote->connectionToken()) return;   // the session changed: the task manager already settled the task
        m_actionBusy = false;
        done(out, taskId);
        emit actionChanged();
    });
    watcher->setFuture(QtConcurrent::run(&m_actionPool, [slot, request, script, cancel, maxOutput] {
        ActionOutcome out;
        if (cancel->load()) { out.cancelled = true; return out; }
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(request);   // host pin re-checked
            if (opened.status != SshResult::Status::Ok) { out.openError = opened.message; return out; }
        }
        out.run = session.runScript(script, maxOutput, [cancel] { return !cancel->load(); });
        return out;
    }));
}

QVariantMap MailController::replyDraft(const QVariantMap &message) const {
    QVariantMap d;
    QList<MailAddress> from;
    QString err;
    parseMailAddresses(message.value("from").toString(), &from, &err);
    d.insert("to", from.isEmpty() ? message.value("from").toString() : formatMailAddress(MailAddress { from.first().name, from.first().addr }));
    d.insert("subject", replySubject(message.value("subject").toString()));
    const QString text = message.value("hasText").toBool() ? message.value("text").toString() : message.value("htmlPlain").toString();
    d.insert("body", replyBody(message.value("from").toString(), message.value("date").toString(), text));
    d.insert("inReplyTo", message.value("id").toString());
    return d;
}

void MailController::resetSend() {
    if (m_actionBusy) return;
    m_sendState = QStringLiteral("idle");
    m_sendStatus.clear();
    emit sendChanged();
}

QString MailController::send(const QVariantMap &draftMap) {
    if (!m_remote->connected()) return QStringLiteral("Aucun serveur connecté.");
    if (m_actionBusy) return QStringLiteral("Une autre action est en cours sur le serveur.");
    MailDraft d;
    d.account = draftMap.value("account").toString();
    for (const MailSendAccount &a : m_accounts) if (a.name == d.account) d.fromAddress = a.from;
    if (d.fromAddress.isEmpty()) return QStringLiteral("Choisissez un compte d'envoi (ouvrez « Comptes » pour en créer un).");
    d.fromName = m_displayName;
    d.to = draftMap.value("to").toString();
    d.cc = draftMap.value("cc").toString();
    d.bcc = draftMap.value("bcc").toString();
    d.subject = draftMap.value("subject").toString();
    d.body = draftMap.value("body").toString();
    d.inReplyTo = draftMap.value("inReplyTo").toString();
    const BuiltMessage message = buildMessage(d, QDateTime::currentDateTime(), randomHex(12));
    if (!message.ok) return message.error;
    QByteArray script;
    for (int attempt = 0; attempt < 3 && script.isEmpty(); ++attempt)
        script = buildMailSendScript(d.account, message.recipients, message.data, d.inReplyTo, m_configPath, QStringLiteral("FQMSG") + randomHex(10));
    if (script.isEmpty()) return QStringLiteral("Le message n'a pas pu être préparé.");
    m_sendState = QStringLiteral("sending");
    m_sendStatus = QStringLiteral("Envoi en cours…");
    emit sendChanged();
    const QString title = QStringLiteral("Envoi · %1 destinataire(s)").arg(message.recipients.size());
    runAction(QStringLiteral("mailSend"), title, d.account, false, script, 64 * 1024, [this](const ActionOutcome &out, int taskId) {
        TaskManager *tasks = m_remote->tasks();
        if (!out.openError.isEmpty() || out.run.status != SshResult::Status::Ok) {
            // The connection broke somewhere around delivery: do not claim either outcome.
            const bool neverStarted = !out.openError.isEmpty();
            const QString text = neverStarted ? out.openError : QStringLiteral("Connexion SSH perdue pendant l'envoi : on ne sait pas si le message est parti.");
            m_sendState = neverStarted ? QStringLiteral("failed") : QStringLiteral("unknown");
            m_sendStatus = text;
            if (tasks && taskId) { if (neverStarted) tasks->fail(taskId, text); else tasks->markUnknown(taskId, text); }
            emit sendChanged();
            return;
        }
        const int exitCode = exitCodeOf(out.run);
        bool delivered = false, unknown = false;
        const QString text = describeSendOutcome(exitCode, out.run.message, &delivered, &unknown);
        m_sendStatus = text;
        if (delivered) {
            m_sendState = exitCode == 0 ? QStringLiteral("sent") : QStringLiteral("warning");
            if (tasks && taskId) tasks->succeed(taskId, text);
            refresh();   // the copy appears in Sent
        } else if (unknown) {
            m_sendState = QStringLiteral("unknown");
            if (tasks && taskId) tasks->markUnknown(taskId, text);
        } else {
            m_sendState = QStringLiteral("failed");
            if (tasks && taskId) tasks->fail(taskId, text);
        }
        emit sendChanged();
    });
    return {};
}

void MailController::probeSetup(const QString &accountName) {
    if (!m_remote->connected()) return;
    const QByteArray script = buildSetupProbeScript(accountName, m_configPath);
    if (script.isEmpty()) return;
    runAction(QStringLiteral("mailProbe"), QStringLiteral("Lecture de la configuration"), accountName, false, script, 64 * 1024, [this](const ActionOutcome &out, int taskId) {
        TaskManager *tasks = m_remote->tasks();
        if (!out.openError.isEmpty() || out.run.status != SshResult::Status::Ok || out.run.code != QLatin1String("exit:0")) {
            const QString text = !out.openError.isEmpty() ? out.openError : describeActionFailure(QStringLiteral("Lecture de la configuration"), exitCodeOf(out.run), out.run.message);
            m_setupState = MailSetupState();
            m_setupState.problem = text;
            m_setupStateVariant = setupStateToVariant(m_setupState);
            setActionStatus(text);
            if (tasks && taskId) tasks->fail(taskId, text);
        } else {
            m_setupState = parseSetupProbe(out.run.data);
            m_setupStateVariant = setupStateToVariant(m_setupState);
            if (!m_setupState.ok) setActionStatus(m_setupState.problem);   // a successful probe keeps the status of the action before it
            if (tasks && taskId) { if (m_setupState.ok) tasks->succeed(taskId, QStringLiteral("Configuration lue.")); else tasks->fail(taskId, m_setupState.problem); }
        }
        emit setupChanged();
    });
}

QString MailController::previewSetup(const QVariantMap &specMap) {
    if (!m_remote->connected()) return QStringLiteral("Aucun serveur connecté.");
    if (!m_setupState.ok) return m_setupState.problem.isEmpty() ? QStringLiteral("La configuration du serveur n'a pas encore été lue.") : m_setupState.problem;
    const MailAccountSpec spec = accountSpecFromVariant(specMap);
    const QString invalid = validateAccountSpec(spec);
    if (!invalid.isEmpty()) return invalid;
    QStringList missing;
    if (m_setupState.mbsyncVersion.isEmpty()) missing << QStringLiteral("mbsync (paquet isync)");
    if (m_setupState.msmtpVersion.isEmpty()) missing << QStringLiteral("msmtp");
    if (!missing.isEmpty())
        return QStringLiteral("Outils manquants sur le serveur : %1. Installez-les d'abord (sudo apt install isync msmtp notmuch).").arg(missing.join(QStringLiteral(", ")));
    m_previewFiles = buildSetupFiles(spec, m_setupState.home, m_setupState.mailRoot(), m_setupState.hasNotmuchConfig);
    m_previewAccount = spec.name;
    if (!spec.displayName.isEmpty()) setDisplayName(spec.displayName);
    emit setupChanged();
    return {};
}

void MailController::clearSetup() {
    m_previewFiles.clear();
    m_previewAccount.clear();
    emit setupChanged();
}

void MailController::applySetup(const QStringList &keys) {
    if (m_actionBusy || m_previewFiles.isEmpty()) return;
    QList<MailSetupFile> chosen;
    for (const MailSetupFile &f : m_previewFiles) if (keys.contains(f.key)) chosen << f;
    if (chosen.isEmpty()) { setActionStatus(QStringLiteral("Cochez au moins un fichier à écrire.")); return; }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss"));
    const QByteArray script = buildSetupWriteScript(m_previewAccount, chosen, stamp, QStringLiteral("FQW") + randomHex(8) + QStringLiteral("X"));
    if (script.isEmpty()) { setActionStatus(QStringLiteral("L'écriture n'a pas pu être préparée.")); return; }
    const QString account = m_previewAccount;
    runAction(QStringLiteral("mailSetup"), QStringLiteral("Compte courrier · ") + account, account, false, script, 64 * 1024, [this, account, chosen](const ActionOutcome &out, int taskId) {
        TaskManager *tasks = m_remote->tasks();
        const int exitCode = exitCodeOf(out.run);
        const QStringList written = parseSetupWritten(out.run.data);
        if (!out.openError.isEmpty() || out.run.status != SshResult::Status::Ok || exitCode != 0) {
            const QString text = !out.openError.isEmpty() ? out.openError : describeActionFailure(QStringLiteral("Écriture"), exitCode, out.run.message);
            // Files before the failing one were written completely (each file is replaced atomically).
            setActionStatus(written.isEmpty() ? text : text + QStringLiteral(" Déjà écrits : ") + written.join(QStringLiteral(", ")) + QLatin1Char('.'));
            const bool certainlyNothing = written.isEmpty() && out.openError.isEmpty() && out.run.status == SshResult::Status::Ok;
            if (tasks && taskId) { if (certainlyNothing) tasks->fail(taskId, text); else tasks->markUnknown(taskId, text); }
            return;
        }
        QStringList paths;
        for (const MailSetupFile &f : chosen) paths << f.path;
        setActionStatus(QStringLiteral("Écrit sur le serveur (mode 0600) : ") + paths.join(QStringLiteral(", ")) + QStringLiteral(". Une sauvegarde horodatée a été gardée à côté de chaque fichier déjà existant."));
        if (tasks && taskId) tasks->succeed(taskId, QStringLiteral("Fichiers écrits."));
        m_previewFiles.clear();    // the password leaves the client's memory as soon as it is on the server
        emit setupChanged();
        probeSetup(account);
        enqueue(Job { Kind::Accounts, buildMailAccountsScript(), {}, {}, {}, m_seq, 0 });
    });
}

void MailController::testSmtp(const QString &account) {
    runAction(QStringLiteral("mailTest"), QStringLiteral("Test SMTP · ") + account, account, true, buildSmtpTestScript(account), 64 * 1024, [this](const ActionOutcome &out, int taskId) {
        TaskManager *tasks = m_remote->tasks();
        if (out.cancelled || out.run.code == QLatin1String("cancelled")) { setActionStatus(QStringLiteral("Test annulé.")); if (tasks && taskId) tasks->markCancelled(taskId, QStringLiteral("Annulé.")); return; }
        const int exitCode = exitCodeOf(out.run);
        if (!out.openError.isEmpty() || out.run.status != SshResult::Status::Ok || exitCode != 0) {
            const QString text = !out.openError.isEmpty() ? out.openError : describeActionFailure(QStringLiteral("Test SMTP"), exitCode, out.run.message + QLatin1Char('\n') + QString::fromUtf8(out.run.data));
            setActionStatus(text, QString::fromUtf8(out.run.data).trimmed());
            if (tasks && taskId) tasks->fail(taskId, text);
            return;
        }
        setActionStatus(QStringLiteral("Le serveur SMTP répond (connexion et TLS vérifiés ; l'authentification se vérifie à l'envoi)."), runResultText(out.run));
        if (tasks && taskId) tasks->succeed(taskId, QStringLiteral("SMTP joignable."));
    });
}

void MailController::testImap(const QString &account) {
    runAction(QStringLiteral("mailTest"), QStringLiteral("Test IMAP · ") + account, account, true, buildImapTestScript(account), 64 * 1024, [this](const ActionOutcome &out, int taskId) {
        TaskManager *tasks = m_remote->tasks();
        if (out.cancelled || out.run.code == QLatin1String("cancelled")) { setActionStatus(QStringLiteral("Test annulé.")); if (tasks && taskId) tasks->markCancelled(taskId, QStringLiteral("Annulé.")); return; }
        const int exitCode = exitCodeOf(out.run);
        if (!out.openError.isEmpty() || out.run.status != SshResult::Status::Ok || exitCode != 0) {
            const QString text = !out.openError.isEmpty() ? out.openError : describeActionFailure(QStringLiteral("Test IMAP"), exitCode, out.run.message + QLatin1Char('\n') + QString::fromUtf8(out.run.data));
            setActionStatus(text, QString::fromUtf8(out.run.data).trimmed());
            if (tasks && taskId) tasks->fail(taskId, text);
            return;
        }
        setActionStatus(QStringLiteral("Connexion IMAP réussie : dossiers distants listés ci-dessous."), runResultText(out.run));
        if (tasks && taskId) tasks->succeed(taskId, QStringLiteral("IMAP joignable."));
    });
}

void MailController::syncNow(const QString &account) {
    runAction(QStringLiteral("mailSync"), QStringLiteral("Relevé du courrier · ") + account, account, true, buildSyncScript(account, m_configPath), 256 * 1024, [this](const ActionOutcome &out, int taskId) {
        TaskManager *tasks = m_remote->tasks();
        if (out.cancelled || out.run.code == QLatin1String("cancelled")) {
            setActionStatus(QStringLiteral("Relevé annulé (les messages déjà reçus restent)."), runResultText(out.run));
            if (tasks && taskId) tasks->markCancelled(taskId, QStringLiteral("Annulé."));
            refresh();
            return;
        }
        const int exitCode = exitCodeOf(out.run);
        if (!out.openError.isEmpty() || out.run.status != SshResult::Status::Ok || exitCode != 0) {
            const QString text = !out.openError.isEmpty() ? out.openError : describeActionFailure(QStringLiteral("Relevé"), exitCode, out.run.message);
            setActionStatus(text, runResultText(out.run));
            if (tasks && taskId) tasks->fail(taskId, text);
            refresh();
            return;
        }
        setActionStatus(QStringLiteral("Courrier relevé et indexé."), runResultText(out.run));
        if (tasks && taskId) tasks->succeed(taskId, QStringLiteral("Relevé terminé."));
        refresh();
    });
}

void MailController::cancelAction() {
    if (!m_actionBusy) return;
    if (m_actionTask != 0 && m_remote->tasks()) m_remote->tasks()->requestCancel(m_actionTask);
    else if (m_actionCancel) m_actionCancel->store(true);
}

}  // namespace fortiq
