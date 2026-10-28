#include "LogsControllerInternal.h"

namespace fortiq {
LogsController::LogsController(RemoteController *remote, QObject *parent)
    : QObject(parent), m_remote(remote), m_session(std::make_shared<std::unique_ptr<SshSession>>())
{
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
    m_followTimer.setInterval(3000);
    connect(&m_followTimer, &QTimer::timeout, this, &LogsController::poll);
    if (m_remote->tasks())
        connect(m_remote->tasks(), &TaskManager::cancelRequested, this, [this](int id) { if (id != 0 && id == m_followTask) setFollowing(false); });
    connect(m_remote, &RemoteController::changed, this, [this] {
        // Results are bound to the server they came from.
        if (!m_remote->connected()) { disconnected(); return; }
        if (m_token != -1 && m_token != m_remote->connectionToken()) disconnected();
    });
}

LogsController::~LogsController() {
    m_followTimer.stop();
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_pool.waitForDone();
}

void LogsController::disconnected() {
    ++m_seq;
    ++m_sourcesSeq;
    ++m_serviceSeq;
    ++m_inspectorSeq;
    m_serviceBusy = false;
    m_serviceInfo.clear(); m_serviceHistory.clear(); m_serviceDays.clear(); m_serviceError.clear();
    m_units.clear(); m_files.clear();
    emit serviceChanged();
    emit sourcesChanged();
    m_followTimer.stop();
    if (m_following) endFollowTask(true);
    m_following = false;
    m_busy = false;
    m_token = -1;
    if (m_inspectorOpen) { m_inspectorBusy = false; emit inspectorChanged(); }
    m_state = QStringLiteral("disconnected");
    m_stateText = m_events.isEmpty() || m_dataLabel.isEmpty()
        ? QStringLiteral("Source déconnectée — affichage figé, suivi en direct arrêté.")
        : QStringLiteral("Source déconnectée — affichage figé des journaux de %1, suivi en direct arrêté.").arg(m_dataLabel);
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_session = std::make_shared<std::unique_ptr<SshSession>>();
    emit changed();
}

bool LogsController::accepts(int token) const {
    return m_remote->connected() && token == m_remote->connectionToken();
}

void LogsController::select(const QString &source, const QString &target) {
    // A live-follow task belongs to one specific source. Do not keep the old
    // task label/identity after switching journals or files.
    const bool sourceChanged = source != m_source || target != m_target;
    if (sourceChanged && m_following) endFollowTask(true);
    ++m_serviceSeq;
    m_serviceBusy = false;
    closeInspector();
    m_source = source;
    m_target = target;
    m_serviceInfo.clear();
    m_serviceHistory.clear();
    m_serviceDays.clear();
    m_serviceError.clear();
    emit serviceChanged();
    if (source == "file") m_maxPriority = 7; // plain files carry no priority
    if (sourceChanged && m_following) beginFollowTask();
    reload();
}
void LogsController::setRange(const QString &range) { m_range = range; reload(); }
void LogsController::setMaxPriority(int priority) { m_maxPriority = std::clamp(priority, 0, 7); reload(); }

void LogsController::reload() {
    m_cursor.clear();
    m_fileOffset = 0;
    m_fileInode.clear();
    m_filePending.clear();
    m_fileSkipLine = false;
    m_fileHistoryLimited = false;
    run(false);
}

void LogsController::setFollowing(bool on) {
    if (on == m_following) return;
    m_following = on && m_remote->connected();
    if (m_following) { m_followTimer.start(); beginFollowTask(); }
    else { m_followTimer.stop(); endFollowTask(true); }
    emit changed();
}

int LogsController::beginRead(const QString &kind, const QString &title) {
    TaskManager *tasks = m_remote->tasks();
    if (!tasks) return 0;
    TaskManager::Spec spec;
    spec.kind = kind;
    spec.title = title;
    spec.source = m_remote->serverLabel();
    spec.generation = m_remote->connectionToken();
    spec.cancellable = false;   // short reads: a newer request or a session change replaces them
    spec.guards = false;
    spec.session = true;
    spec.transient = true;
    return tasks->begin(spec);
}

void LogsController::endRead(int taskId, bool current, const SshResult &r) {
    TaskManager *tasks = m_remote ? m_remote->tasks() : nullptr;
    if (!tasks || taskId == 0) return;
    const QString err = r.message;
    const bool ok = r.status == SshResult::Status::Ok
        && (r.code == "exit:0" || r.truncated || err.contains("FORTIQ:missing") || err.contains("FORTIQ:denied") || err.contains("FORTIQ:gone"));
    if (!current) tasks->markCancelled(taskId, QStringLiteral("Remplacé par une lecture plus récente ou session changée."));
    else if (ok) tasks->succeed(taskId, QString());
    else tasks->fail(taskId, r.message.isEmpty() ? QStringLiteral("Commande distante échouée (%1).").arg(r.code) : r.message);
}

void LogsController::beginFollowTask() {
    TaskManager *tasks = m_remote->tasks();
    if (!tasks || m_followTask != 0) return;
    TaskManager::Spec spec;
    spec.kind = QStringLiteral("logsFollow");
    spec.title = QStringLiteral("Suivi en direct · ") + (m_source == "file" ? m_target : m_source);
    spec.source = m_remote->serverLabel();
    spec.generation = m_remote->connectionToken();
    spec.cancellable = true;   // stopping it stops the follow
    spec.guards = false;
    spec.session = true;
    m_followTask = tasks->begin(spec);
}

void LogsController::endFollowTask(bool cancelled) {
    TaskManager *tasks = m_remote ? m_remote->tasks() : nullptr;
    const int id = std::exchange(m_followTask, 0);
    if (!tasks || id == 0) return;
    if (cancelled) tasks->markCancelled(id, QStringLiteral("Suivi arrêté."));
    else tasks->succeed(id, QStringLiteral("Terminé."));
}

void LogsController::poll() {
    if (!m_busy && m_remote->connected()) run(true);
}

QByteArray LogsController::journalScript(bool append) const {
    // Initial view is a recent tail. Following must read from the cursor
    // forwards: -n would silently drop the oldest events of a large backlog.
    const bool resume = append && !m_cursor.isEmpty();
    QByteArray args = "--no-pager -o json";
    args += resume ? QByteArray(" --no-tail") : " -n " + QByteArray::number(kMaxEvents);
    if (m_source == "user" || m_source == "userunit") args += " --user";
    if ((m_source == "unit" || m_source == "userunit") && !m_target.isEmpty()) args += " -u " + SshSession::shellQuote(m_target);
    if (m_maxPriority < 7) args += " -p 0.." + QByteArray::number(m_maxPriority);
    if (resume) args += " --after-cursor=" + SshSession::shellQuote(m_cursor);
    else if (m_range == "boot") args += " -b";
    else {
        static const QHash<QString, QByteArray> since { { "15min", "-15min" }, { "1h", "-1h" }, { "24h", "-24h" }, { "7d", "-7d" } };
        args += " --since=" + since.value(m_range, "-1h");
    }
    // journalctl's own "not seeing messages" hint is not printed with -o json:
    // report uid and groups so the client can state limited access itself.
    return "command -v journalctl >/dev/null 2>&1 || { echo 'FORTIQ:missing' >&2; exit 3; }\n"
           "echo \"FORTIQ:id $(id -u) $(id -nG)\" >&2\n"
           "exec journalctl " + args + "\n";
}

QByteArray LogsController::fileScript(bool append) const {
    // Open once: rotation cannot switch the inode between stat and read.
    // A page is deliberately smaller than the transport cap; the header gives
    // the byte position of its first byte, never an optimistic next offset.
    QByteArray s = "p=" + SshSession::shellQuote(m_target) + "\n"
        "[ -e \"$p\" ] || { echo 'FORTIQ:gone' >&2; exit 4; }\n"
        "[ -r \"$p\" ] || { echo 'FORTIQ:denied' >&2; exit 4; }\n"
        "exec 3< \"$p\" || { echo 'FORTIQ:denied' >&2; exit 4; }\n"
        "set -- $(stat -L -c '%i %s' /proc/self/fd/3)\n"
        "[ \"$#\" = 2 ] || exit 5\n"
        "start=0; reset=1\n";
    if (append && !m_fileInode.isEmpty())
        s += "if [ \"$1\" = " + SshSession::shellQuote(m_fileInode) + " ] && [ \"$2\" -ge " + QByteArray::number(m_fileOffset) + " ]; then start=" + QByteArray::number(m_fileOffset) + "; reset=0; fi\n";
    s += "if [ \"$reset\" = 1 ] && [ \"$2\" -gt 262144 ]; then start=$(( $2 - 262144 )); fi\n"
         "count=$(( $2 - start )); [ \"$count\" -le 262144 ] || count=262144\n"
         "echo \"FORTIQ:stat $1 $2 $start $reset $count\"\n"
         "tail -c +$(( start + 1 )) <&3 | head -c \"$count\"\n";
    return s;
}

void LogsController::run(bool append) {
    if (!m_remote->connected()) return;
    if (m_source == "file" && m_target.isEmpty()) return;
    const int seq = ++m_seq;
    if (m_token != m_remote->connectionToken()) {
        m_token = m_remote->connectionToken();
        append = false;
        m_cursor.clear();
        m_fileOffset = 0;
        m_fileInode.clear();
        m_filePending.clear();
        m_fileSkipLine = false;
        m_fileHistoryLimited = false;
    }
    m_busy = true;
    if (!append) { m_state.clear(); m_stateText.clear(); }
    const int taskId = append ? 0 : beginRead(QStringLiteral("logsLoad"), QStringLiteral("Lecture des journaux · ") + (m_source == "file" ? m_target : m_source));
    emit changed();
    const QByteArray script = m_source == "file" ? fileScript(append) : journalScript(append);
    const int token = m_remote->connectionToken();
    const ConnectionRequest req = m_remote->connectionRequest();
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, token, append, seq, taskId] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        endRead(taskId, seq == m_seq && accepts(token), r);
        if (accepts(token)) apply(r, append, seq);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, script] {
        if (!*slot) *slot = std::make_unique<SshSession>();
        if (!(*slot)->isOpen()) {
            const SshResult opened = (*slot)->open(req); // pin re-checked
            if (opened.status != SshResult::Status::Ok) return opened;
        }
        return (*slot)->runScript(script, kMaxOutput);
    }));
}

