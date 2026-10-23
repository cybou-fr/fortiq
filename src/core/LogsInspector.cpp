#include "LogsControllerInternal.h"

namespace fortiq {
QVariantMap LogsController::synthesize(const QVariantList &events, int anchor, int windowSeconds,
                                       bool limited, bool truncated, bool fileSource)
{
    QVariantList observed, inferred, unknown, next;
    const int n = static_cast<int>(events.size());
    auto ev = [&](int i) { return events.at(i).toMap(); };
    auto prio = [&](int i) { return ev(i).value("priority").toInt(); };
    auto t = [&](int i) { return ev(i).value("time").toDouble(); };
    auto isErr = [&](int i) { const int p = prio(i); return p >= 0 && p <= 3; };
    auto isWarn = [&](int i) { return prio(i) == 4; };

    QList<int> errors, warnings;
    for (int i = 0; i < n; ++i) { if (isErr(i)) errors << i; else if (isWarn(i)) warnings << i; }
    const QString window = QStringLiteral("±%1").arg(windowSeconds >= 60 ? QStringLiteral("%1 min").arg(windowSeconds / 60)
                                                                        : QStringLiteral("%1 s").arg(windowSeconds));
    observed << item(QStringLiteral("Fenêtre %1 – %2 (%3) : %4 événement(s), %5 erreur(s), %6 avertissement(s).")
                         .arg(n ? hms(t(0)) : "?", n ? hms(t(n - 1)) : "?", window).arg(n).arg(errors.size()).arg(warnings.size()),
                     errors + warnings);
    if (anchor >= 0 && anchor < n)
        observed << item(QStringLiteral("Événement choisi (%1, %2) : %3")
                             .arg(hms(t(anchor)), ev(anchor).value("unit").toString(), ev(anchor).value("message").toString().left(160)),
                         { anchor });
    if (!errors.isEmpty())
        observed << item(QStringLiteral("Première erreur de la fenêtre à %1 (%2) : %3")
                             .arg(hms(t(errors.first())), ev(errors.first()).value("unit").toString(),
                                  ev(errors.first()).value("message").toString().left(160)),
                         { errors.first() });

    // Affected units, in order of their first error or warning.
    QStringList unitOrder;
    QHash<QString, QList<int>> unitErr, unitWarn;
    for (int i : (errors + warnings)) {
        const QString u = ev(i).value("unit").toString();
        (isErr(i) ? unitErr : unitWarn)[u] << i;
    }
    QList<int> flagged = errors + warnings;
    std::sort(flagged.begin(), flagged.end());
    for (int i : flagged) { const QString u = ev(i).value("unit").toString(); if (!unitOrder.contains(u)) unitOrder << u; }
    for (const QString &u : unitOrder) {
        QList<int> refs = unitErr.value(u) + unitWarn.value(u);
        std::sort(refs.begin(), refs.end());
        observed << item(QStringLiteral("%1 : %2 erreur(s), %3 avertissement(s), de %4 à %5.")
                             .arg(u).arg(unitErr.value(u).size()).arg(unitWarn.value(u).size())
                             .arg(hms(t(refs.first())), hms(t(refs.last()))), refs);
    }

    // Service lifecycle as reported by systemd.
    struct Life { int index; Lifecycle l; };
    QList<Life> lives;
    for (int i = 0; i < n; ++i) if (auto l = lifecycleOf(ev(i))) lives << Life { i, *l };
    for (const Life &x : lives) {
        QString text;
        if (x.l.kind == "restart") text = QStringLiteral("%1 : redémarrage programmé n°%2 à %3.").arg(x.l.unit, x.l.detail, hms(t(x.index)));
        else if (x.l.kind == "failed") text = QStringLiteral("%1 : échec (« %2 ») à %3.").arg(x.l.unit, x.l.detail, hms(t(x.index)));
        else if (x.l.kind == "exited") text = QStringLiteral("%1 : processus principal terminé, statut %2, à %3.").arg(x.l.unit, x.l.detail, hms(t(x.index)));
        else if (x.l.kind == "gaveup") text = QStringLiteral("%1 : redémarrages trop rapprochés, systemd abandonne à %2.").arg(x.l.unit, hms(t(x.index)));
        else if (x.l.kind == "started") text = QStringLiteral("%1 : démarré à %2.").arg(x.l.unit, hms(t(x.index)));
        else text = QStringLiteral("%1 : arrêté à %2.").arg(x.l.unit, hms(t(x.index)));
        observed << item(text, { x.index });
    }

    // Repeats among errors and warnings.
    QHash<QString, QList<int>> groups;
    for (int i : flagged) groups[ev(i).value("unit").toString() + QStringLiteral(" · ") + templateOf(ev(i).value("message").toString())] << i;
    QStringList repeated;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) if (it.value().size() >= 2) repeated << it.key();
    std::sort(repeated.begin(), repeated.end(), [&](const QString &a, const QString &b) { return groups[a].size() > groups[b].size(); });
    for (const QString &k : repeated.mid(0, 5))
        observed << item(QStringLiteral("Répété ×%1 : %2").arg(groups[k].size()).arg(k.left(180)), groups[k]);

