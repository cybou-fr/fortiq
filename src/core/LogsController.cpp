#include "LogsController.h"
#include "RemoteController.h"
#include "../ssh/SshClient.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QtConcurrentRun>

#include <algorithm>

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
        if (!m_remote->connected()) { if (m_token != -1) disconnected(); return; }
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
    m_followTimer.stop();
    m_following = false;
    m_busy = false;
    m_token = -1;
    m_state = QStringLiteral("disconnected");
    m_stateText = QStringLiteral("Source déconnectée — affichage figé, suivi en direct arrêté.");
    auto slot = m_session;
    (void)QtConcurrent::run(&m_pool, [slot] { slot->reset(); });
    m_session = std::make_shared<std::unique_ptr<SshSession>>();
    emit changed();
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
    QString unit = o.value("_SYSTEMD_UNIT").toString();
    if (unit.isEmpty() || unit.endsWith(QStringLiteral(".scope"))) unit = o.value("SYSLOG_IDENTIFIER").toString();
    if (unit.isEmpty()) unit = o.value("_COMM").toString();
    return QVariantMap {
        { "time", ms }, { "timeText", timeText(ms) }, { "priority", priority }, { "priorityName", priorityName(priority) },
        { "unit", unit }, { "pid", o.value("_PID").toString() }, { "message", message.left(4000) },
        { "cursor", o.value("__CURSOR").toString() }, { "boot", o.value("_BOOT_ID").toString().left(8) } };
}

