#include "LogsControllerInternal.h"

namespace fortiq {
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

}  // namespace fortiq