    // Hypotheses: time order, restarts after errors, new or vanished patterns.
    if (unitErr.size() >= 2) {
        QStringList withErrors;
        for (const QString &u : unitOrder) if (unitErr.contains(u)) withErrors << u;
        const QString a = withErrors.at(0), b = withErrors.at(1);
        const int ia = unitErr[a].first(), ib = unitErr[b].first();
        inferred << item(QStringLiteral("Hypothèse : les erreurs de %1 (%2) précèdent celles de %3 (%4) de %5 s. "
                                        "Proximité dans le temps seulement, pas une cause démontrée.")
                             .arg(a, hms(t(ia)), b, hms(t(ib))).arg(qRound((t(ib) - t(ia)) / 1000.0)), { ia, ib });
    }
    for (const Life &x : lives) {
        if (x.l.kind != "restart" && x.l.kind != "failed") continue;
        QList<int> before;
        for (int i : errors) {
            const QString u = ev(i).value("unit").toString();
            if (i < x.index && (u == x.l.unit || x.l.unit.startsWith(u + '.') || u.startsWith(x.l.unit.section('.', 0, 0))))
                before << i;
        }
        if (!before.isEmpty())
            inferred << item(QStringLiteral("Hypothèse : %1 de %2 à %3 suit %4 erreur(s) de cette unité dans les %5 s précédentes — "
                                            "réaction probable du gestionnaire de services.")
                                 .arg(x.l.kind == "restart" ? QStringLiteral("le redémarrage") : QStringLiteral("l'échec"),
                                      x.l.unit, hms(t(x.index))).arg(before.size())
                                 .arg(qRound((t(x.index) - t(before.first())) / 1000.0)),
                             before + QList<int> { x.index });
    }
    if (anchor >= 0 && anchor < n) {
        const double at = t(anchor);
        QStringList fresh;
        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            bool beforeSeen = false;
            for (int i : it.value()) beforeSeen |= t(i) < at;
            if (!beforeSeen && it.value().first() != anchor) fresh << it.key();
        }
        for (const QString &k : fresh.mid(0, 3))
            inferred << item(QStringLiteral("Hypothèse : « %1 » n'apparaît qu'après l'événement choisi dans cette fenêtre.").arg(k.left(160)), groups[k]);
    }

    // What remains unknown.
    unknown << item(QStringLiteral("La cause n'est pas déterminée : les événements sont reliés seulement par le temps, l'unité et le texte."), {});
    if (limited) unknown << item(QStringLiteral("Accès limité : les messages du système et des autres utilisateurs ne sont pas visibles."), {});
    if (truncated) unknown << item(QStringLiteral("Plus de 2000 événements dans la fenêtre : seuls les plus récents sont pris en compte."), {});
    if (!fileSource) {
        unknown << item(QStringLiteral("Les messages de niveau debug et les fichiers de /var/log ne sont pas inclus."), {});
    } else {
        unknown << item(QStringLiteral("Fichier texte : pas de niveau de gravité ni d'unité fiables ; l'heure est lue dans chaque ligne quand elle existe."), {});
    }
    if (errors.isEmpty() && warnings.isEmpty())
        unknown << item(QStringLiteral("Aucune erreur ni avertissement visible dans cette fenêtre."), {});

    // Safe, read-only next inspections.
    for (const QString &u : unitOrder.mid(0, 3)) {
        if (u.endsWith(QStringLiteral(".service")))
            next << item(QStringLiteral("Ouvrir le journal de %1 sur 24 h").arg(u), {}, QStringLiteral("unit"), u);
        if (u.endsWith(QStringLiteral(".service")))
            next << item(QStringLiteral("Sur le serveur, en lecture seule : systemctl status %1").arg(u), {});
    }
    if (windowSeconds < 900) next << item(QStringLiteral("Élargir la fenêtre à ±15 min"), {}, QStringLiteral("widen"), QStringLiteral("900"));

    return QVariantMap { { "observed", observed }, { "inferred", inferred }, { "unknown", unknown }, { "next", next } };
}

