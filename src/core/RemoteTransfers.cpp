#include "RemoteControllerInternal.h"

namespace fortiq {
QVariantList RemoteController::transfers() const {
    QVariantList out;
    for (const Transfer &t : m_transfers) {
        out.push_back(QVariantMap {
            { "id", t.id }, { "upload", t.upload }, { "name", t.name }, { "remote", t.remote },
            { "local", t.local }, { "state", t.state }, { "message", t.message },
            { "done", static_cast<double>(t.done) }, { "total", static_cast<double>(t.total) } });
    }
    return out;
}
int RemoteController::activeTransfers() const {
    int n = 0;
    for (const Transfer &t : m_transfers) n += (t.state == "queued" || t.state == "running" || t.state == "conflict");
    return n;
}
RemoteController::Transfer *RemoteController::findTransfer(int id) {
    for (Transfer &t : m_transfers) if (t.id == id) return &t;
    return nullptr;
}

void RemoteController::download(const QString &remotePath, const QUrl &localFile) {
    const auto clean = cleanAbsolutePath(remotePath);
    const QString local = localDocumentPath(localFile);
    if (!m_connected || !clean || local.isEmpty()) return;
    Transfer t;
    t.id = m_nextTransferId++;
    t.name = clean->section('/', -1);
    t.remote = *clean;
    t.local = local;
    m_transfers.push_back(t);
    emit transfersChanged();
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
        t.id = m_nextTransferId++;
        t.upload = true;
        t.name = localDocumentName(local);
        if (t.name.isEmpty()) {
            t.name = QStringLiteral("Document");
            t.state = QStringLiteral("error");
            t.message = QStringLiteral("Cannot read the selected document name. Select the file again to renew access.");
            m_transfers.push_back(t);
            continue;
        }
        t.remote = *dir;
        t.local = local;
        m_transfers.push_back(t);
        startTransfer(t.id, static_cast<int>(ConflictPolicy::Fail));
    }
    emit transfersChanged();
}

void RemoteController::resolveConflict(int id, const QString &policy) {
    Transfer *t = findTransfer(id);
    if (!t || t->state != "conflict") return;
    if (policy == "skip") {
        t->state = QStringLiteral("cancelled");
        t->message = QStringLiteral("Ignoré ; le fichier existant est conservé.");
        emit transfersChanged();
        return;
    }
    t->state = QStringLiteral("queued");
    t->message.clear();
    t->done = 0;
    emit transfersChanged();
    startTransfer(id, static_cast<int>(policy == "replace" ? ConflictPolicy::Replace : ConflictPolicy::KeepBoth));
}

void RemoteController::cancelTransfer(int id) {
    Transfer *t = findTransfer(id);
    if (!t) return;
    t->cancel->store(true);
    if (t->state == "queued" || t->state == "conflict") {
        t->state = QStringLiteral("cancelled");
        t->message = QStringLiteral("Annulé.");
        emit transfersChanged();
    }
}

void RemoteController::clearFinishedTransfers() {
    m_transfers.removeIf([](const Transfer &t) {
        return t.state == "done" || t.state == "error" || t.state == "cancelled" || t.state == "unknown";
    });
    emit transfersChanged();
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
            if (Transfer *t = self->findTransfer(id); t && t->state == "running") {
                t->done = done; t->total = total;
                emit self->transfersChanged();
            }
        }, Qt::QueuedConnection);
    };

    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, id, token, job] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        Transfer *t = findTransfer(id);
        if (!t || token != m_generation || t->state == "cancelled") return;
        if (r.status == SshResult::Status::Ok) {
            t->state = QStringLiteral("done");
            t->done = r.bytes;
            t->total = r.bytes;
            t->message = job.upload ? QStringLiteral("Publié : %1").arg(r.directory.section('/', -1))
                                    : QStringLiteral("Enregistré : %1").arg(r.directory);
            if (job.upload && m_connected && job.remote == m_currentDirectory) refresh();
        } else {
            t->state = r.code == "conflict" ? QStringLiteral("conflict")
                     : r.code == "cancelled" ? QStringLiteral("cancelled")
                     : r.code == "unknown" ? QStringLiteral("unknown") : QStringLiteral("error");
            t->message = r.message;
        }
        emit transfersChanged();
    });
    watcher->setFuture(QtConcurrent::run(&m_transferPool, [slot, req, job, policy, report, self, id, token] {
        if (job.cancel->load()) return SshResult{};  // cancelled while queued; result ignored
        QMetaObject::invokeMethod(self, [self, id, token] {
            if (!self || token != self->m_generation) return;
            if (Transfer *t = self->findTransfer(id); t && t->state == "queued") {
                t->state = QStringLiteral("running");
                emit self->transfersChanged();
            }
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