void LogsController::loadSources() {
    if (!m_remote->connected()) return;
    const QByteArray script =
        "systemctl list-units --type=service --all --no-legend --plain --no-pager 2>/dev/null"
        " | awk '{print \"U\\t\" $1 \"\\t\" $3 \"\\t\" $4}'\n"
        "find /var/log -maxdepth 2 -type f ! -name '*.gz' ! -name '*.xz' ! -name '*.zst' ! -name '*.journal' ! -name '*.journal~'"
        " \\( -readable -printf 'F\\tr\\t%s\\t%p\\n' -o -printf 'F\\tn\\t%s\\t%p\\n' \\) 2>/dev/null | head -n 400\n";
    const ConnectionRequest req = m_remote->connectionRequest();
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        if (r.status != SshResult::Status::Ok) return;
        QVariantList units, files;
        for (const QByteArray &line : r.data.split('\n')) {
            const QList<QByteArray> f = line.split('\t');
            if (f.size() >= 4 && f[0] == "U" && f[1].endsWith(".service"))
                units.push_back(QVariantMap { { "name", QString::fromUtf8(f[1]) }, { "active", QString::fromUtf8(f[2]) }, { "sub", QString::fromUtf8(f[3]) } });
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
    m_source = source;
    m_target = target;
    if (source == "file") m_maxPriority = 7; // plain files carry no priority
    reload();
}
void LogsController::setRange(const QString &range) { m_range = range; reload(); }
void LogsController::setMaxPriority(int priority) { m_maxPriority = std::clamp(priority, 0, 7); reload(); }

void LogsController::reload() {
    m_cursor.clear();
    m_fileOffset = 0;
    m_fileInode.clear();
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
    QByteArray args = "--no-pager -o json -n " + QByteArray::number(kMaxEvents);
    if (m_source == "user") args += " --user";
    if (m_source == "unit" && !m_target.isEmpty()) args += " -u " + SshSession::shellQuote(m_target);
    if (m_maxPriority < 7) args += " -p 0.." + QByteArray::number(m_maxPriority);
    if (append && !m_cursor.isEmpty()) args += " --after-cursor=" + SshSession::shellQuote(m_cursor);
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
    QByteArray s = "p=" + SshSession::shellQuote(m_target) + "\n"
        "[ -e \"$p\" ] || { echo 'FORTIQ:gone' >&2; exit 4; }\n"
        "[ -r \"$p\" ] || { echo 'FORTIQ:denied' >&2; exit 4; }\n"
        "set -- $(stat -L -c '%i %s' -- \"$p\")\n"
        "echo \"FORTIQ:stat $1 $2\"\n";
    if (append && !m_fileInode.isEmpty()) {
        // Same inode and not shorter: exactly the bytes up to the size just
        // measured (the next read starts there: no gap, no duplicate).
        // Otherwise the file was rotated or truncated.
        s += "if [ \"$1\" = " + SshSession::shellQuote(m_fileInode) + " ] && [ \"$2\" -ge " + QByteArray::number(m_fileOffset) + " ]; then\n"
             "  tail -c +" + QByteArray::number(m_fileOffset + 1) + " -- \"$p\" | head -c $(( $2 - " + QByteArray::number(m_fileOffset) + " ))\n"
             "  exit 0\n"
             "fi\n"
             "echo 'FORTIQ:rotated'\n";
    }
    s += "head -c \"$2\" -- \"$p\" | tail -n " + QByteArray::number(kMaxEvents) + "\n";
    return s;
}

void LogsController::run(bool append) {
    if (!m_remote->connected()) return;
    if (m_source == "file" && m_target.isEmpty()) return;
    const int seq = ++m_seq;
    if (m_token != m_remote->connectionToken()) { m_token = m_remote->connectionToken(); append = false; }
    m_busy = true;
    if (!append) { m_state.clear(); m_stateText.clear(); }
    emit changed();
    const QByteArray script = m_source == "file" ? fileScript(append) : journalScript(append);
    const ConnectionRequest req = m_remote->connectionRequest();
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, append, seq] {
        const SshResult r = watcher->result();
        watcher->deleteLater();
        apply(r, append, seq);
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
        QByteArray data = r.data;
        const int nl = data.indexOf('\n');
        if (data.startsWith("FORTIQ:stat ") && nl > 0) {
            const QList<QByteArray> st = data.left(nl).split(' ');
            if (st.size() >= 3) { m_fileInode = QString::fromLatin1(st[1]); m_fileOffset = st[2].toLongLong(); }
            data.remove(0, nl + 1);
        }
        if (data.startsWith("FORTIQ:rotated\n")) {
            data.remove(0, 15);
            append = false;
            m_state = QStringLiteral("rotated");
            m_stateText = QStringLiteral("Rotation ou troncature détectée : relecture des dernières lignes.");
        }
        // On append, the last partial line is kept for the next read.
        const QList<QByteArray> lines = data.split('\n');
        for (const QByteArray &raw : lines) {
            if (raw.isEmpty()) continue;
            const QString line = QString::fromUtf8(raw).left(4000);
            const double ms = fileLineTime(line);
            fresh.push_back(QVariantMap {
                { "time", ms }, { "timeText", timeText(ms) }, { "priority", -1 }, { "priorityName", priorityName(-1) },
                { "unit", m_target.section('/', -1) }, { "pid", QString() }, { "message", line }, { "cursor", QString() }, { "boot", QString() } });
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
        // Same rule as journalctl: root, adm, systemd-journal (and wheel on
        // some distributions) read the whole system journal.
        bool fullAccess = true;
        for (const QString &line : err.split('\n')) {
            if (!line.startsWith(QStringLiteral("FORTIQ:id "))) continue;
            const QStringList f = line.mid(10).split(' ', Qt::SkipEmptyParts);
            fullAccess = !f.isEmpty() && (f[0] == "0" || f.contains("adm") || f.contains("systemd-journal") || f.contains("wheel"));
        }
        if (m_source != "user" && (!fullAccess || err.contains(QStringLiteral("not seeing messages from other users")))) {
            m_state = QStringLiteral("limited");
            m_stateText = QStringLiteral("Accès limité : seuls les messages de votre utilisateur sont visibles. "
                                         "Les membres des groupes adm ou systemd-journal voient tout le journal.");
        } else if (!append && m_state != "rotated") {
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
    }
    const bool bounded = m_events.size() >= kMaxEvents || r.truncated;
    m_coverage = QStringLiteral("%1 événement%2").arg(m_events.size()).arg(m_events.size() > 1 ? "s" : "")
        + (bounded ? QStringLiteral(" — limite : les %1 plus récents").arg(kMaxEvents) : QString());
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
