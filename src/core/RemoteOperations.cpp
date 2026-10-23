#include "RemoteControllerInternal.h"

namespace fortiq {
void RemoteController::runOperation(const QString &doneMessage, std::function<SshResult(SshSession &)> op) {
    if (!m_connected || m_operationBusy) return;
    ConnectionRequest req;
    req.host = m_host; req.port = m_port; req.user = m_user;
    req.password = m_password; req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = m_currentDirectory;
    const int token = m_generation;
    const QString folder = m_currentDirectory;
    m_operationBusy = true;
    emit operationBusyChanged();
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, token, folder, doneMessage] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        if (token != m_generation) return;
        m_operationBusy = false;
        emit operationBusyChanged();
        const bool ok = r.status == SshResult::Status::Ok;
        // The folder changed (or may have): drop its RAM copy and re-list.
        if (ok || r.code == "unknown") {
            // Restores and Trash moves touch other folders too.
            m_cache.clear();
            if (m_currentDirectory == folder) refresh();
        }
        emit operationFinished(ok, ok ? doneMessage : r.message, r.code, r.directory);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, op] {
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(req); // pin re-checked
            if (opened.status != SshResult::Status::Ok) return opened;
        }
        // Never replay a mutation after a drop: the result says "unknown".
        return op(session);
    }));
}

void RemoteController::createFolder(const QString &name) {
    const QString dir = m_currentDirectory;
    runOperation(QStringLiteral("Dossier « %1 » créé.").arg(name.trimmed()),
                 [dir, name](SshSession &s) { return s.makeDirectory(dir, name); });
}
void RemoteController::renameEntry(const QString &path, const QString &newName) {
    runOperation(QStringLiteral("Renommé en « %1 ».").arg(newName.trimmed()),
                 [path, newName](SshSession &s) { return s.renameEntry(path, newName); });
}
void RemoteController::trashEntry(const QString &path) {
    runOperation(QStringLiteral("« %1 » mis à la corbeille.").arg(path.section('/', -1)),
                 [path](SshSession &s) { return s.moveToTrash(path); });
}

void RemoteController::restoreEntry(const QString &trashedPath, bool keepBoth) {
    runOperation(QStringLiteral("« %1 » restauré.").arg(trashedPath.section('/', -1)), [trashedPath, keepBoth](SshSession &s) {
        return s.restoreFromTrash(trashedPath, keepBoth ? ConflictPolicy::KeepBoth : ConflictPolicy::Fail);
    });
}
void RemoteController::deletePermanently(const QString &trashedPath) {
    runOperation(QStringLiteral("« %1 » supprimé définitivement.").arg(trashedPath.section('/', -1)),
                 [trashedPath](SshSession &s) { return s.deleteFromTrash(trashedPath); });
}
void RemoteController::emptyTrash() {
    runOperation(QStringLiteral("Corbeille vidée."), [](SshSession &s) { return s.emptyTrash(); });
}

void RemoteController::saveFile(const QString &text, bool force) { runSave(text, force ? 1 : 0); }
void RemoteController::saveFileCopy(const QString &text) { runSave(text, 2); }

void RemoteController::runSave(const QString &text, int mode) {
    if (!m_connected || m_saving || !previewEditable()) return;
    // Keep the file's own conventions: line endings and byte-order mark.
    QString body = text;
    body.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (m_editCrlf) body.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    QByteArray bytes = body.toUtf8();
    if (m_editBom) bytes.prepend("\xEF\xBB\xBF");

    ConnectionRequest req;
    req.host = m_host; req.port = m_port; req.user = m_user;
    req.password = m_password; req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = m_currentDirectory;
    const QString path = m_previewPath;
    const QByteArray expected = m_editHash;
    const int token = m_generation;
    const int seq = m_previewSeq;
    m_saving = true;
    emit savingChanged();

    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, token, seq, mode, text] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        if (token != m_generation) return;
        m_saving = false;
        emit savingChanged();
        const bool ok = r.status == SshResult::Status::Ok;
        if (ok && mode != 2 && seq == m_previewSeq) {
            // The saved bytes are the new base version for the next save.
            m_editHash = r.data;
            m_previewText = text;
            m_previewInfo = QStringLiteral("%1 — enregistré").arg(humanSize(r.bytes));
            emit previewChanged();
        }
        if (ok) {
            m_cache.remove(parentPath(r.directory));
            if (m_currentDirectory == parentPath(r.directory)) refresh();
        }
        emit saveFinished(ok, ok ? (mode == 2 ? QStringLiteral("Copie enregistrée : %1").arg(r.directory.section('/', -1))
                                               : QStringLiteral("Enregistré."))
                                 : r.message,
                          r.code, r.directory);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, path, bytes, expected, mode] {
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(req); // pin re-checked
            if (opened.status != SshResult::Status::Ok) return opened;
        }
        // Never replayed after a drop: the user decides after "unknown".
        return mode == 2 ? session.saveCopy(parentPath(path), path.section('/', -1), bytes)
                         : session.saveFile(path, bytes, expected, mode == 1);
    }));
}
}  // namespace fortiq
