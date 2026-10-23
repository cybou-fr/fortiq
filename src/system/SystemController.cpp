#include "SystemController.h"

#include "SystemInfo.h"
#include "../core/RemoteController.h"
#include "../ssh/SshClient.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QtConcurrentRun>

namespace fortiq {

namespace {
struct Outcome {
    SystemSnapshot snapshot;
    QString error;
    bool cancelled = false;
};
}  // namespace

SystemController::SystemController(RemoteController *remote, QObject *parent)
    : QObject(parent), m_remote(remote), m_session(std::make_shared<std::unique_ptr<SshSession>>()) {
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
    connect(remote, &RemoteController::changed, this, &SystemController::onRemoteChanged);
}

SystemController::~SystemController() {
    if (m_cancel) m_cancel->store(true);
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_pool.waitForDone();
}

void SystemController::onRemoteChanged() {
    if (m_remote->connected() && m_remote->connectionToken() == m_token) return;
    if (m_cancel) m_cancel->store(true);
    ++m_seq;
    m_busy = false;
    m_token = m_remote->connected() ? m_remote->connectionToken() : -1;
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_session = std::make_shared<std::unique_ptr<SshSession>>();
    m_overview.clear(); m_processes.clear(); m_services.clear(); m_notes.clear();
    m_hasSystemd = false; m_updated.clear();
    m_state = QStringLiteral("disconnected");
    m_status = QStringLiteral("Source déconnectée ou changée.");
    emit changed();
    if (m_active && m_remote->connected()) reload();
}

void SystemController::setWindowActive(bool active) {
    const bool opening = active && !m_active;
    m_active = active;
    if (opening && m_remote->connected()) reload();
}

void SystemController::reload() {
    if (m_busy || !m_remote->connected()) return;
    m_token = m_remote->connectionToken();
    const int token = m_token, seq = ++m_seq;
    const ConnectionRequest request = m_remote->connectionRequest();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_cancel;
    m_busy = true;
    // The first read shows a state; later refreshes keep the old data on screen until the new one arrives.
    if (m_processes.isEmpty()) { m_state = QStringLiteral("loading"); m_status = QStringLiteral("Lecture des processus et des services…"); }
    emit changed();

    auto *watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, seq, token] {
        const Outcome r = watcher->result();
        watcher->deleteLater();
        if (seq != m_seq || !m_remote->connected() || token != m_remote->connectionToken()) return;
        m_busy = false;
        if (r.cancelled) { emit changed(); return; }
        if (!r.error.isEmpty() || !r.snapshot.ok) {
            m_state = QStringLiteral("error");
            m_status = !r.error.isEmpty() ? r.error : r.snapshot.problem;
            emit changed();
            return;
        }
        const SystemSnapshot &s = r.snapshot;
        int running = 0, zombies = 0, failed = 0, active = 0;
        for (const ProcessInfo &p : s.processes) { running += p.state.startsWith(QLatin1Char('R')); zombies += p.state.startsWith(QLatin1Char('Z')); }
        for (const ServiceInfo &sv : s.services) { failed += sv.active == QLatin1String("failed"); active += sv.active == QLatin1String("active"); }
        m_overview = overviewToVariant(s.overview, static_cast<int>(s.processes.size()), running, zombies, failed, active, static_cast<int>(s.services.size()));
        m_processes = processesToVariant(s.processes);
        m_services = servicesToVariant(s.services);
        m_hasSystemd = s.hasSystemd;
        m_notes = s.notes;
        m_updated = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        m_state = QStringLiteral("ready");
        m_status = QStringLiteral("Lecture seule · %1").arg(s.overview.hostname.isEmpty() ? m_remote->serverLabel() : s.overview.hostname);
        emit changed();
    });
    auto slot = m_session;
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, request, cancel] {
        Outcome out;
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(request);   // host pin re-checked
            if (opened.status != SshResult::Status::Ok) { out.error = opened.message; return out; }
        }
        if (cancel->load()) { out.cancelled = true; return out; }
        const SshResult run = session.runScript(buildSystemSnapshotScript(), 4 * 1024 * 1024, [cancel] { return !cancel->load(); });
        if (run.code == QLatin1String("cancelled")) { out.cancelled = true; return out; }
        if (run.status != SshResult::Status::Ok) { out.error = run.message.isEmpty() ? QStringLiteral("Connexion SSH perdue.") : run.message; return out; }
        if (run.truncated) { out.error = QStringLiteral("Réponse trop grande (limite 4 Mio)."); return out; }
        out.snapshot = parseSystemSnapshot(run.data);
        return out;
    }));
}

}  // namespace fortiq
