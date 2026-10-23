#include "RemoteControllerInternal.h"

namespace fortiq {
RemoteController::Transfer *RemoteController::findTransfer(int id) {
    const auto it = m_transferJobs.find(id);
    return it == m_transferJobs.end() ? nullptr : &it.value();
}

// Transfers are tasks of the shared TaskManager; this controller keeps only
// the job details (paths, cancel flag) that the manager does not need.
int RemoteController::createTransferTask(const QString &kind, const QString &name, const QString &destination) {
    TaskManager::Spec spec;
    spec.kind = kind;
    spec.title = name;
    spec.source = serverLabel();
    spec.destination = destination;
    spec.cancellable = true;
    spec.generation = m_generation;
    return m_tasks->create(spec);
}

void RemoteController::download(const QString &remotePath, const QUrl &localFile) {
    const auto clean = cleanAbsolutePath(remotePath);
    const QString local = localDocumentPath(localFile);
    if (!m_connected || !clean || local.isEmpty()) return;
    Transfer t;
    t.name = clean->section('/', -1);
    t.remote = *clean;
    t.local = local;
    t.id = createTransferTask(QStringLiteral("download"), t.name, local);
    m_transferJobs.insert(t.id, t);
    startTransfer(t.id, static_cast<int>(ConflictPolicy::Fail));
}

void RemoteController::upload(const QVariantList &localFiles, const QString &remoteDir) {
    const auto dir = cleanAbsolutePath(remoteDir.isEmpty() ? m_currentDirectory : remoteDir);
    if (!m_connected || !dir) return;
    for (const QVariant &v : localFiles) {
        const QUrl url = v.toUrl();
        const QString local = localDocumentPath(url);
        if (local.isEmpty()) continue;
        Transfer t;
        t.upload = true;
        t.name = localDocumentName(local);
        if (t.name.isEmpty()) {
            const int id = createTransferTask(QStringLiteral("upload"), QStringLiteral("Document"), *dir);
            m_tasks->fail(id, QStringLiteral("Cannot read the selected document name. Select the file again to renew access."));
            continue;
        }
        t.remote = *dir;
        t.local = local;
        t.id = createTransferTask(QStringLiteral("upload"), t.name, *dir);
        m_transferJobs.insert(t.id, t);
        startTransfer(t.id, static_cast<int>(ConflictPolicy::Fail));
    }
}

void RemoteController::resolveConflict(int id, const QString &policy) {
    if (!findTransfer(id) || m_tasks->task(id).value("decision").toString() != QLatin1String("conflict")) return;
    if (policy == "skip") {
        m_tasks->markCancelled(id, QStringLiteral("Ignoré ; le fichier existant est conservé."));
        return;
    }
    m_tasks->resolveDecision(id);
    startTransfer(id, static_cast<int>(policy == "replace" ? ConflictPolicy::Replace : ConflictPolicy::KeepBoth));
}

void RemoteController::cancelTransfer(int id) { m_tasks->requestCancel(id); }
void RemoteController::clearFinishedTransfers() { m_tasks->clearFinished(); }

void RemoteController::onTaskCancelRequested(int id) {
    if (Transfer *t = findTransfer(id)) t->cancel->store(true);
    if (id == m_searchTaskId) m_searchCancel->store(true);
}

void RemoteController::startTransfer(int id, int policyValue) {
    Transfer *t = findTransfer(id);
    if (!t) return;
    t->cancel->store(false);
    ConnectionRequest req;
    req.host = m_host; req.port = m_port; req.user = m_user;
    req.password = m_password; req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = QStringLiteral("/");
    const Transfer job = *t;
    const int token = m_generation;
    const auto policy = static_cast<ConflictPolicy>(policyValue);
    QPointer<RemoteController> self(this);
    auto slot = m_transferSession;

    auto report = [self, id, token](qint64 done, qint64 total) {
        QMetaObject::invokeMethod(self, [self, id, token, done, total] {
            if (!self || token != self->m_generation) return;
            self->m_tasks->setProgress(id, done, total);
        }, Qt::QueuedConnection);
    };

    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, id, token, job] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        if (token != m_generation) return;
        // Cancelled while queued, or settled by a disconnect: the task is final.
        const auto state = m_tasks->state(id);
        if (!state || TaskManager::isTerminal(*state)) return;
        if (r.status == SshResult::Status::Ok) {
            m_tasks->succeed(id, job.upload ? QStringLiteral("Publié : %1").arg(r.directory.section('/', -1))
                                            : QStringLiteral("Enregistré : %1").arg(r.directory), r.bytes);
            if (job.upload && m_connected && job.remote == m_currentDirectory) refresh();
        } else if (r.code == "conflict") {
            // The user decides (keep both / replace / skip); a stop request wins.
            if (!m_tasks->requestDecision(id, QStringLiteral("conflict"), r.message)) m_tasks->markCancelled(id, QStringLiteral("Annulé."));
        } else if (r.code == "cancelled" || state == TaskManager::State::Cancelling) {
            m_tasks->markCancelled(id, r.message.isEmpty() ? QStringLiteral("Annulé.") : r.message);
        } else if (r.code == "unknown") {
            m_tasks->markUnknown(id, r.message);
        } else {
            m_tasks->fail(id, r.message, r.code);
        }
    });
    watcher->setFuture(QtConcurrent::run(&m_transferPool, [slot, req, job, policy, report, self, id, token] {
        if (job.cancel->load()) return SshResult{};  // cancelled while queued; result ignored
        QMetaObject::invokeMethod(self, [self, id, token] {
            if (!self || token != self->m_generation) return;
            self->m_tasks->start(id);
        }, Qt::QueuedConnection);
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(req); // host pin re-checked
            if (opened.status != SshResult::Status::Ok) return opened;
        }
        QElapsedTimer tick;
        tick.start();
        const TransferProgress progress = [&](qint64 done, qint64 total) {
            if (tick.elapsed() > 120 || done == total) { tick.restart(); report(done, total); }
            return !job.cancel->load();
        };
        if (job.upload) return session.upload(job.local, job.remote, job.name, policy, progress);
        auto result = session.download(job.remote, job.local, progress);
        // A native document picker can outlive an idle transport. Retry only
        // the read-only preflight, before any destination has been opened.
        if (result.code == "disconnected" && !job.cancel->load()) {
            const auto reopened = session.open(req); // pin checked again
            if (reopened.status != SshResult::Status::Ok) return reopened;
            result = session.download(job.remote, job.local, progress);
        }
        return result;
    }));
}
}  // namespace fortiq
