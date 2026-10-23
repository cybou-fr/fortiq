#include "UrlTools.h"

#include <QRegularExpression>

namespace fortiq {

namespace {
constexpr int kMaxUrlLength = 2048;

bool hasControlCharacters(const QString &s) {
    for (const QChar c : s) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f) return true;
    }
    return false;
}
}  // namespace

UrlCheck normalizeUrl(const QString &input) {
    UrlCheck out;
    QString text = input.trimmed();
    if (text.isEmpty()) { out.error = QStringLiteral("Saisissez une adresse web."); return out; }
    if (text.size() > kMaxUrlLength) { out.error = QStringLiteral("Adresse trop longue (limite 2048 caractères)."); return out; }
    if (hasControlCharacters(text)) { out.error = QStringLiteral("L'adresse contient des caractères de contrôle."); return out; }
    for (const QChar c : text) {
        if (c.isSpace()) { out.error = QStringLiteral("L'adresse ne peut pas contenir d'espaces."); return out; }
    }

    // A scheme is "letters:" followed by "//" (or a known opaque scheme). "host:8080" is a host with a port.
    static const QRegularExpression scheme(QStringLiteral("^([A-Za-z][A-Za-z0-9+.-]*):"));
    const auto m = scheme.match(text);
    if (m.hasMatch()) {
        const QString name = m.captured(1).toLower();
        const bool authority = text.mid(m.capturedLength()).startsWith(QStringLiteral("//"));
        const bool portLike = QRegularExpression(QStringLiteral("^[A-Za-z0-9.-]+:\\d{1,5}(/|$|\\?|#)")).match(text).hasMatch();
        if (name == QLatin1String("http") || name == QLatin1String("https")) {
            if (!authority) { out.error = QStringLiteral("Adresse incomplète."); return out; }
        } else if (authority || !portLike) {
            out.error = QStringLiteral("Seules les adresses http et https sont autorisées (« %1: » refusé).").arg(name);
            return out;
        } else {
            text = QStringLiteral("https://") + text;   // "host:8080/path"
        }
    } else if (text.startsWith(QStringLiteral("//"))) {
        text = QStringLiteral("https:") + text;
    } else {
        text = QStringLiteral("https://") + text;
    }

    QUrl url(text, QUrl::StrictMode);
    if (!url.isValid()) {
        url = QUrl(text, QUrl::TolerantMode);
        if (!url.isValid()) { out.error = QStringLiteral("Adresse invalide."); return out; }
    }
    const QString schemeName = url.scheme().toLower();
    if (schemeName != QLatin1String("http") && schemeName != QLatin1String("https")) {
        out.error = QStringLiteral("Seules les adresses http et https sont autorisées."); return out;
    }
    if (url.host().isEmpty()) { out.error = QStringLiteral("L'adresse n'a pas de nom d'hôte."); return out; }
    if (!url.userInfo().isEmpty()) {
        out.error = QStringLiteral("N'incluez pas d'identifiants dans l'adresse (utilisateur:mot de passe@hôte).");
        return out;
    }
    if (url.port() != -1 && (url.port() < 1 || url.port() > 65535)) { out.error = QStringLiteral("Port invalide."); return out; }
    url.setScheme(schemeName);
    url.setHost(url.host().toLower());
    url.setFragment(QString());
    if (url.path().isEmpty()) url.setPath(QStringLiteral("/"));
    out.url = url.toString(QUrl::FullyEncoded);
    if (out.url.size() > kMaxUrlLength) { out.error = QStringLiteral("Adresse trop longue (limite 2048 caractères)."); out.url.clear(); return out; }
    out.ok = true;
    return out;
}

QString resolveLink(const QString &base, const QString &href) {
    const QString link = href.trimmed();
    if (link.isEmpty() || link.startsWith(QLatin1Char('#'))) return {};
    if (hasControlCharacters(link)) return {};
    const QUrl baseUrl(base, QUrl::StrictMode);
    if (!baseUrl.isValid()) return {};
    const QUrl target = baseUrl.resolved(QUrl(link, QUrl::TolerantMode));
    if (!target.isValid()) return {};
    const QString s = target.scheme().toLower();
    if (s != QLatin1String("http") && s != QLatin1String("https")) return {};
    const UrlCheck check = normalizeUrl(target.toString(QUrl::FullyEncoded));
    return check.ok ? check.url : QString();
}

}  // namespace fortiq
