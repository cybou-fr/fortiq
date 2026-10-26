#include "HttpWire.h"

#include "../../core/SecureWipe.h"

#include <QHostAddress>
#include <QStringList>

#include <algorithm>

namespace fortiq::gateway {

namespace {
bool isTokenChar(char c) {
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    return QByteArrayView("!#$%&'*+-.^_`|~").contains(c);
}
bool isToken(const QByteArray &s) {
    if (s.isEmpty()) return false;
    for (const char c : s) if (!isTokenChar(c)) return false;
    return true;
}
QByteArray trimmed(const QByteArray &s) {
    qsizetype a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.mid(a, b - a);
}
template <class H> QByteArray firstHeader(const H &h, const char *name) {
    for (const Header &x : h.headers) if (iequals(x.name, name)) return x.value;
    return {};
}
template <class H> int countHeader(const H &h, const char *name) {
    int n = 0;
    for (const Header &x : h.headers) if (iequals(x.name, name)) ++n;
    return n;
}
template <class H> bool headerHasToken(const H &h, const char *name, const char *token) {
    for (const Header &x : h.headers) {
        if (!iequals(x.name, name)) continue;
        for (const QByteArray &part : x.value.split(','))
            if (iequals(trimmed(part), token)) return true;
    }
    return false;
}

// Splits the head into lines; every line end is exactly CRLF and no line holds NUL, CR or LF.
bool splitLines(const QByteArray &head, QList<QByteArray> *lines, QString *error) {
    qsizetype start = 0;
    const qsizetype size = head.size();
    for (qsizetype i = 0; i < size; ++i) {
        const char c = head[i];
        if (c == '\0') { *error = QStringLiteral("NUL dans l'en-tête."); return false; }
        if (c == '\r') {
            if (i + 1 >= size || head[i + 1] != '\n') { *error = QStringLiteral("CR isolé dans l'en-tête."); return false; }
            lines->push_back(head.mid(start, i - start));
            ++i;
            start = i + 1;
        } else if (c == '\n') {
            *error = QStringLiteral("LF isolé dans l'en-tête.");
            return false;
        }
    }
    if (start < size) { *error = QStringLiteral("En-tête incomplet."); return false; }
    return true;
}

bool parseHeaderLines(const QList<QByteArray> &lines, qsizetype from, QList<Header> *out, QString *error) {
    for (qsizetype i = from; i < lines.size(); ++i) {
        const QByteArray &line = lines[i];
        if (line.isEmpty()) break;
        if (line.size() > kMaxHeaderLine) { *error = QStringLiteral("Ligne d'en-tête trop longue."); return false; }
        if (line[0] == ' ' || line[0] == '\t') { *error = QStringLiteral("Repli de ligne (obsolete line folding) refusé."); return false; }
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) { *error = QStringLiteral("Ligne d'en-tête sans nom."); return false; }
        Header h;
        h.name = line.left(colon);
        if (!isToken(h.name)) { *error = QStringLiteral("Nom d'en-tête invalide."); return false; }
        h.value = trimmed(line.mid(colon + 1));
        if (out->size() >= kMaxHeaderCount) { *error = QStringLiteral("Trop d'en-têtes."); return false; }
        out->push_back(h);
    }
    return true;
}

HeadParse locateHead(const QByteArray &buffer, QByteArray *head) {
    HeadParse r;
    const qsizetype end = buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        if (buffer.size() > kMaxHeadBytes) { r.status = ParseStatus::TooLarge; r.error = QStringLiteral("En-tête trop grand."); }
        return r;
    }
    if (end + 4 > kMaxHeadBytes) { r.status = ParseStatus::TooLarge; r.error = QStringLiteral("En-tête trop grand."); return r; }
    *head = buffer.left(end + 4);
    r.consumed = end + 4;
    r.status = ParseStatus::Ok;
    return r;
}

HeadParse bad(const QString &why) { HeadParse r; r.status = ParseStatus::Bad; r.error = why; return r; }

bool validTarget(const QByteArray &t) {
    if (t.isEmpty() || t.size() > 4096) return false;
    for (const unsigned char c : t) if (c <= 0x20 || c == 0x7f) return false;
    return true;
}
}  // namespace

