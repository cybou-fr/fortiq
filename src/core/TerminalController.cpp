#include "TerminalController.h"
#include "RemoteController.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <utility>

namespace fortiq {
TerminalController::TerminalController(RemoteController *remote, QObject *parent)
    : QObject(parent), m_remote(remote), m_session(std::make_shared<std::unique_ptr<SshSession>>()) {
    m_pool.setMaxThreadCount(1); m_pool.setExpiryTimeout(-1);
    m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, &TerminalController::poll);
    connect(remote, &RemoteController::changed, this, [this] {
        if (m_serverToken >= 0 && (!m_remote->connected() || m_remote->connectionToken() != m_serverToken)) {
            stop(); emit resetScreen();
            m_status = QStringLiteral("Source SSH déconnectée ou changée. Rouvrez explicitement le terminal."); emit changed();
        }
    });
}
TerminalController::~TerminalController() {
    m_timer.stop(); auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); }); m_pool.waitForDone();
}
void TerminalController::stop() {
    ++m_seq; m_timer.stop(); m_input.clear(); m_running = m_busy = false; m_serverToken = -1;
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_session = std::make_shared<std::unique_ptr<SshSession>>();
    m_status = QStringLiteral("Terminal fermé. Aucun rejeu automatique."); emit changed();
}
void TerminalController::start() {
    if (m_busy || m_running || !m_remote->connected()) return;
    m_serverToken = m_remote->connectionToken(); const int seq = ++m_seq;
    const auto req = m_remote->connectionRequest(); auto slot = m_session;
    const int columns = m_columns, rows = m_rows;
    m_busy = true; m_status = QStringLiteral("Ouverture SSH / PTY…"); emit resetScreen(); emit changed();
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, seq] {
        const auto r = watcher->result(); watcher->deleteLater();
        if (seq != m_seq) return;
        m_busy = false; m_running = r.status == SshResult::Status::Ok;
        m_status = m_running ? QStringLiteral("Shell interactif distant · SSH") : r.message;
        if (m_running) m_timer.start(); else m_serverToken = -1;
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, columns, rows] {
        *slot = std::make_unique<SshSession>();
        auto r = (*slot)->open(req); // pinned fingerprint checked before authentication
        if (r.status == SshResult::Status::Ok) r = (*slot)->startTerminal(columns, rows);
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
        stop(); m_status = QStringLiteral("Entrée trop volumineuse : terminal fermé sans rejeu."); emit changed(); return;
    }
    m_input += data;
}
void TerminalController::poll() {
    if (!m_running || m_busy) return;
    if (!m_remote->connected() || m_remote->connectionToken() != m_serverToken) { stop(); emit resetScreen(); return; }
    m_busy = true; const int seq = m_seq; auto slot = m_session;
    const auto input = std::exchange(m_input, {}); const int columns = m_columns, rows = m_rows;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, seq] {
        const auto r = watcher->result(); watcher->deleteLater(); if (seq != m_seq) return;
        m_busy = false;
        if (!r.data.isEmpty()) emit output(r.data);
        if (r.status != SshResult::Status::Ok || !r.code.isEmpty()) {
            stop(); m_status = r.status == SshResult::Status::Ok ? QStringLiteral("Shell terminé (%1).").arg(r.code) : r.message;
            emit changed();
        }
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, input, columns, rows] {
        if (!*slot) return SshResult{};
        return (*slot)->terminalExchange(input, columns, rows);
    }));
}
}
