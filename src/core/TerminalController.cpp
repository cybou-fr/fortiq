#include "TerminalController.h"
#include "RemoteController.h"
#include "TaskManager.h"
#include "TerminalHistory.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <utility>
#include <algorithm>

namespace fortiq {
TerminalController::TerminalController(RemoteController *remote, QObject *parent)
    : QObject(parent), m_remote(remote), m_session(std::make_shared<std::unique_ptr<SshSession>>()) {
    m_pool.setMaxThreadCount(1); m_pool.setExpiryTimeout(-1);
    m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, &TerminalController::poll);
    if (remote->tasks())
        connect(remote->tasks(), &TaskManager::cancelRequested, this, [this](int id) {
            if (id != 0 && id == m_task) { m_endHow = QStringLiteral("cancelled"); m_endMessage = QStringLiteral("Terminal fermé par l'utilisateur."); stop(); }
        });
    connect(remote, &RemoteController::changed, this, [this] {
        if(m_historyToken>=0 && (!m_remote->connected() || m_remote->connectionToken()!=m_historyToken)) {
            m_historyToken=-1; m_history.clear(); m_historyStatus=QStringLiteral("Source SSH déconnectée."); emit historyChanged();
        }
        if (m_serverToken >= 0 && (!m_remote->connected() || m_remote->connectionToken() != m_serverToken)) {
            m_endHow=QStringLiteral("failed");
            m_endMessage=QStringLiteral("Connexion SSH perdue : état du processus distant inconnu.");
            stop(); emit resetScreen();
            m_history.clear(); m_historyStatus = QStringLiteral("Source SSH déconnectée."); emit historyChanged();
            m_status = QStringLiteral("Source SSH déconnectée ou changée."); emit changed();
        }
        maybeStart();
    });
}
TerminalController::~TerminalController() {
    m_timer.stop(); auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); }); m_pool.waitForDone();
}
// The shell session is one task for as long as it runs: closing it by the user is a cancel, a shell that ends is a success, a broken link a failure.
void TerminalController::endTask(const QString &how, const QString &message) {
    TaskManager *tasks = m_remote ? m_remote->tasks() : nullptr;
    const int id = std::exchange(m_task, 0);
    if (!tasks || id == 0) return;
    if (how == "cancelled") tasks->markCancelled(id, message);
    else if (how == "failed") tasks->fail(id, message);
    else tasks->succeed(id, message);
}
void TerminalController::stop() {
    endTask(m_endHow.isEmpty() ? QStringLiteral("succeeded") : m_endHow, m_endMessage.isEmpty() ? QStringLiteral("Terminal fermé.") : m_endMessage);
    m_endHow.clear(); m_endMessage.clear();
    ++m_seq; m_timer.stop(); m_input.clear(); m_backpressure = false; m_running = m_busy = m_pollBusy = false; m_serverToken = -1;
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_session = std::make_shared<std::unique_ptr<SshSession>>();
    m_status = QStringLiteral("Terminal fermé. Aucun rejeu automatique."); emit changed();
}
void TerminalController::setWindowActive(bool active) {
    // Reopening the window is not consent to launch a new remote shell.
    // Auto-start at most once per SSH connection token; explicit start()
    // remains available after the user closes or loses a terminal.
    m_windowActive = active;
    maybeStart();
}
void TerminalController::maybeStart() {
    if (m_windowActive && m_remote->connected() && !m_running && !m_busy
        && m_autoAttemptToken != m_remote->connectionToken()) start();
}
void TerminalController::start() {
    if (m_busy || m_running || !m_remote->connected()) return;
    m_serverToken = m_remote->connectionToken(); m_autoAttemptToken = m_historyToken = m_serverToken; const int seq = ++m_seq;
    const auto req = m_remote->connectionRequest(); auto slot = m_session;
    const int columns = m_columns, rows = m_rows;
    m_busy = true; m_history.clear(); m_historyStatus = QStringLiteral("Chargement de ~/.bash_history…"); emit historyChanged();
    m_status = QStringLiteral("Ouverture SSH / PTY…"); emit resetScreen(); emit changed();
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, seq] {
        const auto r = watcher->result(); watcher->deleteLater();
        if (seq != m_seq) return;
        m_busy = false; m_running = r.status == SshResult::Status::Ok;
        m_history = m_running && r.home == "history-ok" ? terminalHistoryRows(r.data) : QVariantList{};
        m_historyStatus = m_running && r.home == "history-ok"
            ? QStringLiteral("~/.bash_history · lignes enregistrées, plus récentes en haut")
            : QStringLiteral("Historique Bash absent ou non accessible.");
        emit historyChanged();
        m_status = m_running ? QStringLiteral("Shell interactif distant · SSH") : r.message;
        if (m_running) {
            m_timer.start();
            if (TaskManager *tasks = m_remote->tasks()) {
                TaskManager::Spec spec;
                spec.kind = QStringLiteral("terminal"); spec.title = QStringLiteral("Terminal · ") + m_remote->serverLabel(); spec.source = m_remote->serverLabel();
                spec.generation = m_serverToken; spec.cancellable = true; spec.guards = true; spec.session = true;   // leaving ends the shell: ask first
                m_task = tasks->begin(spec);
            }
        } else m_serverToken = -1;
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, columns, rows] {
        *slot = std::make_unique<SshSession>();
        auto r = (*slot)->open(req); // pinned fingerprint checked before authentication
        if (r.status == SshResult::Status::Ok) {
            // Existing server-side history only; no keystroke capture or shell hooks.
            const auto history = (*slot)->runScript("if [ -f \"$HOME/.bash_history\" ] && [ -r \"$HOME/.bash_history\" ]; then tail -n 200 -- \"$HOME/.bash_history\"; else exit 3; fi\n",65536);
            r = (*slot)->startTerminal(columns, rows);
            if(r.status == SshResult::Status::Ok) { r.data=history.data; r.home=history.code=="exit:0" && !history.truncated ? "history-ok" : "history-unavailable"; }
        }
        if (r.status != SshResult::Status::Ok) slot->reset();
        return r;
    }));
}
void TerminalController::resize(int columns, int rows) {
    m_columns = qBound(2, columns, 240); m_rows = qBound(2, rows, 100);
}
void TerminalController::send(const QByteArray &data) {
    if (!m_running || data.isEmpty()) return;
    if (data.size() + m_input.size() > 32768) {
        m_endHow = QStringLiteral("failed"); m_endMessage = QStringLiteral("Entrée trop volumineuse : terminal fermé sans rejeu.");
        stop(); m_status = QStringLiteral("Entrée trop volumineuse : terminal fermé sans rejeu."); emit changed(); return;
    }
    m_input += data;
}
void TerminalController::poll() {
    if (!m_running || m_pollBusy) return;
    if (m_backpressure && m_input.isEmpty()) return;                 // nothing to send and the screen is still busy: do not read more
    if (!m_remote->connected() || m_remote->connectionToken() != m_serverToken) {
        m_endHow=QStringLiteral("failed");
        m_endMessage=QStringLiteral("Connexion SSH perdue : état du processus distant inconnu.");
        stop(); emit resetScreen(); return;
    }
    m_pollBusy = true; const int seq = m_seq; auto slot = m_session;
    const auto input = std::exchange(m_input, {}); const int columns = m_columns, rows = m_rows; const bool readOutput = !m_backpressure;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, seq] {
        const auto r = watcher->result(); watcher->deleteLater(); if (seq != m_seq) return;
        m_pollBusy = false;
        if (!r.data.isEmpty()) emit output(r.data);
        if (r.status != SshResult::Status::Ok || !r.code.isEmpty()) {
            m_endHow = r.status == SshResult::Status::Ok ? QStringLiteral("succeeded") : QStringLiteral("failed");
            m_endMessage = r.status == SshResult::Status::Ok ? QStringLiteral("Shell terminé (%1).").arg(r.code) : r.message;
            stop(); m_status = r.status == SshResult::Status::Ok ? QStringLiteral("Shell terminé (%1).").arg(r.code) : r.message;
            emit changed();
        }
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, input, columns, rows, readOutput] {
        if (!*slot) return SshResult{};
        return (*slot)->terminalExchange(input, columns, rows, readOutput);
    }));
}
}
