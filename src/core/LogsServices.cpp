#include "LogsControllerInternal.h"

namespace fortiq {
void LogsController::applyService(const SshResult &r, int token, int seq, bool userScope) {
    if (!accepts(token) || seq != m_serviceSeq) return;
    m_serviceBusy = false;
    if (r.status != SshResult::Status::Ok || r.code != "exit:0" || r.message.contains("FORTIQ:missing")) {
        m_serviceError = r.message.contains("FORTIQ:missing") ? QStringLiteral("systemctl n'est pas disponible sur ce serveur.") : (r.message.isEmpty() ? QStringLiteral("Commande distante échouée (%1).").arg(r.code) : r.message);
        emit serviceChanged();
        return;
    }
    const int show = r.data.indexOf("#show\n"), journal = r.data.indexOf("#journal\n");
    const QByteArray showText = show >= 0 ? r.data.mid(show + 6, (journal > show ? journal : r.data.size()) - show - 6) : QByteArray();
    m_serviceInfo = parseServiceShow(showText);
    QVariantList events;
    if (journal >= 0)
        for (const QByteArray &line : r.data.mid(journal + 9).split('\n'))
            if (!line.isEmpty()) if (QVariantMap e = parseJournalLine(line); !e.isEmpty()) events << e;
    m_serviceHistory = lifecycleHistory(events);
    m_serviceDays = dailyCounts(m_serviceHistory, QDateTime::currentMSecsSinceEpoch());
    bool full = !r.truncated;
    for (const QString &line : r.message.split('\n'))
        if (line.startsWith(QStringLiteral("FORTIQ:id "))) {
            const QStringList f = line.mid(10).split(' ', Qt::SkipEmptyParts);
            full = !r.truncated && !f.isEmpty() && (f[0] == "0" || f.contains("adm") || f.contains("systemd-journal") || f.contains("wheel"));
        }
    m_serviceInfo.insert("historyLimited", !userScope && !full);
    m_serviceInfo.insert("eventsRead", events.size());
    m_serviceInfo.insert("userScope", userScope);
    if (m_serviceInfo.value("load").toString() == "not-found")
        m_serviceError = QStringLiteral("Cette unité n'existe pas sur le serveur.");
    emit serviceChanged();
}

void LogsController::loadService() {
    if (!m_remote->connected() || (m_source != "unit" && m_source != "userunit") || m_target.isEmpty()) return;
    const int seq = ++m_serviceSeq;
    m_serviceBusy = true;
    m_serviceError.clear();
    emit serviceChanged();
    const QByteArray scope = m_source == "userunit" ? " --user" : "";
    const QByteArray props = "Id,Description,LoadState,ActiveState,SubState,UnitFileState,Result,MainPID,NRestarts,Restart,"
                             "ExecMainStatus,FragmentPath,ActiveEnterTimestamp,InactiveEnterTimestamp,MemoryCurrent,CPUUsageNSec,TasksCurrent";
    const QByteArray script = "u=" + SshSession::shellQuote(m_target) + "\n"
        "[ -n \"$XDG_RUNTIME_DIR\" ] || export XDG_RUNTIME_DIR=/run/user/$(id -u)\n"
        "command -v systemctl >/dev/null 2>&1 || { echo 'FORTIQ:missing' >&2; exit 3; }\n"
        "echo '#show'\n"
        "systemctl" + scope + " show \"$u\" --no-pager --timestamp=unix -p " + props + " 2>/dev/null"
        " || systemctl" + scope + " show \"$u\" --no-pager -p " + props + "\n"
        "echo '#journal'\n"
        "echo \"FORTIQ:id $(id -u) $(id -nG)\" >&2\n"
        "command -v journalctl >/dev/null 2>&1 && exec journalctl" + scope + " --no-pager -o json -u \"$u\" --since=-7d -n 5000 -p 0..6\n";
    const int token = m_remote->connectionToken();
    const ConnectionRequest req = m_remote->connectionRequest();
    const bool userScope = m_source == "userunit";
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, token, seq, userScope] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        applyService(r, token, seq, userScope);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, script] {
        if (!*slot) *slot = std::make_unique<SshSession>();
        if (!(*slot)->isOpen()) {
            const SshResult opened = (*slot)->open(req);
            if (opened.status != SshResult::Status::Ok) return opened;
        }
        return (*slot)->runScript(script, 16 * 1024 * 1024);
    }));
}

void LogsController::applySources(const SshResult &r, int token, int seq) {
    if (!accepts(token) || seq != m_sourcesSeq) return;
    if (r.status != SshResult::Status::Ok || r.code != "exit:0" || r.truncated) return;
    QVariantList units, files;
    for (const QByteArray &line : r.data.split('\n')) {
        const QList<QByteArray> f = line.split('\t');
        if (f.size() >= 4 && (f[0] == "U" || f[0] == "V") && f[1].endsWith(".service"))
            units.push_back(QVariantMap { { "name", QString::fromUtf8(f[1]) }, { "active", QString::fromUtf8(f[2]) },
                                          { "sub", QString::fromUtf8(f[3]) }, { "user", f[0] == "V" } });
        else if (f.size() >= 4 && f[0] == "F")
            files.push_back(QVariantMap { { "path", QString::fromUtf8(f[3]) }, { "name", QString::fromUtf8(f[3]).mid(9) },
                                          { "readable", f[1] == "r" }, { "size", f[2].toDouble() } });
    }
    std::sort(units.begin(), units.end(), [](const QVariant &a, const QVariant &b) {
        const bool fa = a.toMap().value("sub") == "failed", fb = b.toMap().value("sub") == "failed";
        if (fa != fb) return fa; // failed services first
        return a.toMap().value("name").toString() < b.toMap().value("name").toString();
    });
    std::sort(files.begin(), files.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("path").toString() < b.toMap().value("path").toString();
    });
    m_units = units;
    m_files = files;
    emit sourcesChanged();
}

void LogsController::loadSources() {
    if (!m_remote->connected()) return;
    const int seq = ++m_sourcesSeq;
    const QByteArray script =
        "systemctl list-units --type=service --all --no-legend --plain --no-pager 2>/dev/null"
        " | awk '{print \"U\\t\" $1 \"\\t\" $3 \"\\t\" $4}'\n"
        "systemctl --user list-units --type=service --all --no-legend --plain --no-pager 2>/dev/null"
        " | awk '{print \"V\\t\" $1 \"\\t\" $3 \"\\t\" $4}'\n"
        "find /var/log -maxdepth 2 -type f ! -name '*.gz' ! -name '*.xz' ! -name '*.zst' ! -name '*.journal' ! -name '*.journal~'"
        " \\( -readable -printf 'F\\tr\\t%s\\t%p\\n' -o -printf 'F\\tn\\t%s\\t%p\\n' \\) 2>/dev/null | head -n 400\n";
    const int token = m_remote->connectionToken();
    const ConnectionRequest req = m_remote->connectionRequest();
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, token, seq] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        applySources(r, token, seq);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, script] {
        if (!*slot) *slot = std::make_unique<SshSession>();
        if (!(*slot)->isOpen()) {
            const SshResult opened = (*slot)->open(req);
            if (opened.status != SshResult::Status::Ok) return opened;
        }
        return (*slot)->runScript(script, 2 * 1024 * 1024);
    }));
}

}  // namespace fortiq
