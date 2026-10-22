#include "LogsController.h"
#include "RemoteController.h"
#include "../ssh/SshClient.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <optional>

namespace fortiq {
namespace {
constexpr int kMaxEvents = 2000;
constexpr qint64 kMaxOutput = 8 * 1024 * 1024;

QString priorityName(int p) {
    switch (p) {
    case 0: return QStringLiteral("urgence");
    case 1: return QStringLiteral("alerte");
    case 2: return QStringLiteral("critique");
    case 3: return QStringLiteral("erreur");
    case 4: return QStringLiteral("avertissement");
    case 5: return QStringLiteral("notice");
    case 6: return QStringLiteral("info");
    case 7: return QStringLiteral("debug");
    default: return QStringLiteral("inconnu");
    }
}
QString timeText(double ms) {
    if (ms <= 0) return QString();
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ms)).toString(QStringLiteral("dd/MM HH:mm:ss"));
}
// Best-effort timestamp of a plain log line: ISO-8601 or syslog "Mon dd hh:mm:ss".
double fileLineTime(const QString &line) {
    static const QRegularExpression iso(QStringLiteral("^\\[?(\\d{4}-\\d{2}-\\d{2}[T ]\\d{2}:\\d{2}:\\d{2})"));
    static const QRegularExpression syslog(QStringLiteral("^([A-Z][a-z]{2}) +(\\d{1,2}) (\\d{2}:\\d{2}:\\d{2})"));
    if (const auto m = iso.match(line); m.hasMatch()) {
        QString t = m.captured(1);
        t[10] = 'T';
        const QDateTime dt = QDateTime::fromString(t, Qt::ISODate);
        if (dt.isValid()) return static_cast<double>(dt.toMSecsSinceEpoch());
    }
    if (const auto m = syslog.match(line); m.hasMatch()) {
        static const QStringList months { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        const int month = static_cast<int>(months.indexOf(m.captured(1))) + 1;
        const QDate today = QDate::currentDate();
        int year = today.year();
        if (month > today.month()) --year; // syslog omits the year
        const QDateTime dt(QDate(year, month, m.captured(2).toInt()), QTime::fromString(m.captured(3), QStringLiteral("HH:mm:ss")));
        if (dt.isValid()) return static_cast<double>(dt.toMSecsSinceEpoch());
    }
    return 0;
}
}

LogsController::LogsController(RemoteController *remote, QObject *parent)
    : QObject(parent), m_remote(remote), m_session(std::make_shared<std::unique_ptr<SshSession>>())
{
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
    m_followTimer.setInterval(3000);
    connect(&m_followTimer, &QTimer::timeout, this, &LogsController::poll);
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
    m_following = false;
    m_busy = false;
    m_token = -1;
    if (m_inspectorOpen) { m_inspectorBusy = false; emit inspectorChanged(); }
    m_state = QStringLiteral("disconnected");
    m_stateText = QStringLiteral("Source déconnectée — affichage figé, suivi en direct arrêté.");
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_session = std::make_shared<std::unique_ptr<SshSession>>();
    emit changed();
}

bool LogsController::accepts(int token) const {
    return m_remote->connected() && token == m_remote->connectionToken();
}

QString LogsController::templateOf(const QString &message) {
    static const QRegularExpression uuid(QStringLiteral("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}"));
    static const QRegularExpression ip(QStringLiteral("\\b\\d{1,3}(?:\\.\\d{1,3}){3}(?::\\d+)?\\b"));
    static const QRegularExpression hex(QStringLiteral("\\b0x[0-9a-fA-F]+\\b|\\b[0-9a-fA-F]{12,}\\b"));
    static const QRegularExpression num(QStringLiteral("\\d+"));
    QString t = message.left(400);
    t.replace(uuid, QStringLiteral("‹id›"));
    t.replace(ip, QStringLiteral("‹ip›"));
    t.replace(hex, QStringLiteral("‹hex›"));
    t.replace(num, QStringLiteral("#"));
    return t.simplified();
}

