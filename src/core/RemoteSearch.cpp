#include "RemoteControllerInternal.h"

namespace fortiq {
void RemoteController::startSearch(const QString &query, const QString &mode) {
    const QString q = query.trimmed();
    if (!m_connected || q.isEmpty() || m_currentDirectory.isEmpty()) return;
    m_searchCancel->store(true); // a running search stops; its result is ignored
    m_searchCancel = std::make_shared<std::atomic_bool>(false);
    const int seq = ++m_searchSeq;
    const int token = m_generation;
    m_searchActive = true;
    m_searchBusy = true;
    m_searchQuery = q;
    m_searchMode = mode == "content" ? QStringLiteral("content") : QStringLiteral("name");
    m_searchScope = m_currentDirectory;
    m_searchResults.clear();
    m_searchInfo = QStringLiteral("Recherche…");
    emit searchChanged();

    ConnectionRequest req;
    req.host = m_host; req.port = m_port; req.user = m_user;
    req.password = m_password; req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = QStringLiteral("/");
    auto slot = m_searchSession;
    auto cancel = m_searchCancel;
    const QString scope = m_searchScope, searchMode = m_searchMode;
    QPointer<RemoteController> self(this);
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, seq, token] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        if (token != m_generation || seq != m_searchSeq) return;
        m_searchBusy = false;
        m_searchResults = r.entries;
        const int n = static_cast<int>(r.entries.size());
        if (r.status == SshResult::Status::Ok) {
            m_searchInfo = (n == 0 ? QStringLiteral("Aucun résultat")
                                   : QStringLiteral("%1 résultat%2").arg(n).arg(n > 1 ? "s" : ""))
                + (r.truncated ? QStringLiteral(" — limite atteinte, affinez la recherche") : QString())
                + QStringLiteral("  ·  %1").arg(r.code);
        } else {
            m_searchInfo = r.code == "cancelled" ? QStringLiteral("Recherche annulée — %1 résultat(s) partiel(s)").arg(n)
                                                 : r.message;
        }
        emit searchChanged();
    });
    watcher->setFuture(QtConcurrent::run(&m_searchPool, [slot, req, scope, q, searchMode, cancel, self, seq] {
        if (cancel->load()) return SshResult{};
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult opened = session.open(req); // pin re-checked
            if (opened.status != SshResult::Status::Ok) return opened;
        }
        int lastReported = -1;
        return session.search(scope, q, searchMode, 500, [&](int found) {
            if (found != lastReported) {
                lastReported = found;
                QMetaObject::invokeMethod(self, [self, seq, found] {
                    if (!self || seq != self->m_searchSeq || !self->m_searchBusy) return;
                    self->m_searchInfo = QStringLiteral("Recherche… %1 trouvé(s)").arg(found);
                    emit self->searchChanged();
                }, Qt::QueuedConnection);
            }
            return !cancel->load();
        });
    }));
}

void RemoteController::cancelSearch() {
    m_searchCancel->store(true);
}

void RemoteController::clearSearch() {
    m_searchCancel->store(true);
    ++m_searchSeq;
    m_searchActive = false;
    m_searchBusy = false;
    m_searchResults.clear();
    m_searchInfo.clear();
    m_searchQuery.clear();
    emit searchChanged();
}
}  // namespace fortiq
