#include "NetworkController.h"

#include "NetworkInfo.h"
#include "../core/RemoteController.h"
#include "../core/TaskManager.h"
#include "../ssh/SshClient.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QtConcurrentRun>

namespace fortiq {

namespace {
struct SnapshotOutcome {
    NetSnapshot snapshot;
    QString error;
    bool cancelled = false;
};
struct ProbeOutcome {
    SshResult run;
    QString openError;
};
}  // namespace

NetworkController::NetworkController(RemoteController *remote, QObject *parent)
    : QObject(parent), m_remote(remote), m_probeSession(std::make_shared<std::unique_ptr<SshSession>>()) {
    m_pool.setMaxThreadCount(1);
    m_probePool.setMaxThreadCount(1);
    m_probePool.setExpiryTimeout(-1);
    connect(remote, &RemoteController::changed, this, &NetworkController::onRemoteChanged);
    if (remote->tasks())
        connect(remote->tasks(), &TaskManager::cancelRequested, this, [this](int id) { if (id == m_probeTask && m_probeCancel) m_probeCancel->store(true); });
}

NetworkController::~NetworkController() {
    if (m_cancel) m_cancel->store(true);
    if (m_probeCancel) m_probeCancel->store(true);
    auto slot = m_probeSession;
    (void)QtConcurrent::run(&m_probePool, [slot] { slot->reset(); });
    m_pool.waitForDone();
    m_probePool.waitForDone();
}

void NetworkController::resetAll(const QString &state, const QString &status) {
    m_interfaces.clear(); m_routes.clear(); m_listeners.clear(); m_notes.clear();
    m_dnsServers.clear(); m_dnsSearch.clear(); m_tools.clear();
    m_dnsNote.clear(); m_resolvedText.clear(); m_hostname.clear(); m_updated.clear();
    m_exposed = 0;
    m_state = state;
    m_status = status;
    m_probeOutput.clear(); m_probeStatus.clear(); m_probeKind.clear(); m_probeTarget.clear();
    emit changed();
    emit probeChanged();
}

void NetworkController::onRemoteChanged() {
    if (m_remote->connected() && m_remote->connectionToken() == m_token) return;
    if (m_cancel) m_cancel->store(true);
    if (m_probeCancel) m_probeCancel->store(true);
    ++m_seq; ++m_probeSeq;
    m_busy = false; m_probeBusy = false;
    m_token = m_remote->connected() ? m_remote->connectionToken() : -1;
    auto slot = m_probeSession;
    (void)QtConcurrent::run(&m_probePool, [slot] { slot->reset(); });
    m_probeSession = std::make_shared<std::unique_ptr<SshSession>>();
    resetAll(QStringLiteral("disconnected"), QStringLiteral("Source déconnectée ou changée."));
    if (m_active && m_remote->connected()) reload();
}

void NetworkController::setWindowActive(bool active) {
    const bool opening = active && !m_active;
    m_active = active;
    if (opening && m_remote->connected()) reload();
}

void NetworkController::reload() {
    if (m_busy || !m_remote->connected()) return;
    m_token = m_remote->connectionToken();
    const int token = m_token, seq = ++m_seq;
    const ConnectionRequest request = m_remote->connectionRequest();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_cancel;
    m_busy = true;
    m_state = QStringLiteral("loading");
    m_status = QStringLiteral("Lecture du réseau du serveur…");
    emit changed();

    auto *watcher = new QFutureWatcher<SnapshotOutcome>(this);
    connect(watcher, &QFutureWatcher<SnapshotOutcome>::finished, this, [this, watcher, seq, token] {
        const SnapshotOutcome r = watcher->result();
        watcher->deleteLater();
        if (seq != m_seq || !m_remote->connected() || token != m_remote->connectionToken()) return;
        m_busy = false;
        if (r.cancelled) { m_state = m_interfaces.isEmpty() ? QStringLiteral("disconnected") : QStringLiteral("ready"); emit changed(); return; }
        if (!r.error.isEmpty() || !r.snapshot.ok) {
            m_state = QStringLiteral("error");
            m_status = !r.error.isEmpty() ? r.error : r.snapshot.problem;
            emit changed();
            return;
        }
        const NetSnapshot &s = r.snapshot;
        m_hostname = s.hostname;
        m_interfaces = interfacesToVariant(s.interfaces);
        m_routes = routesToVariant(s.routes);
        m_listeners = listenersToVariant(s.listeners);
        m_exposed = 0;
        for (const NetListener &l : s.listeners) m_exposed += l.exposure == QLatin1String("all");
        m_notes = s.notes;
        m_dnsServers = s.dnsServers; m_dnsSearch = s.dnsSearch; m_dnsNote = s.dnsNote; m_resolvedText = s.resolvedText;
        m_tools = s.tools;
        m_updated = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        m_state = QStringLiteral("ready");
        m_status = QStringLiteral("Lecture seule · réseau de %1").arg(m_hostname.isEmpty() ? m_remote->serverLabel() : m_hostname);
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [request, cancel] {
        SnapshotOutcome out;
        SshSession session;
        const SshResult opened = session.open(request);   // host pin re-checked
        if (opened.status != SshResult::Status::Ok) { out.error = opened.message; return out; }
        if (cancel->load()) { out.cancelled = true; return out; }
        const SshResult run = session.runScript(buildNetSnapshotScript(), 1024 * 1024, [cancel] { return !cancel->load(); });
        if (run.code == QLatin1String("cancelled")) { out.cancelled = true; return out; }
        if (run.status != SshResult::Status::Ok) { out.error = run.message.isEmpty() ? QStringLiteral("Connexion SSH perdue.") : run.message; return out; }
        if (run.truncated) { out.error = QStringLiteral("Réponse trop grande (limite 1 Mio)."); return out; }
        out.snapshot = parseNetSnapshot(run.data);
        return out;
    }));
}

void NetworkController::runProbe(const QString &kind, const QString &target) {
    if (m_probeBusy) return;
    QString error;
    const QByteArray script = buildProbeScript(kind, target, &error);
    if (script.isEmpty()) {
        m_probeKind = kind; m_probeTarget = target;
        m_probeOutput.clear();
        m_probeStatus = error;
        emit probeChanged();
        return;
    }
    if (!m_remote->connected()) { m_probeStatus = QStringLiteral("Aucun serveur connecté"); emit probeChanged(); return; }
    const int token = m_remote->connectionToken(), seq = ++m_probeSeq;
    m_probeCancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_probeCancel;
    m_probeKind = kind; m_probeTarget = target.trimmed();
    m_probeOutput.clear();
    m_probeBusy = true;
    m_probeStatus = QStringLiteral("Exécution sur le serveur…");

    int taskId = 0;
    if (TaskManager *tasks = m_remote->tasks()) {
        TaskManager::Spec spec;
        spec.kind = QStringLiteral("networkProbe");
        spec.title = kind + QLatin1Char(' ') + m_probeTarget;
        spec.source = m_remote->serverLabel();
        spec.destination = m_probeTarget;
        spec.cancellable = true;
        spec.generation = token;
        taskId = m_probeTask = tasks->create(spec);
        tasks->start(taskId);
    }
    emit probeChanged();

    const ConnectionRequest request = m_remote->connectionRequest();
    auto slot = m_probeSession;
    auto *watcher = new QFutureWatcher<ProbeOutcome>(this);
    connect(watcher, &QFutureWatcher<ProbeOutcome>::finished, this, [this, watcher, seq, token, taskId] {
        const ProbeOutcome r = watcher->result();
        watcher->deleteLater();
        TaskManager *tasks = m_remote->tasks();
        if (seq != m_probeSeq || token != m_remote->connectionToken() || !m_remote->connected()) {
            if (tasks) tasks->markCancelled(taskId, QStringLiteral("Annulé."));
            return;
        }
        m_probeBusy = false;
        if (r.run.code == QLatin1String("cancelled")) {
            m_probeStatus = QStringLiteral("Annulé.");
            m_probeOutput = QString::fromUtf8(r.run.data);
            if (tasks) tasks->markCancelled(taskId, QStringLiteral("Annulé."));
        } else if (!r.openError.isEmpty() || r.run.status != SshResult::Status::Ok) {
            m_probeStatus = !r.openError.isEmpty() ? r.openError : (r.run.message.isEmpty() ? QStringLiteral("Connexion SSH perdue.") : r.run.message);
            if (tasks) tasks->fail(taskId, m_probeStatus);
        } else {
            m_probeOutput = QString::fromUtf8(r.run.data);
            if (r.run.truncated) m_probeOutput += QStringLiteral("\n[sortie tronquée]");
            const bool toolMissing = r.run.code == QLatin1String("exit:3");
            m_probeStatus = toolMissing ? QStringLiteral("Outil absent sur le serveur.") : QStringLiteral("Terminé.");
            if (tasks) { if (toolMissing) tasks->fail(taskId, m_probeStatus); else tasks->succeed(taskId, m_probeStatus); }
        }
        emit probeChanged();
    });
    watcher->setFuture(QtConcurrent::run(&m_probePool, [slot, request, script, cancel] {
        ProbeOutcome out;
        if (cancel->load()) { out.run.code = QStringLiteral("cancelled"); return out; }
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(request);
            if (opened.status != SshResult::Status::Ok) { out.openError = opened.message; return out; }
        }
        out.run = session.runScript(script, 64 * 1024, [cancel] { return !cancel->load(); });
        return out;
    }));
}

void NetworkController::cancelProbe() {
    if (!m_probeBusy) return;
    if (m_probeTask != 0 && m_remote->tasks()) m_remote->tasks()->requestCancel(m_probeTask);
    else if (m_probeCancel) m_probeCancel->store(true);
}

}  // namespace fortiq