QByteArray RequestHead::header(const char *name) const { return firstHeader(*this, name); }
int RequestHead::count(const char *name) const { return countHeader(*this, name); }
bool RequestHead::hasToken(const char *name, const char *token) const { return headerHasToken(*this, name, token); }
QByteArray ResponseHead::header(const char *name) const { return firstHeader(*this, name); }
int ResponseHead::count(const char *name) const { return countHeader(*this, name); }
bool ResponseHead::hasToken(const char *name, const char *token) const { return headerHasToken(*this, name, token); }

HeadParse parseRequestHead(const QByteArray &buffer, RequestHead *out) {
    QByteArray head;
    HeadParse located = locateHead(buffer, &head);
    if (located.status != ParseStatus::Ok) return located;
    QList<QByteArray> lines;
    QString error;
    if (!splitLines(head, &lines, &error)) return bad(error);
    if (lines.isEmpty() || lines[0].isEmpty()) return bad(QStringLiteral("Ligne de requête vide."));
    const QList<QByteArray> parts = lines[0].split(' ');
    if (parts.size() != 3) return bad(QStringLiteral("Ligne de requête invalide."));
    RequestHead h;
    h.method = parts[0];
    h.target = parts[1];
    h.version = parts[2];
    if (!isToken(h.method) || h.method.size() > 16) return bad(QStringLiteral("Méthode invalide."));
    if (!validTarget(h.target)) return bad(QStringLiteral("Cible invalide."));
    if (h.version != "HTTP/1.1" && h.version != "HTTP/1.0") return bad(QStringLiteral("Version HTTP non prise en charge."));
    if (!parseHeaderLines(lines, 1, &h.headers, &error)) return bad(error);
    *out = h;
    return located;
}

HeadParse parseResponseHead(const QByteArray &buffer, ResponseHead *out) {
    QByteArray head;
    HeadParse located = locateHead(buffer, &head);
    if (located.status != ParseStatus::Ok) return located;
    QList<QByteArray> lines;
    QString error;
    if (!splitLines(head, &lines, &error)) return bad(error);
    if (lines.isEmpty()) return bad(QStringLiteral("Ligne d'état vide."));
    const QByteArray &line = lines[0];
    const qsizetype sp1 = line.indexOf(' ');
    if (sp1 < 0) return bad(QStringLiteral("Ligne d'état invalide."));
    ResponseHead h;
    h.version = line.left(sp1);
    if (h.version != "HTTP/1.1" && h.version != "HTTP/1.0") return bad(QStringLiteral("Réponse non HTTP/1.x."));
    const QByteArray rest = line.mid(sp1 + 1);
    if (rest.size() < 3) return bad(QStringLiteral("Code d'état invalide."));
    bool okCode = false;
    h.status = rest.left(3).toInt(&okCode);
    if (!okCode || h.status < 100 || h.status > 599 || (rest.size() > 3 && rest[3] != ' ')) return bad(QStringLiteral("Code d'état invalide."));
    h.reason = rest.size() > 4 ? rest.mid(4) : QByteArray();
    if (!parseHeaderLines(lines, 1, &h.headers, &error)) return bad(error);
    *out = h;
    return located;
}

