#include "RemoteControllerInternal.h"
#include "../ssh/LinkMonitor.h"

namespace fortiq {
RemoteController::RemoteController(QObject *parent)
    : QObject(parent), m_session(std::make_shared<std::unique_ptr<SshSession>>()),
      m_transferSession(std::make_shared<std::unique_ptr<SshSession>>()),
      m_searchSession(std::make_shared<std::unique_ptr<SshSession>>())
{
    m_searchPool.setMaxThreadCount(1);
    m_searchPool.setExpiryTimeout(-1);
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
    m_transferPool.setMaxThreadCount(1);
    m_transferPool.setExpiryTimeout(-1);
    connect(&LinkMonitor::instance(), &LinkMonitor::changed, this, [this](const QString &key) { if (key == linkKey()) emit linkChanged(); });
    connect(this, &RemoteController::changed, this, &RemoteController::linkChanged);   // server switched or disconnected
    m_ownedTasks = std::make_unique<TaskManager>();
    setTaskManager(m_ownedTasks.get());
}

void RemoteController::setTaskManager(TaskManager *tasks) {
    if (!tasks || tasks == m_tasks) return;
    if (m_tasks) disconnect(m_tasks, nullptr, this, nullptr);
    m_tasks = tasks;
    connect(m_tasks, &TaskManager::cancelRequested, this, &RemoteController::onTaskCancelRequested);
    // A finished transfer no longer needs its job details.
    connect(m_tasks, &TaskManager::finished, this, [this](int id) { m_transferJobs.remove(id); });
}

RemoteController::~RemoteController() {
    for (Transfer &t : m_transferJobs) t.cancel->store(true);
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    auto transferSlot = m_transferSession;
    (void)QtConcurrent::run(&m_transferPool, [transferSlot] { transferSlot->reset(); });
    m_searchCancel->store(true);
    auto searchSlot = m_searchSession;
    (void)QtConcurrent::run(&m_searchPool, [searchSlot] { searchSlot->reset(); });
    m_pool.waitForDone();
    m_transferPool.waitForDone();
    m_searchPool.waitForDone();
}

ConnectionRequest RemoteController::connectionRequest() const {
    ConnectionRequest req;
    req.host = m_host; req.port = m_port; req.user = m_user;
    req.password = m_password; req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = QStringLiteral("/");
    return req;
}

void RemoteController::request(Kind kind, const QString &directory, bool record) {
    ConnectionRequest req;
    req.host = m_host;
    req.port = m_port;
    req.user = m_user;
    req.password = m_password; // kept in RAM until disconnect for transparent reconnects
    req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = directory;

    const int token = m_generation;
    const QString previous = m_currentDirectory;
    m_pendingDirectory = directory;
    // Show the RAM copy at once; the live listing replaces it when it arrives.
    if (kind == Kind::List && m_cache.contains(directory)) {
        m_currentDirectory = directory;
        m_entries = m_cache.value(directory);
    }
    m_busy = true;
    emit changed();

    auto slot = m_session;
    auto *watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, token, kind, previous, record] {
        const Outcome outcome = watcher->result();
        watcher->deleteLater();
        if (token != m_generation) return; // old server's async response
        const SshResult &result = outcome.result;
        if (result.status == SshResult::Status::Ok) m_cache.insert(result.directory, result.entries);
        // A newer navigation is queued; only its answer may change the view.
        if (kind == Kind::List && result.status == SshResult::Status::Ok
            && result.directory != m_pendingDirectory) return;
        m_busy = false;
        m_latencyMs = outcome.elapsedMs;
        if (result.status == SshResult::Status::TrustRequired) {
            m_connected = false;
            m_pendingFingerprint = result.fingerprint;
            m_status = result.message;
        } else if (result.status == SshResult::Status::Ok) {
            if (record && !previous.isEmpty() && previous != result.directory) {
                m_back.push_back(previous);
                m_forward.clear();
            }
            if (!m_connected) emit serversChanged();
            m_connected = true;
            m_pendingFingerprint.clear();
            m_currentDirectory = result.directory;
            m_home = result.home;
            m_entries = result.entries;
            m_status = result.message;
            if (kind == Kind::Open && m_rememberPending && !m_activeServerId.isEmpty()) {
                m_rememberPending = false;
                const SecretStore::Result saved = SecretStore::save(m_activeServerId, m_password);
                if (saved.ok) setSecretSaved(m_activeServerId, true);
                else m_status = QStringLiteral("Connected, but the password could not be remembered: ") + saved.error;
            }
            if (kind == Kind::Open) {
                QSettings settings;
                settings.setValue("bootstrap/lastHost", m_host);
                settings.setValue("bootstrap/lastPort", m_port);
                settings.setValue("bootstrap/lastUser", m_user);
                settings.setValue("bootstrap/lastKeyPath", m_keyPath); // path only, never key bytes
            }
        } else {
            m_status = result.message;
            if (kind == Kind::Open) {
                m_rememberPending = false;
                m_connected = false;
                m_entries.clear();
                m_currentDirectory.clear();
            } else {
                // Keep the user where they were; the failed folder is not entered.
                m_currentDirectory = previous;
                m_entries = m_cache.value(previous, m_entries);
            }
        }
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, kind] {
        QElapsedTimer timer;
        timer.start();
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        Outcome outcome;
        if (kind == Kind::Open || !session.isOpen()) {
            outcome.result = session.open(req);
        } else {
            outcome.result = session.list(req.directory);
            if (!session.isOpen()) outcome.result = session.open(req); // one reconnect, pin re-checked
        }
        outcome.elapsedMs = static_cast<int>(timer.elapsed());
        return outcome;
    }));
}

