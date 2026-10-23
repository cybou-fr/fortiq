#include "UrlPolicy.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace fortiq {

namespace {
QByteArray shellSingleQuote(const QString &value) {
    QByteArray v = value.toUtf8();
    v.replace("'", "'\\''");
    return "'" + v + "'";
}

bool inV4(quint32 a, quint32 net, int bits) {
    const quint32 mask = bits == 0 ? 0 : (0xFFFFFFFFu << (32 - bits));
    return (a & mask) == (net & mask);
}
quint32 v4(int a, int b, int c, int d) { return (quint32(a) << 24) | (quint32(b) << 16) | (quint32(c) << 8) | quint32(d); }

AddressVerdict verdict(AddressClass cls, const char *label) { return { cls, QString::fromUtf8(label) }; }

AddressVerdict classify4(quint32 a) {
    // Metadata services first: they are the reason for the whole policy.
    if (a == v4(169, 254, 169, 254) || a == v4(169, 254, 170, 2) || a == v4(100, 100, 100, 200)) return verdict(AddressClass::Blocked, "métadonnées du cloud");
    if (inV4(a, v4(0, 0, 0, 0), 8)) return verdict(AddressClass::Blocked, "adresse non routable (0.0.0.0/8)");
    if (inV4(a, v4(169, 254, 0, 0), 16)) return verdict(AddressClass::Blocked, "adresse locale de lien (169.254/16)");
    if (inV4(a, v4(224, 0, 0, 0), 4)) return verdict(AddressClass::Blocked, "multidiffusion");
    if (inV4(a, v4(240, 0, 0, 0), 4)) return verdict(AddressClass::Blocked, "adresse réservée ou de diffusion");
    if (inV4(a, v4(127, 0, 0, 0), 8)) return verdict(AddressClass::Internal, "boucle locale (localhost)");
    if (inV4(a, v4(10, 0, 0, 0), 8) || inV4(a, v4(172, 16, 0, 0), 12) || inV4(a, v4(192, 168, 0, 0), 16)) return verdict(AddressClass::Internal, "réseau privé");
    if (inV4(a, v4(100, 64, 0, 0), 10)) return verdict(AddressClass::Internal, "réseau partagé de l'opérateur (CGNAT)");
    if (inV4(a, v4(192, 0, 0, 0), 24) || inV4(a, v4(192, 0, 2, 0), 24) || inV4(a, v4(198, 51, 100, 0), 24) || inV4(a, v4(203, 0, 113, 0), 24)
        || inV4(a, v4(198, 18, 0, 0), 15) || inV4(a, v4(192, 88, 99, 0), 24))
        return verdict(AddressClass::Internal, "plage réservée (documentation, test ou relais)");
    return verdict(AddressClass::Public, "publique");
}

AddressVerdict classifyHost(const QHostAddress &addr) {
    if (addr.isNull()) return verdict(AddressClass::Blocked, "adresse illisible");
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) return classify4(addr.toIPv4Address());
    if (addr.protocol() != QAbstractSocket::IPv6Protocol) return verdict(AddressClass::Blocked, "adresse illisible");
    const Q_IPV6ADDR b = addr.toIPv6Address();
    auto zero = [&](int from, int to) { for (int i = from; i < to; ++i) if (b.c[i] != 0) return false; return true; };
    auto embedded = [&](int at) { return classify4(v4(b.c[at], b.c[at + 1], b.c[at + 2], b.c[at + 3])); };
    if (zero(0, 16)) return verdict(AddressClass::Blocked, "adresse non spécifiée (::)");
    if (zero(0, 15) && b.c[15] == 1) return verdict(AddressClass::Internal, "boucle locale (localhost)");
    if (zero(0, 10) && b.c[10] == 0xff && b.c[11] == 0xff) return embedded(12);                       // ::ffff:a.b.c.d
    if (zero(0, 12)) return embedded(12);                                                           // ::a.b.c.d (obsolete)
    if (b.c[0] == 0x00 && b.c[1] == 0x64 && b.c[2] == 0xff && b.c[3] == 0x9b && zero(4, 12)) return embedded(12);   // 64:ff9b::/96
    if (b.c[0] == 0x20 && b.c[1] == 0x02) return embedded(2);                                       // 6to4
    if (b.c[0] == 0xff) return verdict(AddressClass::Blocked, "multidiffusion IPv6");
    if (b.c[0] == 0xfe && (b.c[1] & 0xc0) == 0x80) return verdict(AddressClass::Blocked, "adresse locale de lien IPv6");
    if (b.c[0] == 0xfd && b.c[1] == 0x00 && b.c[2] == 0x0e && b.c[3] == 0xc2 && zero(4, 14) && b.c[14] == 0x02 && b.c[15] == 0x54)
        return verdict(AddressClass::Blocked, "métadonnées du cloud");                                // fd00:ec2::254
    if ((b.c[0] & 0xfe) == 0xfc) return verdict(AddressClass::Internal, "réseau privé IPv6 (ULA)");
    if (b.c[0] == 0xfe && (b.c[1] & 0xc0) == 0xc0) return verdict(AddressClass::Internal, "site-local IPv6 (obsolète)");
    if (b.c[0] == 0x20 && b.c[1] == 0x01 && b.c[2] == 0x0d && b.c[3] == 0xb8) return verdict(AddressClass::Internal, "plage réservée (documentation)");
    if (b.c[0] == 0x20 && b.c[1] == 0x01 && b.c[2] == 0x00 && b.c[3] == 0x00) return verdict(AddressClass::Internal, "tunnel Teredo");
    return verdict(AddressClass::Public, "publique");
}