Authority parseAuthority(const QByteArray &text, int defaultPort) {
    Authority a;
    QByteArray hostPart;
    QByteArray portPart;
    if (text.startsWith('[')) {
        const qsizetype close = text.indexOf(']');
        if (close < 0) { a.error = QStringLiteral("Adresse IPv6 invalide."); return a; }
        hostPart = text.mid(1, close - 1);
        const QByteArray rest = text.mid(close + 1);
        if (rest.startsWith(':')) portPart = rest.mid(1);
        else if (!rest.isEmpty()) { a.error = QStringLiteral("Autorité invalide."); return a; }
        QHostAddress v6;
        if (!v6.setAddress(QString::fromLatin1(hostPart)) || v6.protocol() != QAbstractSocket::IPv6Protocol) {
            a.error = QStringLiteral("Adresse IPv6 invalide."); return a;
        }
    } else {
        const qsizetype colon = text.lastIndexOf(':');
        if (colon >= 0) { hostPart = text.left(colon); portPart = text.mid(colon + 1); }
        else hostPart = text;
        for (const char c : hostPart) {
            const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '_';
            if (!ok) { a.error = QStringLiteral("Caractère interdit dans le nom d'hôte."); return a; }
        }
    }
    if (hostPart.isEmpty() || hostPart.size() > 253) { a.error = QStringLiteral("Nom d'hôte invalide."); return a; }
    int port = defaultPort;
    if (!portPart.isEmpty()) {
        bool ok = false;
        for (const char c : portPart) if (c < '0' || c > '9') { a.error = QStringLiteral("Port invalide."); return a; }
        port = portPart.size() <= 5 ? portPart.toInt(&ok) : 0;
        if (!ok || port < 1 || port > 65535) { a.error = QStringLiteral("Port invalide."); return a; }
    } else if (defaultPort <= 0) {
        a.error = QStringLiteral("Port manquant.");
        return a;
    }
    a.host = QString::fromLatin1(hostPart).toLower();
    a.port = port;
    a.ok = true;
    return a;
}

HttpTarget parseAbsoluteHttp(const QByteArray &uri) {
    HttpTarget t;
    static const QByteArray scheme = "http://";
    if (uri.size() < scheme.size() || uri.left(scheme.size()).compare(scheme, Qt::CaseInsensitive) != 0) {
        t.error = uri.startsWith("https://") ? QStringLiteral("Une adresse https doit passer par CONNECT.") : QStringLiteral("Adresse absolue http attendue.");
        return t;
    }
    const QByteArray rest = uri.mid(scheme.size());
    qsizetype cut = rest.size();
    for (qsizetype i = 0; i < rest.size(); ++i) if (rest[i] == '/' || rest[i] == '?' || rest[i] == '#') { cut = i; break; }
    const QByteArray authority = rest.left(cut);
    if (authority.contains('@')) { t.error = QStringLiteral("Identifiants dans l'adresse refusés."); return t; }
    const Authority a = parseAuthority(authority, 80);
    if (!a.ok) { t.error = a.error; return t; }
    QByteArray path = rest.mid(cut);
    const qsizetype frag = path.indexOf('#');
    if (frag >= 0) path.truncate(frag);
    if (path.isEmpty()) path = "/";
    else if (path[0] == '?') path.prepend('/');
    t.host = a.host;
    t.port = a.port;
    t.path = path;
    t.ok = true;
    return t;
}

QByteArray basicAuthValue(const QByteArray &user, const QByteArray &token) {
    return "Basic " + (user + ":" + token).toBase64();
}

bool proxyAuthorized(RequestHead *head, const QByteArray &expected) {
    // Constant time over the longer of the two: no early exit on the first differing byte.
    bool present = false;
    bool equal = false;
    for (Header &h : head->headers) {
        if (!iequals(h.name, "Proxy-Authorization")) continue;
        if (!present) {
            present = true;
            const QByteArray &got = h.value;
            unsigned diff = static_cast<unsigned>(got.size() ^ expected.size());
            const qsizetype n = (std::max)(got.size(), expected.size());
            for (qsizetype i = 0; i < n; ++i) {
                const unsigned char a = i < got.size() ? static_cast<unsigned char>(got[i]) : 0;
                const unsigned char b = i < expected.size() ? static_cast<unsigned char>(expected[i]) : 0;
                diff |= static_cast<unsigned>(a ^ b);
            }
            equal = diff == 0;
        } else {
            equal = false;   // two credentials: ambiguous, refuse
        }
        wipe(h.value);
    }
    return present && equal;
}

// ---------------------------------------------------------------- BodyFramer
void BodyFramer::reset(Mode mode, qint64 length) {
    m_mode = mode;
    m_failed = false;
    m_remaining = length;
    m_chunk = Chunk::SizeLine;
    m_line.clear();
    m_dataEnd = 0;
    m_trailerLine = 0;
    m_done = mode == Mode::None || (mode == Mode::Length && length <= 0);
}

