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

bool looksLikeSearch(const QString &typed) {
    const QString text = typed.trimmed();
    if (text.isEmpty()) return false;
    for (const QChar c : text)
        if (c.isSpace()) return !text.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) && !text.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
    if (text.startsWith(QLatin1Char('['))) return false;                                   // bracketed IPv6
    if (text.contains(QLatin1Char('.')) || text.contains(QLatin1Char(':')) || text.contains(QLatin1Char('/'))) return false;
    if (text.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) return false;
    return true;
}

QString searchUrl(const QString &engineTemplate, const QString &query) {
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(query.trimmed()));
    QString url = engineTemplate;
    url.replace(QStringLiteral("%s"), encoded);
    return url.size() > kMaxUrlLength ? QString() : url;
}

QString formUrl(const QString &action, const QList<FormValue> &values) {
    const UrlCheck check = normalizeUrl(action);
    if (!check.ok) return {};
    QString url = check.url;
    const int hash = url.indexOf(QLatin1Char('#'));
    if (hash >= 0) url.truncate(hash);
    QStringList parts;
    for (const FormValue &v : values) {
        if (v.name.isEmpty()) continue;
        parts << QString::fromLatin1(QUrl::toPercentEncoding(v.name)) + QLatin1Char('=') + QString::fromLatin1(QUrl::toPercentEncoding(v.value));
    }
    if (!parts.isEmpty()) url += (url.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?')) + parts.join(QLatin1Char('&'));
    return url.size() > kMaxUrlLength ? QString() : url;
}

}  // namespace fortiq