QVariantList LogsController::groupPatterns(const QVariantList &events) {
    struct Group { QString key, unit, sample, tmpl; int count = 0; int worst = 99; double first = 0, last = 0; };
    QHash<QString, Group> groups;
    for (const QVariant &v : events) {
        const QVariantMap e = v.toMap();
        const QString tmpl = templateOf(e.value("message").toString());
        const QString key = e.value("unit").toString() + '\x1f' + tmpl;
        Group &g = groups[key];
        if (g.count == 0) { g.key = key; g.unit = e.value("unit").toString(); g.sample = e.value("message").toString(); g.tmpl = tmpl; }
        ++g.count;
        const int p = e.value("priority").toInt();
        if (p >= 0 && p < g.worst) g.worst = p;
        const double t = e.value("time").toDouble();
        if (t > 0 && (g.first == 0 || t < g.first)) g.first = t;
        if (t > g.last) g.last = t;
    }
    QList<Group> list = groups.values();
    std::sort(list.begin(), list.end(), [](const Group &a, const Group &b) {
        if (a.count != b.count) return a.count > b.count;
        return a.worst < b.worst;
    });
    QVariantList out;
    for (const Group &g : list.mid(0, 60)) {
        out.push_back(QVariantMap {
            { "key", g.key }, { "template", g.tmpl }, { "sample", g.sample }, { "unit", g.unit },
            { "count", g.count }, { "priority", g.worst == 99 ? -1 : g.worst },
            { "priorityName", priorityName(g.worst == 99 ? -1 : g.worst) },
            { "first", timeText(g.first) }, { "last", timeText(g.last) } });
    }
    return out;
}

QVariantMap LogsController::parseJournalLine(const QByteArray &line) {
    const QJsonObject o = QJsonDocument::fromJson(line).object();
    if (o.isEmpty()) return {};
    QString message;
    const QJsonValue m = o.value("MESSAGE");
    if (m.isString()) message = m.toString();
    else if (m.isArray()) { // binary-safe form: array of bytes
        QByteArray bytes;
        for (const QJsonValue &b : m.toArray()) bytes.append(static_cast<char>(b.toInt()));
        message = QString::fromUtf8(bytes);
    }
    const double ms = o.value("__REALTIME_TIMESTAMP").toString().toDouble() / 1000.0;
    const int priority = o.contains("PRIORITY") ? o.value("PRIORITY").toString().toInt() : -1;
    // A user service runs inside user@UID.service: its own name is the user unit.
    QString unit = o.value("_SYSTEMD_USER_UNIT").toString();
    if (unit.isEmpty()) unit = o.value("_SYSTEMD_UNIT").toString();
    if (unit.isEmpty() || unit.endsWith(QStringLiteral(".scope"))) unit = o.value("SYSLOG_IDENTIFIER").toString();
    if (unit.isEmpty()) unit = o.value("_COMM").toString();
    return QVariantMap {
        { "time", ms }, { "timeText", timeText(ms) }, { "priority", priority }, { "priorityName", priorityName(priority) },
        { "unit", unit }, { "pid", o.value("_PID").toString() }, { "message", message.left(4000) },
        { "cursor", o.value("__CURSOR").toString() }, { "boot", o.value("_BOOT_ID").toString().left(8) },
        { "subject", o.value("UNIT").toString().isEmpty() ? o.value("USER_UNIT").toString() : o.value("UNIT").toString() } };
}

namespace {
QString hms(double ms) {
    return ms > 0 ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ms)).toString(QStringLiteral("HH:mm:ss")) : QStringLiteral("?");
}
QVariantMap item(const QString &text, const QList<int> &refs, const QString &action = {}, const QString &arg = {}) {
    QVariantList r;
    for (int i : refs.mid(0, 40)) r << i;
    QVariantMap m { { "text", text }, { "refs", r } };
    if (!action.isEmpty()) { m.insert("action", action); m.insert("arg", arg); }
    return m;
}
struct Lifecycle { QString kind, unit, detail; };
// systemd's own unit messages (English; other locales are simply not classified).
std::optional<Lifecycle> lifecycleOf(const QVariantMap &e) {
    static const QRegularExpression restart(QStringLiteral("Scheduled restart job, restart counter is at (\\d+)"));
    static const QRegularExpression failed(QStringLiteral("Failed with result '([^']+)'"));
    static const QRegularExpression exited(QStringLiteral("Main process exited, code=(\\w+), status=([^\\s]+)"));
    static const QRegularExpression started(QStringLiteral("^Started (.+?)\\.?$"));
    static const QRegularExpression stopped(QStringLiteral("^Stopped (.+?)\\.?$"));
    static const QRegularExpression tooQuick(QStringLiteral("Start request repeated too quickly"));
    const QString msg = e.value("message").toString();
    QString unit = e.value("subject").toString();
    auto pick = [&](const QString &fallback) { return unit.isEmpty() ? fallback : unit; };
    if (auto m = restart.match(msg); m.hasMatch()) return Lifecycle { "restart", pick(e.value("unit").toString()), m.captured(1) };
    if (auto m = tooQuick.match(msg); m.hasMatch()) return Lifecycle { "gaveup", pick(e.value("unit").toString()), {} };
    if (auto m = failed.match(msg); m.hasMatch()) return Lifecycle { "failed", pick(e.value("unit").toString()), m.captured(1) };
    if (auto m = exited.match(msg); m.hasMatch()) return Lifecycle { "exited", pick(e.value("unit").toString()), m.captured(2) };
    if (!unit.isEmpty()) {
        if (auto m = started.match(msg); m.hasMatch()) return Lifecycle { "started", unit, {} };
        if (auto m = stopped.match(msg); m.hasMatch()) return Lifecycle { "stopped", unit, {} };
    }
    return std::nullopt;
}
}

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