qsizetype BodyFramer::consume(const QByteArray &data, qsizetype offset) {
    if (m_done || m_failed) return 0;
    const qsizetype avail = data.size() - offset;
    if (avail <= 0) return 0;
    switch (m_mode) {
    case Mode::None: return 0;
    case Mode::UntilClose: return avail;
    case Mode::Length: {
        const qsizetype take = static_cast<qsizetype>((std::min<qint64>)(m_remaining, avail));
        m_remaining -= take;
        if (m_remaining == 0) m_done = true;
        return take;
    }
    case Mode::Chunked: break;
    }
    qsizetype i = offset;
    const qsizetype end = data.size();
    while (i < end && !m_done) {
        switch (m_chunk) {
        case Chunk::SizeLine: {
            const char c = data[i++];
            if (c == '\n') {
                QByteArray line = m_line;
                m_line.clear();
                if (line.endsWith('\r')) line.chop(1);
                const qsizetype semi = line.indexOf(';');
                if (semi >= 0) line.truncate(semi);
                line = line.trimmed();
                bool ok = false;
                const qint64 size = line.isEmpty() || line.size() > 15 ? -1 : line.toLongLong(&ok, 16);
                if (!ok || size < 0) { m_failed = true; return i - offset; }
                if (size == 0) { m_chunk = Chunk::Trailer; m_trailerLine = 0; }
                else { m_remaining = size; m_chunk = Chunk::Data; }
            } else {
                m_line.append(c);
                if (m_line.size() > 4096) { m_failed = true; return i - offset; }
            }
            break;
        }
        case Chunk::Data: {
            const qsizetype take = static_cast<qsizetype>((std::min<qint64>)(m_remaining, end - i));
            i += take;
            m_remaining -= take;
            if (m_remaining == 0) { m_chunk = Chunk::DataEnd; m_dataEnd = 0; }
            break;
        }
        case Chunk::DataEnd: {
            const char c = data[i++];
            if (m_dataEnd == 0 && c == '\r') m_dataEnd = 1;
            else if (m_dataEnd == 1 && c == '\n') { m_chunk = Chunk::SizeLine; m_dataEnd = 0; }
            else { m_failed = true; return i - offset; }
            break;
        }
        case Chunk::Trailer: {
            // Trailer fields end with an empty line. "\r\n" alone = end of the message.
            const char c = data[i++];
            if (c == '\n') {
                if (m_trailerLine <= 1) m_done = true;   // empty line (just the CR, or nothing)
                m_trailerLine = 0;
            } else {
                ++m_trailerLine;
                if (m_trailerLine > kMaxHeaderLine) { m_failed = true; return i - offset; }
            }
            break;
        }
        }
    }
    return i - offset;
}

// ---------------------------------------------------------------- framing decisions
static bool parseLength(const QByteArray &v, qint64 *out) {
    if (v.isEmpty() || v.size() > 15) return false;
    for (const char c : v) if (c < '0' || c > '9') return false;
    *out = v.toLongLong();
    return true;
}

Framing requestFraming(const RequestHead &head) {
    Framing f;
    const bool te = head.count("Transfer-Encoding") > 0;
    const int cl = head.count("Content-Length");
    if (te) {
        if (cl > 0) { f.ok = false; f.error = QStringLiteral("Content-Length et Transfer-Encoding ensemble : refusé."); return f; }
        if (head.count("Transfer-Encoding") != 1 || !iequals(head.header("Transfer-Encoding").trimmed(), "chunked")) {
            f.ok = false; f.error = QStringLiteral("Transfer-Encoding autre que chunked : refusé."); return f;
        }
        f.mode = BodyFramer::Mode::Chunked;
        return f;
    }
    if (cl > 0) {
        qint64 len = 0;
        QByteArray first;
        for (const Header &h : head.headers) {
            if (!iequals(h.name, "Content-Length")) continue;
            for (const QByteArray &part : h.value.split(',')) {   // "5, 5" is tolerated only if all are equal
                const QByteArray p = part.trimmed();
                if (first.isEmpty()) first = p;
                else if (first != p) { f.ok = false; f.error = QStringLiteral("Content-Length contradictoires."); return f; }
            }
        }
        if (!parseLength(first, &len)) { f.ok = false; f.error = QStringLiteral("Content-Length invalide."); return f; }
        f.mode = len > 0 ? BodyFramer::Mode::Length : BodyFramer::Mode::None;
        f.length = len;
        return f;
    }
    return f;
}