QString className(AddressClass c) {
    return c == AddressClass::Public ? QStringLiteral("public") : c == AddressClass::Internal ? QStringLiteral("internal") : QStringLiteral("blocked");
}
}  // namespace

AddressVerdict classifyAddress(const QString &ip) {
    QString text = ip.trimmed();
    const qsizetype zone = text.indexOf(QLatin1Char('%'));
    if (zone >= 0) text = text.left(zone);
    QHostAddress addr;
    if (!addr.setAddress(text)) return verdict(AddressClass::Blocked, "adresse illisible");
    return classifyHost(addr);
}

PolicyDecision decideAddresses(const QStringList &resolvedIps) {
    PolicyDecision d;
    if (resolvedIps.isEmpty()) { d.reason = QStringLiteral("Le nom n'a aucune adresse."); return d; }
    bool anyInternal = false;
    QStringList blocked, internal;
    for (const QString &ip : resolvedIps) {
        const AddressVerdict v = classifyAddress(ip);
        d.details.push_back(QVariantMap { { "ip", ip }, { "label", v.label }, { "cls", className(v.cls) } });
        if (v.cls == AddressClass::Blocked) blocked << QStringLiteral("%1 (%2)").arg(ip, v.label);
        else if (v.cls == AddressClass::Internal) { anyInternal = true; internal << QStringLiteral("%1 (%2)").arg(ip, v.label); }
    }
    if (!blocked.isEmpty()) {
        d.kind = PolicyDecision::Kind::Block;
        d.reason = QStringLiteral("Adresse bloquée : %1. Ces adresses ne sont jamais ouvertes depuis le serveur (elles donnent accès à ses métadonnées et à son réseau local).").arg(blocked.join(QStringLiteral(", ")));
        return d;
    }
    d.pinIp = resolvedIps.first();
    if (anyInternal) {
        d.kind = PolicyDecision::Kind::Confirm;
        d.reason = QStringLiteral("Cette adresse est interne au serveur : %1. Le serveur contacterait un service qui n'est pas public.").arg(internal.join(QStringLiteral(", ")));
        // The pinned address is an internal one when any is: the user confirms exactly what will be contacted.
        for (const QString &ip : resolvedIps) if (classifyAddress(ip).cls == AddressClass::Internal) { d.pinIp = ip; break; }
        return d;
    }
    d.kind = PolicyDecision::Kind::Allow;
    return d;
}