void RemoteController::navigate(const QString &path) {
    if (!m_connected) return;
    const auto clean = cleanAbsolutePath(path.trimmed());
    if (!clean) {
        m_status = QStringLiteral("Invalid folder path.");
        emit changed();
        return;
    }
    request(Kind::List, *clean, true);
}
void RemoteController::goUp() {
    if (m_connected && !m_currentDirectory.isEmpty() && m_currentDirectory != "/")
        request(Kind::List, parentPath(m_currentDirectory), true);
}
void RemoteController::goBack() {
    if (!m_connected || m_back.isEmpty()) return;
    m_forward.push_back(m_currentDirectory);
    request(Kind::List, m_back.takeLast(), false);
}
void RemoteController::goForward() {
    if (!m_connected || m_forward.isEmpty()) return;
    m_back.push_back(m_currentDirectory);
    request(Kind::List, m_forward.takeLast(), false);
}
void RemoteController::goHome() {
    if (m_connected && !m_home.isEmpty()) request(Kind::List, m_home, true);
}
void RemoteController::refresh() {
    if (m_connected && !m_currentDirectory.isEmpty()) request(Kind::List, m_currentDirectory, false);
}

QString RemoteController::linkKey() const {
    return m_connected && !m_host.isEmpty() ? LinkMonitor::key(m_host, m_port, m_user) : QString();
}
bool RemoteController::linkLost() const { const QString key = linkKey(); return !key.isEmpty() && LinkMonitor::instance().lost(key); }
QString RemoteController::linkLostAt() const {
    const QString key = linkKey();
    return key.isEmpty() ? QString() : LinkMonitor::instance().state(key).lostAt.toString(QStringLiteral("HH:mm"));
}
QString RemoteController::linkText() const {
    if (!linkLost()) return {};
    return QStringLiteral("Connexion perdue à %1 — les données affichées datent d'avant la coupure. Une nouvelle connexion est tentée à chaque actualisation.").arg(linkLostAt());
}

void RemoteController::disconnectServer() {
    LinkMonitor::forget(linkKey());   // an intentional disconnect is not a lost link
    m_rememberPending = false;
    clearSearch();
    auto searchSlot = m_searchSession;
    (void)QtConcurrent::run(&m_searchPool, [searchSlot] { searchSlot->reset(); });
    m_searchSession = std::make_shared<std::unique_ptr<SshSession>>();
    if (m_operationBusy) { m_operationBusy = false; emit operationBusyChanged(); }
    if (m_saving) { m_saving = false; emit savingChanged(); }
    // Tasks are bound to this server: stop them, never redirect them. What may
    // already have happened on the server is reported as unknown, not as failed.
    for (const int id : m_tasks->activeIds(m_generation)) {
        const QString state = m_tasks->task(id).value("state").toString();
        const bool running = state == QLatin1String("running") || state == QLatin1String("cancelling");
        if (Transfer *t = findTransfer(id)) {
            t->cancel->store(true);
            const bool documentMayBePartial = running && !t->upload && isContentDocument(t->local);
            const QString message = documentMayBePartial
                ? QStringLiteral("Déconnecté du serveur. Le document choisi peut être partiel ; vérifiez-le avant de réessayer.")
                : QStringLiteral("Déconnecté du serveur.");
            if (running && (t->upload || documentMayBePartial)) m_tasks->markUnknown(id, message);
            else m_tasks->markCancelled(id, message);
        } else if (m_tasks->task(id).value("session").toBool()) {
            // A read, a view or an interactive session: there is nothing to verify afterwards.
            m_tasks->markCancelled(id, QStringLiteral("Déconnecté du serveur."));
        } else if (running) {
            m_tasks->markUnknown(id, QStringLiteral("Déconnecté du serveur pendant l'opération ; vérifiez le résultat sur le serveur."));
        } else {
            m_tasks->markCancelled(id, QStringLiteral("Déconnecté du serveur."));
        }
    }
    auto transferSlot = m_transferSession;
    (void)QtConcurrent::run(&m_transferPool, [transferSlot] { transferSlot->reset(); });
    m_transferSession = std::make_shared<std::unique_ptr<SshSession>>();
    closePreview();
    ++m_generation; // outstanding results can never bind to another server
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_session = std::make_shared<std::unique_ptr<SshSession>>();
    m_busy = false;
    m_connected = false;
    m_pendingFingerprint.clear();
    m_entries.clear();
    m_currentDirectory.clear();
    m_home.clear();
    m_back.clear();
    m_forward.clear();
    m_cache.clear();
    scrubPassword();
    emit serversChanged();
    m_status = QStringLiteral("Disconnected. Working data is not stored on this device.");
    emit changed();
}
}  // namespace fortiq
