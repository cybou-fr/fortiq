#include "NetworkInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace fortiq {

QByteArray buildNetSnapshotScript() {
    return QByteArray(
        "export LC_ALL=C\n"
        "sec() { printf '#section %s\\n' \"$1\"; }\n"
        "sec hostname; hostname 2>/dev/null\n"
        "sec addr; ip -j addr 2>/dev/null\n"
        "sec link; ip -j -s link 2>/dev/null\n"
        "sec route4; ip -j route 2>/dev/null\n"
        "sec route6; ip -6 -j route 2>/dev/null\n"
        "sec ss; ss -H -tulpn 2>/dev/null || ss -H -tuln 2>/dev/null\n"
        "sec resolv; cat /etc/resolv.conf 2>/dev/null\n"
        "sec resolved; resolvectl status --no-pager 2>/dev/null | head -80\n"
        "sec tools; for t in ping dig getent tracepath mtr; do command -v \"$t\" >/dev/null 2>&1 && echo \"$t\"; done\n"
        "sec end\n");
}

namespace {
QHash<QString, QByteArray> splitSections(const QByteArray &out) {
    QHash<QString, QByteArray> sections;
    QString current;
    QByteArray body;
    auto flush = [&] { if (!current.isEmpty()) sections.insert(current, body); body.clear(); };
    for (const QByteArray &line : out.split('\n')) {
        if (line.startsWith("#section ")) {
            flush();
            current = QString::fromLatin1(line.mid(9)).trimmed();
        } else if (!current.isEmpty()) {
            body += line;
            body += '\n';
        }
    }
    flush();
    return sections;
}

QJsonArray jsonArray(const QByteArray &bytes) {
    const QByteArray trimmed = bytes.trimmed();
    if (trimmed.isEmpty()) return {};
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &error);
    return error.error == QJsonParseError::NoError && doc.isArray() ? doc.array() : QJsonArray();
}
}  // namespace

QList<NetInterface> parseIpAddrJson(const QByteArray &json) {
    QList<NetInterface> out;
    for (const QJsonValue &v : jsonArray(json)) {
        const QJsonObject o = v.toObject();
        NetInterface n;
        n.name = o.value("ifname").toString();
        if (n.name.isEmpty()) continue;
        n.index = o.value("ifindex").toInt();
        n.state = o.value("operstate").toString();
        n.mtu = o.value("mtu").toInt();
        n.mac = o.value("address").toString();
        n.kind = o.value("link_type").toString();
        for (const QJsonValue &f : o.value("flags").toArray()) n.flags << f.toString();
        for (const QJsonValue &a : o.value("addr_info").toArray()) {
            const QJsonObject ao = a.toObject();
            NetAddress addr;
            addr.family = ao.value("family").toString();
            addr.address = ao.value("local").toString();
            addr.prefix = ao.value("prefixlen").toInt();
            addr.scope = ao.value("scope").toString();
            if (!addr.address.isEmpty()) n.addresses << addr;
        }
        out << n;
    }
    return out;
}

void mergeLinkStats(QList<NetInterface> &interfaces, const QByteArray &json) {
    for (const QJsonValue &v : jsonArray(json)) {
        const QJsonObject o = v.toObject();
        const QString name = o.value("ifname").toString();
        const QJsonObject stats = o.value("stats64").toObject();
        if (stats.isEmpty()) continue;
        for (NetInterface &n : interfaces) {
            if (n.name != name) continue;
            const QJsonObject rx = stats.value("rx").toObject(), tx = stats.value("tx").toObject();
            n.hasStats = true;
            n.rxBytes = static_cast<qint64>(rx.value("bytes").toDouble());
            n.rxPackets = static_cast<qint64>(rx.value("packets").toDouble());
            n.rxErrors = static_cast<qint64>(rx.value("errors").toDouble());
            n.rxDropped = static_cast<qint64>(rx.value("dropped").toDouble());
            n.txBytes = static_cast<qint64>(tx.value("bytes").toDouble());
            n.txPackets = static_cast<qint64>(tx.value("packets").toDouble());
            n.txErrors = static_cast<qint64>(tx.value("errors").toDouble());
            n.txDropped = static_cast<qint64>(tx.value("dropped").toDouble());
        }
    }
}

QList<NetRoute> parseIpRouteJson(const QByteArray &json, const QString &family) {
    QList<NetRoute> out;
    for (const QJsonValue &v : jsonArray(json)) {
        const QJsonObject o = v.toObject();
        NetRoute r;
        r.family = family;
        r.destination = o.value("dst").toString();
        if (r.destination.isEmpty()) continue;
        r.gateway = o.value("gateway").toString();
        r.device = o.value("dev").toString();
        r.protocol = o.value("protocol").toString();
        r.prefsrc = o.value("prefsrc").toString();
        r.metric = o.contains("metric") ? o.value("metric").toInt() : -1;
        r.isDefault = r.destination == QLatin1String("default");
        out << r;
    }
    return out;
}