HostPlan planHost(const QString &normalizedUrl) {
    HostPlan p;
    const QUrl url(normalizedUrl, QUrl::StrictMode);
    if (!url.isValid()) { p.error = QStringLiteral("Adresse invalide."); return p; }
    QString host = url.host(QUrl::FullyEncoded).toLower();
    if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']'))) host = host.mid(1, host.size() - 2);
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) { p.error = QStringLiteral("Seules les adresses http et https sont autorisées."); return p; }
    p.port = url.port(scheme == QLatin1String("https") ? 443 : 80);
    if (host.isEmpty() || host.size() > 253) { p.error = QStringLiteral("Nom d'hôte invalide."); return p; }
    if (host.contains(QLatin1Char('%'))) { p.error = QStringLiteral("Adresse IPv6 avec zone refusée."); return p; }
    p.host = host;

    QHostAddress literal;
    if (host.contains(QLatin1Char(':'))) {                       // IPv6 literal
        if (!literal.setAddress(host)) { p.error = QStringLiteral("Adresse IPv6 invalide."); return p; }
        p.literalIp = true;
        p.ip = literal.toString();
        p.ok = true;
        return p;
    }
    static const QRegularExpression onlyDigitsDots(QStringLiteral("^[0-9.]+$"));
    if (onlyDigitsDots.match(host).hasMatch()) {                 // IPv4 literal: strict dotted decimal, nothing else
        const QStringList parts = host.split(QLatin1Char('.'));
        bool canonical = parts.size() == 4;
        for (const QString &part : parts) {
            bool ok = false;
            const int n = part.toInt(&ok);
            if (!ok || n < 0 || n > 255 || part.size() > 3 || (part.size() > 1 && part.startsWith(QLatin1Char('0')))) canonical = false;
        }
        if (!canonical) { p.error = QStringLiteral("Adresse IP non canonique (formes décimale, octale, hexadécimale ou abrégée refusées)."); return p; }
        literal.setAddress(host);
        p.literalIp = true;
        p.ip = literal.toString();
        p.ok = true;
        return p;
    }
    // A name: letters, digits, '-', '_' and dots; no empty label; the last label is not numeric and no label looks like 0x...
    static const QRegularExpression nameRe(QStringLiteral("^[a-z0-9_]([a-z0-9_.-]*[a-z0-9_])?$"));
    if (!nameRe.match(host).hasMatch() || host.contains(QLatin1String(".."))) { p.error = QStringLiteral("Nom d'hôte invalide."); return p; }
    const QStringList labels = host.split(QLatin1Char('.'));
    static const QRegularExpression numeric(QStringLiteral("^[0-9]+$"));
    if (numeric.match(labels.last()).hasMatch()) { p.error = QStringLiteral("Adresse IP non canonique (le dernier élément du nom est numérique)."); return p; }
    for (const QString &label : labels)
        if (label.startsWith(QLatin1String("0x"))) { p.error = QStringLiteral("Adresse IP non canonique (forme hexadécimale)."); return p; }
    static const QSet<QString> metadataNames {
        QStringLiteral("metadata"), QStringLiteral("metadata.google.internal"), QStringLiteral("instance-data"), QStringLiteral("instance-data.ec2.internal"),
        QStringLiteral("metadata.tencentyun.com"), QStringLiteral("metadata.azure.com"), QStringLiteral("metadata.packet.net") };
    p.blockedName = metadataNames.contains(host);
    p.ok = !p.blockedName;
    if (p.blockedName) p.error = QStringLiteral("Nom bloqué : service de métadonnées du cloud.");
    return p;
}

QByteArray buildResolveScript(const QString &host) {
    return "command -v getent >/dev/null 2>&1 || { printf 'FORTIQ-ERR missing-getent\\n'; exit 3; }\n"
           "getent ahosts " + shellSingleQuote(host) + " 2>/dev/null | awk '{print $1}' | awk '!seen[$0]++' | head -n 8\n";
}

QStringList parseResolveOutput(const QByteArray &output, QString *problem) {
    QStringList ips;
    if (output.startsWith("FORTIQ-ERR ")) { if (problem) *problem = QString::fromLatin1(output.mid(11)).trimmed(); return ips; }
    for (const QByteArray &raw : output.split('\n')) {
        QString text = QString::fromLatin1(raw).trimmed();
        const qsizetype zone = text.indexOf(QLatin1Char('%'));
        if (zone >= 0) text = text.left(zone);
        QHostAddress a;
        if (text.isEmpty() || !a.setAddress(text)) continue;
        const QString canonical = a.toString();
        if (!ips.contains(canonical)) ips << canonical;
    }
    return ips;
}

QString curlResolveSpec(const QString &host, int port, const QString &ip) {
    if (host.isEmpty() || ip.isEmpty()) return {};
    return QStringLiteral("%1:%2:%3").arg(host).arg(port).arg(ip.contains(QLatin1Char(':')) ? QStringLiteral("[%1]").arg(ip) : ip);
}

}  // namespace fortiq
