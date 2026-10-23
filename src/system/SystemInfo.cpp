#include "SystemInfo.h"
#include "../core/SafeCast.h"

#include <QHash>
#include <QSet>
#include <QRegularExpression>

namespace fortiq {

QByteArray buildSystemSnapshotScript() {
    // ps prints the command line last because it may contain spaces; user is widened so names are never cut.
    return QByteArray(
        "export LC_ALL=C\n"
        "sec() { printf '#section %s\\n' \"$1\"; }\n"
        "sec hostname; hostname 2>/dev/null\n"
        "sec os; ( . /etc/os-release 2>/dev/null && echo \"$PRETTY_NAME\" )\n"
        "sec kernel; uname -r 2>/dev/null\n"
        "sec loadavg; cat /proc/loadavg 2>/dev/null\n"
        "sec uptime; cat /proc/uptime 2>/dev/null\n"
        "sec meminfo; grep -E '^(MemTotal|MemFree|MemAvailable|Buffers|Cached|SwapTotal|SwapFree):' /proc/meminfo 2>/dev/null\n"
        "sec nproc; nproc 2>/dev/null\n"
        "sec ps; ps -eo pid=,ppid=,user:32=,stat=,pcpu=,pmem=,rss=,etimes=,nlwp=,args= --sort=-pcpu 2>/dev/null | head -n 3001\n"
        "if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then\n"
        "  sec systemd; echo yes\n"
        "  sec running; systemctl is-system-running 2>/dev/null\n"
        "  sec services; systemctl list-units --type=service --all --no-pager --no-legend --plain 2>/dev/null | head -n 2000\n"
        "else sec systemd; echo no; fi\n"
        "sec end\n");
}

namespace {
QHash<QString, QByteArray> sections(const QByteArray &out) {
    QHash<QString, QByteArray> map;
    QString current;
    QByteArray body;
    auto flush = [&] { if (!current.isEmpty()) map.insert(current, body); body.clear(); };
    for (const QByteArray &line : out.split('\n')) {
        if (line.startsWith("#section ")) { flush(); current = QString::fromLatin1(line.mid(9)).trimmed(); }
        else if (!current.isEmpty()) { body += line; body += '\n'; }
    }
    flush();
    return map;
}
}  // namespace

QList<ProcessInfo> parsePs(const QByteArray &text) {
    // pid ppid user stat %cpu %mem rss etimes nlwp args...
    QList<ProcessInfo> out;
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    for (const QByteArray &raw : text.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty()) continue;
        // Split off the nine fixed fields; what remains is the command line, spaces included.
        QStringList fields;
        qsizetype pos = 0;
        for (int i = 0; i < 9; ++i) {
            while (pos < line.size() && line.at(pos).isSpace()) ++pos;
            const qsizetype start = pos;
            while (pos < line.size() && !line.at(pos).isSpace()) ++pos;
            if (start == pos) break;
            fields << line.mid(start, pos - start);
        }
        if (fields.size() < 9) continue;
        bool okPid = false, okPpid = false;
        ProcessInfo p;
        p.pid = fields.at(0).toInt(&okPid);
        p.ppid = fields.at(1).toInt(&okPpid);
        if (!okPid || !okPpid || p.pid <= 0) continue;
        p.user = fields.at(2);
        p.state = fields.at(3);
        p.cpu = fields.at(4).toDouble();
        p.mem = fields.at(5).toDouble();
        p.rssKb = fields.at(6).toLongLong();
        p.elapsedSec = fields.at(7).toLongLong();
        p.threads = qMax(1, fields.at(8).toInt());
        p.command = line.mid(pos).trimmed();
        if (p.command.startsWith(QLatin1Char('['))) {
            // "[kthreadd]" is a kernel thread; "[sh] <defunct>" is a zombie of a normal process.
            p.kernelThread = !p.command.contains(QLatin1String("<defunct>"));
            p.name = p.command.mid(1, p.command.indexOf(QLatin1Char(']')) - 1);
        } else {
            QString first = p.command.section(QLatin1Char(' '), 0, 0);
            // "-bash" login shells, "/usr/bin/python3 script.py": show the executable, but a script name is more useful for interpreters.
            if (first.startsWith(QLatin1Char('-'))) first = first.mid(1);
            p.name = first.section(QLatin1Char('/'), -1);
            static const QSet<QString> interpreters { "python", "python3", "perl", "ruby", "node", "bash", "sh", "dash", "php", "java" };
            if (interpreters.contains(p.name)) {
                const QStringList parts = p.command.split(ws, Qt::SkipEmptyParts);
                for (int i = 1; i < parts.size(); ++i) {
                    if (parts.at(i).startsWith(QLatin1Char('-'))) continue;
                    p.name = p.name + QStringLiteral(" ") + parts.at(i).section(QLatin1Char('/'), -1);
                    break;
                }
            }
            if (p.name.isEmpty()) p.name = p.command.left(40);
        }
        out << p;
    }
    return out;
}