QList<NetListener> parseSs(const QByteArray &text) {
    // Columns: Netid State Recv-Q Send-Q Local:Port Peer:Port [users:(("name",pid=1,fd=3),...)]
    QList<NetListener> out;
    static const QRegularExpression process(QStringLiteral("\\(\\(\"([^\"]*)\",pid=(\\d+)"));
    for (const QByteArray &raw : text.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty()) continue;
        const QStringList cols = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (cols.size() < 6) continue;
        NetListener l;
        l.protocol = cols.at(0);
        if (l.protocol != QLatin1String("tcp") && l.protocol != QLatin1String("udp")) continue;
        l.state = cols.at(1);
        l.recvQ = cols.at(2).toLongLong();
        l.sendQ = cols.at(3).toLongLong();
        const QString local = cols.at(4);
        const qsizetype colon = local.lastIndexOf(QLatin1Char(':'));
        if (colon < 0) continue;
        l.address = local.left(colon);
        const QString port = local.mid(colon + 1);
        l.port = port == QLatin1String("*") ? 0 : port.toInt();
        if (l.address.startsWith(QLatin1Char('[')) && l.address.endsWith(QLatin1Char(']'))) l.address = l.address.mid(1, l.address.size() - 2);
        const auto m = process.match(line);
        if (m.hasMatch()) { l.process = m.captured(1); l.pid = m.captured(2).toInt(); }
        QString bare = l.address;
        const qsizetype percent = bare.indexOf(QLatin1Char('%'));
        if (percent >= 0) bare = bare.left(percent);
        if (bare == QLatin1String("*") || bare == QLatin1String("0.0.0.0") || bare == QLatin1String("::")) l.exposure = QStringLiteral("all");
        else if (bare.startsWith(QLatin1String("127.")) || bare == QLatin1String("::1")) l.exposure = QStringLiteral("local");
        else l.exposure = QStringLiteral("address");
        out << l;
    }
    return out;
}

void parseResolvConf(const QByteArray &text, QStringList &servers, QStringList &search, QString &note) {
    for (const QByteArray &raw : text.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1Char('#'))) {
            if (line.contains(QLatin1String("generated by WSL"), Qt::CaseInsensitive)) note = QStringLiteral("Généré automatiquement par WSL.");
            else if (line.contains(QLatin1String("systemd-resolved"), Qt::CaseInsensitive) && note.isEmpty()) note = QStringLiteral("Géré par systemd-resolved.");
            else if (line.contains(QLatin1String("NetworkManager"), Qt::CaseInsensitive) && note.isEmpty()) note = QStringLiteral("Géré par NetworkManager.");
            continue;
        }
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() >= 2 && parts.at(0) == QLatin1String("nameserver")) servers << parts.at(1);
        else if (parts.size() >= 2 && (parts.at(0) == QLatin1String("search") || parts.at(0) == QLatin1String("domain"))) search << parts.mid(1);
    }
}

NetSnapshot parseNetSnapshot(const QByteArray &output) {
    NetSnapshot s;
    const auto sections = splitSections(output);
    if (!sections.contains("end")) {
        s.problem = QStringLiteral("Réponse incomplète du serveur.");
        return s;
    }
    s.hostname = QString::fromUtf8(sections.value("hostname")).trimmed();
    s.interfaces = parseIpAddrJson(sections.value("addr"));
    if (s.interfaces.isEmpty()) {
        s.problem = QStringLiteral("`ip -j addr` n'a rien renvoyé : iproute2 absent ou trop ancien (option -j).");
        return s;
    }
    mergeLinkStats(s.interfaces, sections.value("link"));
    s.routes = parseIpRouteJson(sections.value("route4"), QStringLiteral("inet"));
    s.routes += parseIpRouteJson(sections.value("route6"), QStringLiteral("inet6"));
    s.listeners = parseSs(sections.value("ss"));
    if (sections.value("ss").trimmed().isEmpty()) s.notes << QStringLiteral("`ss` n'est pas disponible : ports en écoute inconnus.");
    for (const NetListener &l : s.listeners) if (l.process.isEmpty()) s.processInfoMissing = true;
    if (s.processInfoMissing) s.notes << QStringLiteral("Les noms de processus ne sont visibles que pour votre utilisateur (ou root).");
    parseResolvConf(sections.value("resolv"), s.dnsServers, s.dnsSearch, s.dnsNote);
    s.resolvedText = QString::fromUtf8(sections.value("resolved")).trimmed();
    for (const QByteArray &t : sections.value("tools").split('\n')) if (!t.trimmed().isEmpty()) s.tools << QString::fromLatin1(t.trimmed());
    s.ok = true;
    return s;
}

QString humanBytes(qint64 bytes) {
    if (bytes < 1024) return QStringLiteral("%1 o").arg(bytes);
    const char *units[] = { "Kio", "Mio", "Gio", "Tio" };
    double v = static_cast<double>(bytes);
    int u = -1;
    while (v >= 1024.0 && u < 3) { v /= 1024.0; ++u; }
    return QStringLiteral("%1 %2").arg(QString::number(v, 'f', v < 10 ? 2 : 1), QString::fromLatin1(units[u]));
}