void LogsController::apply(const SshResult &r, bool append, int seq) {
    if (seq != m_seq) return;
    m_busy = false;
    if (r.status != SshResult::Status::Ok) {
        m_state = QStringLiteral("error");
        m_stateText = r.message;
        if (!m_remote->connected()) { disconnected(); return; }
        emit changed();
        return;
    }
    const QString err = r.message;
    if (r.code != "exit:0" && !r.truncated && !err.contains("FORTIQ:missing")
        && !err.contains("FORTIQ:denied") && !err.contains("FORTIQ:gone")) {
        m_state = QStringLiteral("error");
        m_stateText = err.isEmpty() ? QStringLiteral("Commande distante échouée (%1).").arg(r.code) : err;
        emit changed(); return;
    }
    QVariantList fresh;
    if (m_source == "file") {
        if (err.contains("FORTIQ:denied")) {
            m_state = QStringLiteral("denied");
            m_stateText = QStringLiteral("Lecture refusée : %1 n'est pas lisible par cet utilisateur (souvent le groupe adm). Aucune élévation de droits n'est tentée.").arg(m_target);
            m_events.clear(); recompute(); emit changed(); return;
        }
        if (err.contains("FORTIQ:gone")) {
            m_state = QStringLiteral("error");
            m_stateText = QStringLiteral("Le fichier %1 n'existe plus.").arg(m_target);
            m_events.clear(); recompute(); emit changed(); return;
        }
        const int nl = r.data.indexOf('\n');
        const QList<QByteArray> st = r.data.left(nl).split(' ');
        bool sizeOk = false, startOk = false, countOk = false;
        const qint64 size = st.value(2).toLongLong(&sizeOk);
        const qint64 start = st.value(3).toLongLong(&startOk);
        const qint64 count = st.value(5).toLongLong(&countOk);
        const QByteArray data = nl >= 0 ? r.data.mid(nl + 1) : QByteArray();
        if (r.code != "exit:0" || r.truncated || nl < 0 || st.size() != 6
            || st[0] != "FORTIQ:stat" || !sizeOk || !startOk || !countOk
            || start < 0 || size < start || count < 0 || count > 262144
            || count > size - start || data.size() != count
            || (st[4] != "0" && st[4] != "1")
            || (st[4] == "0" && (start != m_fileOffset || QString::fromLatin1(st[1]) != m_fileInode))) {
            m_state = QStringLiteral("error");
            m_stateText = QStringLiteral("Lecture incomplète ou échouée — position conservée, réessayer la lecture.");
            emit changed(); return;
        }
        const bool reset = st[4] == "1";
        const bool rotated = reset && append;
        if (reset) {
            append = false;
            m_filePending.clear();
            m_fileSkipLine = start > 0; // first line of a byte tail can be cut
            m_fileHistoryLimited = start > 0;
        }
        m_fileInode = QString::fromLatin1(st[1]);
        m_fileOffset = start + data.size();
        // Keep at most the displayed prefix of an unfinished line, but consume
        // all its bytes so even a multi-megabyte line cannot stall following.
        qsizetype pos = 0;
        while (pos < data.size()) {
            const qsizetype end = data.indexOf('\n', pos);
            const qsizetype length = (end < 0 ? data.size() : end) - pos;
            if (!m_fileSkipLine)
                m_filePending += data.mid(pos, std::min<qsizetype>(length, 16000 - m_filePending.size()));
            if (end < 0) break;
            if (!m_fileSkipLine && !m_filePending.isEmpty()) {
                const QString line = QString::fromUtf8(m_filePending).left(4000);
                const double ms = fileLineTime(line);
                fresh.push_back(QVariantMap {
                    { "time", ms }, { "timeText", timeText(ms) }, { "priority", -1 }, { "priorityName", priorityName(-1) },
                    { "unit", m_target.section('/', -1) }, { "pid", QString() }, { "message", line }, { "cursor", QString() }, { "boot", QString() } });
                if (fresh.size() >= 2 * kMaxEvents) fresh = fresh.mid(kMaxEvents);
            }
            m_filePending.clear();
            m_fileSkipLine = false;
            pos = end + 1;
        }
        m_state.clear(); m_stateText.clear();
        if (m_fileOffset < size) {
            m_state = QStringLiteral("limited");
            m_stateText = QStringLiteral("Lecture en retard : %1 octets restent à lire. Activer le suivi pour continuer.").arg(size - m_fileOffset);
        } else if (m_fileHistoryLimited) {
            m_state = QStringLiteral("limited");
            m_stateText = QStringLiteral("Vue initiale limitée aux derniers 256 Kio ; la première ligne partielle est ignorée.");
        } else if (rotated) {
            m_state = QStringLiteral("rotated");
            m_stateText = QStringLiteral("Rotation ou troncature détectée : relecture ; les écritures de l'ancien fichier ne sont plus suivies.");
        }
    } else {
        if (err.contains("FORTIQ:missing")) {
            m_state = QStringLiteral("missing");
            m_stateText = QStringLiteral("journalctl n'est pas disponible sur ce serveur (pas de systemd-journald).");
            m_events.clear(); recompute(); emit changed(); return;
        }
        for (const QByteArray &line : r.data.split('\n')) {
            if (line.isEmpty()) continue;
            const QVariantMap e = parseJournalLine(line);
            if (e.isEmpty()) continue;
            fresh.push_back(e);
            m_cursor = e.value("cursor").toString();
        }
        if (r.truncated) {
            m_state = QStringLiteral("limited");
            m_stateText = fresh.isEmpty()
                ? QStringLiteral("Réponse tronquée avant un événement complet — curseur conservé ; un événement peut dépasser la limite de lecture.")
                : QStringLiteral("Réponse tronquée — le suivi reprendra après le dernier événement complet ; couverture incomplète.");
        }
        // Same rule as journalctl: root, adm, systemd-journal (and wheel on
        // some distributions) read the whole system journal.
        bool fullAccess = true;
        for (const QString &line : err.split('\n')) {
            if (!line.startsWith(QStringLiteral("FORTIQ:id "))) continue;
            const QStringList f = line.mid(10).split(' ', Qt::SkipEmptyParts);
            fullAccess = !f.isEmpty() && (f[0] == "0" || f.contains("adm") || f.contains("systemd-journal") || f.contains("wheel"));
        }
        if (m_source != "user" && m_source != "userunit" && (!fullAccess || err.contains(QStringLiteral("not seeing messages from other users")))) {
            m_state = QStringLiteral("limited");
            m_stateText = QStringLiteral("Accès limité : seuls les messages de votre utilisateur sont visibles. "
                                         "Les membres des groupes adm ou systemd-journal voient tout le journal.");
        } else if (!r.truncated && m_state != "rotated") {
            m_state.clear();
            m_stateText.clear();
        }
        const QString realError = QString(err).remove(QRegularExpression(QStringLiteral("FORTIQ:id [^\n]*\n?"))).trimmed();
        if (r.code != "exit:0" && fresh.isEmpty() && !realError.isEmpty() && m_state.isEmpty()) {
            m_state = QStringLiteral("error");
            m_stateText = realError.left(300);
        }
    }
    // Grouping key carried by each event so a pattern can filter the timeline.
    for (QVariant &v : fresh) {
        QVariantMap e = v.toMap();
        e.insert("tkey", e.value("unit").toString() + '' + templateOf(e.value("message").toString()));
        v = e;
    }
    m_dataLabel = m_remote->serverLabel();   // whose events these are (the display is kept after a disconnect or a server change)
    if (append) {
        m_events.append(fresh);
        if (m_events.size() > kMaxEvents) m_events = m_events.mid(m_events.size() - kMaxEvents);
    } else {
        m_events = fresh;
        if (m_events.size() > kMaxEvents) m_events = m_events.mid(m_events.size() - kMaxEvents);
    }
    const bool bounded = m_events.size() >= kMaxEvents || r.truncated;
    m_coverage = QStringLiteral("%1 événement%2").arg(m_events.size()).arg(m_events.size() > 1 ? "s" : "")
        + (bounded ? QStringLiteral(" — vue limitée à %1 événements ; couverture incomplète").arg(kMaxEvents) : QString());
    m_lastUpdate = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    recompute();
    emit changed();
}

void LogsController::recompute() {
    m_patterns = groupPatterns(m_events);
    int buckets[5] = { 0, 0, 0, 0, 0 }; // critique(0-2), erreur, avert., info(5-6), autre(7/unknown)
    for (const QVariant &v : m_events) {
        const int p = v.toMap().value("priority").toInt();
        if (p >= 0 && p <= 2) ++buckets[0];
        else if (p == 3) ++buckets[1];
        else if (p == 4) ++buckets[2];
        else if (p == 5 || p == 6) ++buckets[3];
        else ++buckets[4];
    }
    m_counts = {
        QVariantMap { { "label", "Critique" }, { "count", buckets[0] }, { "level", 2 } },
        QVariantMap { { "label", "Erreurs" }, { "count", buckets[1] }, { "level", 3 } },
        QVariantMap { { "label", "Avertissements" }, { "count", buckets[2] }, { "level", 4 } },
        QVariantMap { { "label", "Info" }, { "count", buckets[3] }, { "level", 6 } },
        QVariantMap { { "label", m_source == "file" ? "Non classé" : "Debug / ?" }, { "count", buckets[4] }, { "level", 7 } } };
}
}  // namespace fortiq