QList<ServiceInfo> parseServices(const QByteArray &text) {
    QList<ServiceInfo> out;
    for (const QByteArray &raw : text.split('\n')) {
        QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty()) continue;
        // Some systemd versions still prefix problem units with a bullet.
        if (line.startsWith(QChar(0x25CF)) || line.startsWith(QLatin1Char('*'))) line = line.mid(1).trimmed();
        QStringList fields;
        qsizetype pos = 0;
        for (int i = 0; i < 4; ++i) {
            while (pos < line.size() && line.at(pos).isSpace()) ++pos;
            const qsizetype start = pos;
            while (pos < line.size() && !line.at(pos).isSpace()) ++pos;
            if (start == pos) break;
            fields << line.mid(start, pos - start);
        }
        if (fields.size() < 4 || !fields.at(0).contains(QLatin1Char('.'))) continue;
        ServiceInfo s;
        s.unit = fields.at(0); s.load = fields.at(1); s.active = fields.at(2); s.sub = fields.at(3);
        s.description = line.mid(pos).trimmed();
        out << s;
    }
    return out;
}

void parseOverviewSection(const QString &name, const QByteArray &body, SystemOverview &o) {
    const QString text = QString::fromUtf8(body).trimmed();
    if (name == QLatin1String("hostname")) o.hostname = text;
    else if (name == QLatin1String("os")) o.os = text;
    else if (name == QLatin1String("kernel")) o.kernel = text;
    else if (name == QLatin1String("nproc")) o.cores = text.toInt();
    else if (name == QLatin1String("running")) o.systemdState = text;
    else if (name == QLatin1String("loadavg")) {
        const QStringList p = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (p.size() >= 3) { o.load1 = p.at(0).toDouble(); o.load5 = p.at(1).toDouble(); o.load15 = p.at(2).toDouble(); }
    } else if (name == QLatin1String("uptime")) {
        o.uptimeSec = toInt64(text.section(QLatin1Char(' '), 0, 0).toDouble());
    } else if (name == QLatin1String("meminfo")) {
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const QString key = line.section(QLatin1Char(':'), 0, 0);
            const qint64 kb = line.section(QLatin1Char(':'), 1).trimmed().section(QLatin1Char(' '), 0, 0).toLongLong();
            if (key == QLatin1String("MemTotal")) o.memTotalKb = kb;
            else if (key == QLatin1String("MemFree")) o.memFreeKb = kb;
            else if (key == QLatin1String("MemAvailable")) o.memAvailableKb = kb;
            else if (key == QLatin1String("Buffers")) o.buffersKb = kb;
            else if (key == QLatin1String("Cached")) o.cachedKb = kb;
            else if (key == QLatin1String("SwapTotal")) o.swapTotalKb = kb;
            else if (key == QLatin1String("SwapFree")) o.swapFreeKb = kb;
        }
    }
}

SystemSnapshot parseSystemSnapshot(const QByteArray &output) {
    SystemSnapshot s;
    const auto map = sections(output);
    if (!map.contains("end")) { s.problem = QStringLiteral("Réponse incomplète du serveur."); return s; }
    for (const char *name : { "hostname", "os", "kernel", "nproc", "running", "loadavg", "uptime", "meminfo" })
        parseOverviewSection(QString::fromLatin1(name), map.value(name), s.overview);
    s.processes = parsePs(map.value("ps"));
    if (s.processes.isEmpty()) { s.problem = QStringLiteral("`ps` n'a renvoyé aucun processus (outil absent ou non pris en charge)."); return s; }
    s.processesTruncated = s.processes.size() >= kMaxProcesses;
    if (s.processesTruncated) s.notes << QStringLiteral("Liste limitée aux %1 processus les plus actifs.").arg(kMaxProcesses);
    s.hasSystemd = QString::fromUtf8(map.value("systemd")).trimmed() == QLatin1String("yes");
    if (s.hasSystemd) {
        s.services = parseServices(map.value("services"));
        if (s.services.isEmpty()) s.notes << QStringLiteral("systemctl n'a listé aucune unité de service.");
    } else {
        s.notes << QStringLiteral("Ce serveur n'utilise pas systemd : pas de liste de services.");
    }
    s.ok = true;
    return s;
}