QVariantMap LogsController::parseServiceShow(const QByteArray &text) {
    QHash<QString, QString> kv;
    for (const QByteArray &line : text.split('\n')) {
        const int eq = line.indexOf('=');
        if (eq > 0) kv.insert(QString::fromUtf8(line.left(eq)), QString::fromUtf8(line.mid(eq + 1)).trimmed());
    }
    auto ts = [&](const QString &key) -> double {
        const QString v = kv.value(key);
        return v.startsWith('@') ? v.mid(1).toDouble() * 1000.0 : 0;
    };
    const double since = kv.value("ActiveState") == "active" ? ts("ActiveEnterTimestamp") : ts("InactiveEnterTimestamp");
    const QString mem = kv.value("MemoryCurrent");
    const QString cpu = kv.value("CPUUsageNSec");
    const bool memSet = !mem.isEmpty() && mem[0].isDigit() && mem.size() < 19;
    const bool cpuSet = !cpu.isEmpty() && cpu[0].isDigit() && cpu.size() < 19;
    return QVariantMap {
        { "id", kv.value("Id") }, { "description", kv.value("Description") },
        { "load", kv.value("LoadState") }, { "active", kv.value("ActiveState") }, { "sub", kv.value("SubState") },
        { "enabled", kv.value("UnitFileState") }, { "result", kv.value("Result") },
        { "pid", kv.value("MainPID") == "0" ? QString() : kv.value("MainPID") },
        { "restarts", kv.value("NRestarts").toInt() }, { "restartPolicy", kv.value("Restart") },
        { "exitStatus", kv.value("ExecMainStatus") }, { "fragment", kv.value("FragmentPath") },
        { "since", since }, { "sinceText", timeText(since) },
        { "memory", memSet ? QLocale(QLocale::French).formattedDataSize(mem.toLongLong(), 1, QLocale::DataSizeTraditionalFormat) : QString() },
        { "cpu", cpuSet ? QStringLiteral("%1 s").arg(cpu.toDouble() / 1e9, 0, 'f', 1) : QString() },
        { "tasks", kv.value("TasksCurrent").size() < 10 && !kv.value("TasksCurrent").isEmpty() && kv.value("TasksCurrent")[0].isDigit()
                     ? kv.value("TasksCurrent") : QString() } };
}

QVariantList LogsController::lifecycleHistory(const QVariantList &events) {
    QVariantList out;
    for (const QVariant &v : events) {
        const QVariantMap e = v.toMap();
        const auto l = lifecycleOf(e);
        if (!l) continue;
        static const QHash<QString, QString> labels {
            { "started", "Démarré" }, { "stopped", "Arrêté" }, { "exited", "Processus terminé" },
            { "failed", "Échec" }, { "restart", "Redémarrage programmé" }, { "gaveup", "Redémarrages abandonnés" } };
        out.push_front(QVariantMap {
            { "time", e.value("time") }, { "timeText", e.value("timeText") }, { "kind", l->kind },
            { "label", labels.value(l->kind) }, { "detail", l->detail }, { "message", e.value("message") } });
    }
    return out;
}

QVariantList LogsController::dailyCounts(const QVariantList &history, qint64 nowMs) {
    // Seven local days ending today: starts, failures, restarts.
    const QDate today = QDateTime::fromMSecsSinceEpoch(nowMs).date();
    QVariantList days;
    for (int back = 6; back >= 0; --back) {
        const QDate d = today.addDays(-back);
        int starts = 0, failures = 0, restarts = 0;
        for (const QVariant &v : history) {
            const QVariantMap h = v.toMap();
            if (QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(h.value("time").toDouble())).date() != d) continue;
            const QString k = h.value("kind").toString();
            if (k == "started") ++starts;
            else if (k == "failed" || k == "gaveup") ++failures;
            else if (k == "restart") ++restarts;
        }
        days << QVariantMap { { "label", d.toString(QStringLiteral("dd/MM")) }, { "starts", starts },
                              { "failures", failures }, { "restarts", restarts } };
    }
    return days;
}

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

void LogsController::select(const QString &source, const QString &target) {
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
    if (m_following) m_followTimer.start(); else m_followTimer.stop();
    emit changed();
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
    emit changed();
    const QByteArray script = m_source == "file" ? fileScript(append) : journalScript(append);
    const int token = m_remote->connectionToken();
    const ConnectionRequest req = m_remote->connectionRequest();
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, token, append, seq] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
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
}