void LogsController::inspect(const QVariantMap &event, int windowSeconds) {
    m_anchorEvent = event;
    m_inspectorWindow = std::clamp(windowSeconds, 10, 3600);
    m_inspectorOpen = true;
    runInspector();
}
void LogsController::setInspectorWindow(int windowSeconds) {
    if (!m_inspectorOpen) return;
    m_inspectorWindow = std::clamp(windowSeconds, 10, 3600);
    runInspector();
}
void LogsController::closeInspector() {
    ++m_inspectorSeq;
    m_inspectorOpen = false;
    m_inspectorBusy = false;
    m_inspectorEvents.clear();
    m_synopsis.clear();
    emit inspectorChanged();
}

void LogsController::runInspector() {
    const double at = m_anchorEvent.value("time").toDouble();
    const int seq = ++m_inspectorSeq;
    m_inspectorError.clear();
    const bool fileSource = m_source == "file";
    const bool limited = m_state == "limited";
    auto finish = [this](QVariantList events, bool truncated, bool fileSource, bool limited) {
        std::stable_sort(events.begin(), events.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value("time").toDouble() < b.toMap().value("time").toDouble();
        });
        // Anchor: same cursor, else same time and message.
        int anchor = -1;
        const QString cursor = m_anchorEvent.value("cursor").toString();
        for (int i = 0; i < events.size() && anchor < 0; ++i) {
            const QVariantMap e = events.at(i).toMap();
            if ((!cursor.isEmpty() && e.value("cursor").toString() == cursor)
                || (e.value("time") == m_anchorEvent.value("time") && e.value("message") == m_anchorEvent.value("message")))
                anchor = i;
        }
        for (int i = 0; i < events.size(); ++i) { QVariantMap e = events.at(i).toMap(); e.insert("ref", i); events[i] = e; }
        m_inspectorEvents = events;
        m_inspectorAnchor = anchor;
        m_synopsis = synthesize(events, anchor, m_inspectorWindow, limited, truncated, fileSource);
        m_inspectorBusy = false;
        emit inspectorChanged();
    };
    if (at <= 0) {
        m_inspectorError = QStringLiteral("Cet événement n'a pas d'heure lisible : impossible de placer une fenêtre autour.");
        m_inspectorEvents.clear();
        m_synopsis.clear();
        emit inspectorChanged();
        return;
    }
    if (fileSource) {
        QVariantList in;
        for (const QVariant &v : m_events) {
            const double t = v.toMap().value("time").toDouble();
            if (t > 0 && std::abs(t - at) <= m_inspectorWindow * 1000.0) in << v;
        }
        finish(in, false, true, false);
        return;
    }
    if (!m_remote->connected()) return;
    m_inspectorBusy = true;
    emit inspectorChanged();
    const qint64 from = static_cast<qint64>(at / 1000.0) - m_inspectorWindow;
    const qint64 to = static_cast<qint64>(at / 1000.0) + m_inspectorWindow + 1;
    QByteArray args = "--no-pager -o json -n " + QByteArray::number(kMaxEvents) + " -p 0..6"
        " --since=@" + QByteArray::number(from) + " --until=@" + QByteArray::number(to);
    if (m_source == "user" || m_source == "userunit") args += " --user";
    const QByteArray script = "command -v journalctl >/dev/null 2>&1 || { echo 'FORTIQ:missing' >&2; exit 3; }\n"
                              "exec journalctl " + args + "\n";
    const int token = m_remote->connectionToken();
    const ConnectionRequest req = m_remote->connectionRequest();
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, token, seq, finish, limited] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        if (!accepts(token) || seq != m_inspectorSeq) return;
        if (r.status != SshResult::Status::Ok || r.code != "exit:0") {
            m_inspectorBusy = false;
            m_inspectorError = r.message.isEmpty() ? QStringLiteral("Commande distante échouée (%1).").arg(r.code) : r.message;
            emit inspectorChanged();
            return;
        }
        QVariantList events;
        for (const QByteArray &line : r.data.split('\n')) {
            if (line.isEmpty()) continue;
            QVariantMap e = parseJournalLine(line);
            if (!e.isEmpty()) events << e;
        }
        finish(events, events.size() >= kMaxEvents || r.truncated, false, limited);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, script] {
        if (!*slot) *slot = std::make_unique<SshSession>();
        if (!(*slot)->isOpen()) {
            const SshResult opened = (*slot)->open(req);
            if (opened.status != SshResult::Status::Ok) return opened;
        }
        return (*slot)->runScript(script, kMaxOutput);
    }));
}