QVariantList interfacesToVariant(const QList<NetInterface> &list) {
    QVariantList out;
    for (const NetInterface &n : list) {
        QVariantList addrs;
        for (const NetAddress &a : n.addresses)
            addrs.push_back(QVariantMap { { "family", a.family }, { "address", a.address }, { "prefix", a.prefix }, { "scope", a.scope },
                                          { "text", a.address + QLatin1Char('/') + QString::number(a.prefix) } });
        out.push_back(QVariantMap {
            { "name", n.name }, { "index", n.index }, { "state", n.state }, { "up", n.flags.contains(QLatin1String("UP")) },
            { "mtu", n.mtu }, { "mac", n.mac }, { "kind", n.kind }, { "flags", n.flags }, { "addresses", addrs },
            { "hasStats", n.hasStats }, { "rx", humanBytes(n.rxBytes) }, { "tx", humanBytes(n.txBytes) },
            { "rxPackets", static_cast<double>(n.rxPackets) }, { "txPackets", static_cast<double>(n.txPackets) },
            { "errors", static_cast<double>(n.rxErrors + n.txErrors) }, { "dropped", static_cast<double>(n.rxDropped + n.txDropped) } });
    }
    return out;
}

QVariantList routesToVariant(const QList<NetRoute> &list) {
    QVariantList out;
    for (const NetRoute &r : list)
        out.push_back(QVariantMap { { "family", r.family }, { "destination", r.destination }, { "gateway", r.gateway }, { "device", r.device },
                                    { "protocol", r.protocol }, { "prefsrc", r.prefsrc }, { "metric", r.metric }, { "isDefault", r.isDefault } });
    return out;
}

QVariantList listenersToVariant(const QList<NetListener> &list) {
    QVariantList out;
    for (const NetListener &l : list)
        out.push_back(QVariantMap { { "protocol", l.protocol }, { "state", l.state }, { "address", l.address }, { "port", l.port },
                                    { "process", l.process }, { "pid", l.pid }, { "exposure", l.exposure },
                                    { "recvQ", static_cast<double>(l.recvQ) }, { "sendQ", static_cast<double>(l.sendQ) } });
    return out;
}

bool isValidProbeTarget(const QString &target) {
    if (target.isEmpty() || target.size() > 253) return false;
    if (target.startsWith(QLatin1Char('-')) || target.startsWith(QLatin1Char('.'))) return false;
    static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9._-]*[A-Za-z0-9])?$"));
    if (name.match(target).hasMatch()) return true;
    // IPv6 literal (optionally with a zone id is NOT accepted: it needs a '%').
    static const QRegularExpression v6(QStringLiteral("^[0-9A-Fa-f:.]+$"));
    const int colons = static_cast<int>(target.count(QLatin1Char(':')));
    return v6.match(target).hasMatch() && colons >= 2 && colons <= 7;
}

QByteArray buildProbeScript(const QString &kind, const QString &target, QString *error) {
    const QString host = target.trimmed();
    if (!isValidProbeTarget(host)) {
        if (error) *error = QStringLiteral("Saisissez un nom d'hôte ou une adresse IP (lettres, chiffres, points, tirets).");
        return {};
    }
    QByteArray s = "export LC_ALL=C\nh='" + host.toUtf8() + "'\n";   // validated: no quote or shell metacharacter can occur
    if (kind == QLatin1String("resolve")) {
        s += "command -v getent >/dev/null 2>&1 || { echo 'getent absent'; exit 3; }\n"
             "echo \"# getent ahosts $h\"; timeout 10 getent ahosts -- \"$h\" 2>&1 || getent ahosts \"$h\" 2>&1 || echo \"Aucune réponse (nom inconnu ou DNS injoignable)\"\n"
             "if command -v dig >/dev/null 2>&1; then echo; echo \"# dig $h\"; timeout 12 dig +time=3 +tries=1 +noall +answer +stats \"$h\" A 2>&1; timeout 12 dig +time=3 +tries=1 +noall +answer \"$h\" AAAA 2>&1; fi\n";
    } else if (kind == QLatin1String("ping")) {
        s += "command -v ping >/dev/null 2>&1 || { echo 'ping absent (apt install iputils-ping)'; exit 3; }\n"
             "timeout 25 ping -c 4 -W 2 \"$h\" 2>&1\n";
    } else if (kind == QLatin1String("trace")) {
        s += "if command -v mtr >/dev/null 2>&1; then timeout 40 mtr -rwn -c 4 \"$h\" 2>&1\n"
             "elif command -v tracepath >/dev/null 2>&1; then timeout 40 tracepath -n -m 15 \"$h\" 2>&1\n"
             "else echo 'ni mtr ni tracepath (apt install iputils-tracepath)'; exit 3; fi\n";
    } else {
        if (error) *error = QStringLiteral("Diagnostic inconnu.");
        return {};
    }
    s += "exit 0\n";
    return s;
}

}  // namespace fortiq