QString humanKb(qint64 kb) {
    if (kb < 1024) return QStringLiteral("%1 Kio").arg(kb);
    const double mb = kb / 1024.0;
    if (mb < 1024) return QStringLiteral("%1 Mio").arg(QString::number(mb, 'f', mb < 10 ? 1 : 0));
    const double gb = mb / 1024.0;
    return QStringLiteral("%1 Gio").arg(QString::number(gb, 'f', gb < 10 ? 2 : 1));
}

QString humanDuration(qint64 s) {
    if (s < 0) s = 0;
    const qint64 d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    if (d > 0) return QStringLiteral("%1 j %2 h").arg(d).arg(h);
    if (h > 0) return QStringLiteral("%1 h %2 min").arg(h).arg(m);
    if (m > 0) return QStringLiteral("%1 min %2 s").arg(m).arg(s % 60);
    return QStringLiteral("%1 s").arg(s);
}

QVariantList processesToVariant(const QList<ProcessInfo> &list) {
    QVariantList out;
    for (const ProcessInfo &p : list)
        out.push_back(QVariantMap {
            { "pid", p.pid }, { "ppid", p.ppid }, { "user", p.user }, { "state", p.state }, { "cpu", p.cpu }, { "mem", p.mem },
            { "rssKb", static_cast<double>(p.rssKb) }, { "rss", humanKb(p.rssKb) }, { "elapsed", humanDuration(p.elapsedSec) },
            { "elapsedSec", static_cast<double>(p.elapsedSec) }, { "threads", p.threads }, { "command", p.command }, { "name", p.name },
            { "kernel", p.kernelThread } });
    return out;
}

QVariantList servicesToVariant(const QList<ServiceInfo> &list) {
    QVariantList out;
    for (const ServiceInfo &s : list)
        out.push_back(QVariantMap { { "unit", s.unit }, { "load", s.load }, { "active", s.active }, { "sub", s.sub }, { "description", s.description },
                                    { "failed", s.active == QLatin1String("failed") } });
    return out;
}

QVariantMap overviewToVariant(const SystemOverview &o, int processCount, int running, int zombies, int failedServices, int activeServices, int serviceCount) {
    const qint64 used = o.memTotalKb > 0 ? o.memTotalKb - (o.memAvailableKb > 0 ? o.memAvailableKb : o.memFreeKb) : 0;
    const qint64 swapUsed = o.swapTotalKb - o.swapFreeKb;
    return QVariantMap {
        { "hostname", o.hostname }, { "os", o.os }, { "kernel", o.kernel }, { "cores", o.cores },
        { "load1", o.load1 }, { "load5", o.load5 }, { "load15", o.load15 },
        { "loadRatio", o.cores > 0 ? o.load1 / o.cores : 0.0 },
        { "uptime", humanDuration(o.uptimeSec) }, { "uptimeSec", static_cast<double>(o.uptimeSec) },
        { "memTotal", humanKb(o.memTotalKb) }, { "memUsed", humanKb(used) }, { "memAvailable", humanKb(o.memAvailableKb) },
        { "memFraction", o.memTotalKb > 0 ? static_cast<double>(used) / o.memTotalKb : 0.0 },
        { "swapTotal", humanKb(o.swapTotalKb) }, { "swapUsed", humanKb(swapUsed) },
        { "swapFraction", o.swapTotalKb > 0 ? static_cast<double>(swapUsed) / o.swapTotalKb : 0.0 },
        { "systemdState", o.systemdState },
        { "processes", processCount }, { "running", running }, { "zombies", zombies },
        { "services", serviceCount }, { "activeServices", activeServices }, { "failedServices", failedServices } };
}

}  // namespace fortiq