QString LogsController::inspectorReport() const {
    QString out = QStringLiteral("FORTIQ — inspection d'incident\n");
    out += QStringLiteral("Méthode : règles déterministes sur %1 événement(s) du journal (fenêtre ±%2 s autour de l'événement choisi), sans AI.\n")
               .arg(m_inspectorEvents.size()).arg(m_inspectorWindow);
    const QList<QPair<QString, QString>> sections {
        { "observed", "Observé" }, { "inferred", "Supposé (hypothèses)" }, { "unknown", "Inconnu" }, { "next", "À vérifier ensuite" } };
    for (const auto &s : sections) {
        out += '\n' + s.second + QStringLiteral(" :\n");
        for (const QVariant &v : m_synopsis.value(s.first).toList()) {
            const QVariantMap m = v.toMap();
            QStringList refs;
            for (const QVariant &r : m.value("refs").toList()) refs << '#' + QString::number(r.toInt() + 1);
            out += QStringLiteral("- ") + m.value("text").toString() + (refs.isEmpty() ? QString() : QStringLiteral(" [") + refs.join(", ") + ']') + '\n';
        }
    }
    out += QStringLiteral("\nÉvénements référencés :\n");
    QSet<int> used;
    for (const auto &s : sections)
        for (const QVariant &v : m_synopsis.value(s.first).toList())
            for (const QVariant &r : v.toMap().value("refs").toList()) used.insert(r.toInt());
    QList<int> ordered(used.begin(), used.end());
    std::sort(ordered.begin(), ordered.end());
    for (int i : ordered) {
        if (i < 0 || i >= m_inspectorEvents.size()) continue;
        const QVariantMap e = m_inspectorEvents.at(i).toMap();
        out += QStringLiteral("#%1 %2 %3 [%4] %5\n").arg(i + 1).arg(e.value("timeText").toString(), e.value("unit").toString(),
                                                          e.value("priorityName").toString(), e.value("message").toString());
    }
    return out;
}

}  // namespace fortiq