Framing responseFraming(const ResponseHead &head, const QByteArray &requestMethod) {
    Framing f;
    if (requestMethod == "HEAD" || (head.status >= 100 && head.status < 200) || head.status == 204 || head.status == 304) return f;
    if (head.count("Transfer-Encoding") > 0) {
        if (head.hasToken("Transfer-Encoding", "chunked")) { f.mode = BodyFramer::Mode::Chunked; return f; }
        f.mode = BodyFramer::Mode::UntilClose;
        return f;
    }
    if (head.count("Content-Length") > 0) {
        qint64 len = 0;
        QByteArray first;
        for (const Header &h : head.headers) {
            if (!iequals(h.name, "Content-Length")) continue;
            const QByteArray p = h.value.trimmed();
            if (first.isEmpty()) first = p;
            else if (first != p) { f.ok = false; f.error = QStringLiteral("Content-Length contradictoires dans la réponse."); return f; }
        }
        if (!parseLength(first, &len)) { f.ok = false; f.error = QStringLiteral("Content-Length invalide dans la réponse."); return f; }
        f.mode = len > 0 ? BodyFramer::Mode::Length : BodyFramer::Mode::None;
        f.length = len;
        return f;
    }
    f.mode = BodyFramer::Mode::UntilClose;
    return f;
}

// ---------------------------------------------------------------- serialization
QByteArray serializeOriginRequest(const RequestHead &head, const HttpTarget &target) {
    QList<QByteArray> dropped = { "connection", "proxy-connection", "proxy-authorization", "proxy-authenticate", "keep-alive", "te",
                                  "trailer", "upgrade", "host" };
    for (const Header &h : head.headers)
        if (iequals(h.name, "Connection"))
            for (const QByteArray &part : h.value.split(',')) dropped.push_back(part.trimmed().toLower());
    QByteArray out = head.method + ' ' + target.path + " HTTP/1.1\r\n";
    const QByteArray hostText = target.host.contains(QLatin1Char(':')) ? "[" + target.host.toLatin1() + "]" : target.host.toLatin1();
    out += "Host: " + hostText + (target.port == 80 ? QByteArray() : ":" + QByteArray::number(target.port)) + "\r\n";
    for (const Header &h : head.headers) {
        if (dropped.contains(h.name.toLower())) continue;
        out += h.name + ": " + h.value + "\r\n";
    }
    out += "Connection: close\r\n\r\n";
    return out;
}

QByteArray serializeResponseHead(const ResponseHead &head, bool keepAlive) {
    QList<QByteArray> dropped = { "connection", "proxy-connection", "keep-alive" };
    for (const Header &h : head.headers)
        if (iequals(h.name, "Connection"))
            for (const QByteArray &part : h.value.split(',')) dropped.push_back(part.trimmed().toLower());
    QByteArray out = "HTTP/1.1 " + QByteArray::number(head.status) + ' ' + head.reason + "\r\n";
    for (const Header &h : head.headers) {
        if (dropped.contains(h.name.toLower())) continue;
        out += h.name + ": " + h.value + "\r\n";
    }
    out += keepAlive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
    return out;
}

QByteArray errorResponse(int status, const QString &text, const QList<QByteArray> &extra) {
    static const struct { int code; const char *reason; } reasons[] = {
        { 400, "Bad Request" }, { 403, "Forbidden" }, { 404, "Not Found" }, { 407, "Proxy Authentication Required" },
        { 431, "Request Header Fields Too Large" }, { 501, "Not Implemented" }, { 502, "Bad Gateway" },
        { 503, "Service Unavailable" }, { 504, "Gateway Timeout" } };
    const char *reason = "Error";
    for (const auto &r : reasons) if (r.code == status) reason = r.reason;
    const QByteArray body = text.toUtf8() + "\n";
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
    out += "Content-Type: text/plain; charset=utf-8\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\nConnection: close\r\n";
    for (const QByteArray &line : extra) out += line + "\r\n";
    out += "\r\n" + body;
    return out;
}

}  // namespace fortiq::gateway
