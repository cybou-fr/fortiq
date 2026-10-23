#include "CurlFetch.h"

#include <QRegularExpression>
#include <QStringDecoder>

namespace fortiq {

namespace {
QByteArray shellSingleQuote(const QString &value) {
    QByteArray v = value.toUtf8();
    v.replace("'", "'\\''");
    return "'" + v + "'";
}
}  // namespace

QByteArray buildCurlScript(const QString &url) {
    // Output frames, in order (all on stdout, binary-safe by length prefix):
    //   FORTIQ-RC <curl exit>\n
    //   FORTIQ-META\t<fields>\n                  (curl --write-out, portable to curl 7.x)
    //   FORTIQ-HEADERS <n>\n<n bytes>
    //   FORTIQ-ERRTEXT <n>\n<n bytes>
    //   FORTIQ-BODY <n>\n<n bytes>
    QByteArray s;
    s += "u=" + shellSingleQuote(url) + "\n";
    s += "command -v curl >/dev/null 2>&1 || { printf 'FORTIQ-ERR missing-curl\\n'; exit 3; }\n"
         "d=$(mktemp -d 2>/dev/null) || { printf 'FORTIQ-ERR mktemp\\n'; exit 3; }\n"
         "trap 'rm -rf \"$d\"' EXIT HUP INT TERM\n"
         "m=$(curl --silent --show-error --location --max-redirs 10"
         " --proto '=http,https' --proto-redir '=http,https'"
         " --connect-timeout 10 --max-time 30 --max-filesize 5242880 --compressed"
         " --user-agent 'Mozilla/5.0 (compatible; FORTIQ-Browser/0.1)'"
         " --header 'Accept: text/html,application/xhtml+xml,application/json;q=0.9,text/plain;q=0.8,*/*;q=0.5'"
         " --dump-header \"$d/h\" --output \"$d/b\""
         " --write-out 'FORTIQ-META\\t%{http_code}\\t%{url_effective}\\t%{content_type}\\t%{size_download}\\t%{num_redirects}\\t%{remote_ip}\\t%{http_version}\\t%{ssl_verify_result}\\t%{time_namelookup}\\t%{time_connect}\\t%{time_appconnect}\\t%{time_starttransfer}\\t%{time_total}'"
         " -- \"$u\" 2>\"$d/e\")\n"
         "rc=$?\n"
         "size() { if [ -f \"$1\" ]; then wc -c < \"$1\" | tr -d ' '; else printf 0; fi; }\n"
         "printf 'FORTIQ-RC %s\\n' \"$rc\"\n"
         "printf '%s\\n' \"$m\"\n"
         "printf 'FORTIQ-HEADERS %s\\n' \"$(size \"$d/h\")\"; [ -f \"$d/h\" ] && cat \"$d/h\"; printf '\\n'\n"
         "head -c 2048 \"$d/e\" > \"$d/e2\" 2>/dev/null\n"
         "printf 'FORTIQ-ERRTEXT %s\\n' \"$(size \"$d/e2\")\"; [ -f \"$d/e2\" ] && cat \"$d/e2\"; printf '\\n'\n"
         "printf 'FORTIQ-BODY %s\\n' \"$(size \"$d/b\")\"; [ -f \"$d/b\" ] && cat \"$d/b\"\n"
         "exit 0\n";
    return s;
}

QString describeCurlExit(int code) {
    switch (code) {
    case 0: return {};
    case 1: case 2: return QStringLiteral("curl ne peut pas traiter cette adresse.");
    case 3: return QStringLiteral("Adresse mal formée.");
    case 5: case 6: return QStringLiteral("Nom d'hôte introuvable depuis le serveur (DNS).");
    case 7: return QStringLiteral("Connexion refusée ou impossible depuis le serveur.");
    case 22: return QStringLiteral("Le serveur web a répondu par une erreur.");
    case 28: return QStringLiteral("Délai dépassé (30 s).");
    case 35: return QStringLiteral("Échec de la négociation TLS.");
    case 47: return QStringLiteral("Trop de redirections (limite 10).");
    case 52: return QStringLiteral("Le serveur web n'a rien renvoyé.");
    case 56: return QStringLiteral("Connexion interrompue pendant la réception.");
    case 60: return QStringLiteral("Certificat TLS non reconnu : connexion refusée.");
    case 63: return QStringLiteral("Réponse trop grande (limite 5 Mio) ; début conservé.");
    case 1000: return QStringLiteral("Protocole refusé (seuls http et https sont permis).");
    default: break;
    }
    if (code == 1 || code == 4) return QStringLiteral("Protocole non supporté.");
    return QStringLiteral("Échec de curl (code %1).").arg(code);
}

namespace {
struct Reader {
    const QByteArray &d;
    qsizetype pos = 0;
    bool line(QByteArray &out) {
        if (pos >= d.size()) return false;
        const qsizetype nl = d.indexOf('\n', pos);
        if (nl < 0) return false;
        out = d.mid(pos, nl - pos);
        pos = nl + 1;
        return true;
    }
    void skipNewline() { if (pos < d.size() && d.at(pos) == '\n') ++pos; }
    bool take(qsizetype n, QByteArray &out) {
        if (n < 0 || pos + n > d.size()) { out = d.mid(pos); pos = d.size(); return false; }
        out = d.mid(pos, n);
        pos += n;
        return true;
    }
};

bool frameCount(const QByteArray &line, const char *tag, qsizetype &n) {
    const QByteArray prefix = QByteArray(tag) + ' ';
    if (!line.startsWith(prefix)) return false;
    bool ok = false;
    n = line.mid(prefix.size()).trimmed().toLongLong(&ok);
    return ok && n >= 0;
}

struct Block { int status = 0; QString text; QList<QPair<QString, QString>> fields; };

QList<Block> parseHeaderBlocks(const QByteArray &raw) {
    QList<Block> blocks;
    Block cur;
    bool open = false;
    const QList<QByteArray> lines = raw.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) {
            if (open) { blocks << cur; cur = Block(); open = false; }
            continue;
        }
        if (line.startsWith("HTTP/")) {
            if (open) blocks << cur;
            cur = Block(); open = true;
            const QList<QByteArray> parts = line.split(' ');
            if (parts.size() >= 2) cur.status = parts.at(1).toInt();
            cur.text = parts.size() >= 3 ? QString::fromLatin1(line.mid(parts.at(0).size() + parts.at(1).size() + 2)).trimmed() : QString();
            continue;
        }
        if (!open) continue;
        if ((line.startsWith(' ') || line.startsWith('\t')) && !cur.fields.isEmpty()) {   // obsolete line folding
            cur.fields.last().second += QLatin1Char(' ') + QString::fromLatin1(line).trimmed();
            continue;
        }
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) continue;
        cur.fields.append({ QString::fromLatin1(line.left(colon)).trimmed(), QString::fromUtf8(line.mid(colon + 1)).trimmed() });
    }
    if (open) blocks << cur;
    return blocks;
}
}  // namespace

CurlResult parseCurlOutput(const QByteArray &out) {
    CurlResult r;
    Reader in { out };
    QByteArray line;
    if (!in.line(line)) { r.problem = QStringLiteral("protocol"); return r; }
    if (line.startsWith("FORTIQ-ERR ")) { r.problem = QString::fromLatin1(line.mid(11)).trimmed(); return r; }
    if (!line.startsWith("FORTIQ-RC ")) { r.problem = QStringLiteral("protocol"); return r; }
    r.curlExit = line.mid(10).trimmed().toInt();

    if (!in.line(line) || !line.startsWith("FORTIQ-META\t")) { r.problem = QStringLiteral("protocol"); return r; }
    const QList<QByteArray> f = line.split('\t');   // [0]=tag, then 13 fields
    auto field = [&](int i) { return i < f.size() ? QString::fromUtf8(f.at(i)) : QString(); };
    r.status = field(1).toInt();
    r.effectiveUrl = field(2);
    r.contentType = field(3);
    r.sizeDownload = field(4).toLongLong();
    r.redirects = field(5).toInt();
    r.remoteIp = field(6);
    r.httpVersion = field(7);
    const QString verify = field(8);
    r.tlsVerify = verify.isEmpty() ? -1 : verify.toInt();
    r.timeDns = field(9).toDouble();
    r.timeConnect = field(10).toDouble();
    r.timeTls = field(11).toDouble();
    r.timeFirstByte = field(12).toDouble();
    r.timeTotal = field(13).toDouble();

    qsizetype n = 0;
    QByteArray headers, err, body;
    if (!in.line(line) || !frameCount(line, "FORTIQ-HEADERS", n) || !in.take(n, headers)) { r.problem = QStringLiteral("protocol"); return r; }
    in.skipNewline();   // every frame body is followed by one newline written by the script
    if (!in.line(line) || !frameCount(line, "FORTIQ-ERRTEXT", n) || !in.take(n, err)) { r.problem = QStringLiteral("protocol"); return r; }
    in.skipNewline();
    if (!in.line(line) || !frameCount(line, "FORTIQ-BODY", n)) { r.problem = QStringLiteral("protocol"); return r; }
    in.take(n, body);   // a short body (output cap hit) is kept and flagged below
    r.body = body;
    r.framed = true;
    if (body.size() < n) r.truncated = true;

    r.curlMessage = QString::fromUtf8(err).trimmed();
    const QList<Block> blocks = parseHeaderBlocks(headers);
    if (!blocks.isEmpty()) {
        const Block &last = blocks.last();
        if (r.status == 0) r.status = last.status;
        r.statusText = last.text;
        for (const auto &kv : last.fields) r.headers.push_back(QVariantMap { { "name", kv.first }, { "value", kv.second } });
        for (int i = 0; i + 1 < blocks.size(); ++i) {
            QString location;
            for (const auto &kv : blocks.at(i).fields) if (kv.first.compare(QLatin1String("location"), Qt::CaseInsensitive) == 0) location = kv.second;
            r.redirectChain.push_back(QVariantMap { { "status", blocks.at(i).status }, { "location", location } });
        }
    }
    if (r.curlExit == 63) r.truncated = true;
    return r;
}

QString headerValue(const QVariantList &headers, const QString &name) {
    for (const QVariant &v : headers) {
        const QVariantMap m = v.toMap();
        if (m.value("name").toString().compare(name, Qt::CaseInsensitive) == 0) return m.value("value").toString();
    }
    return {};
}

namespace {
const char16_t kCp1252[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178 };

QString charsetFromContentType(const QString &contentType) {
    static const QRegularExpression re(QStringLiteral("charset\\s*=\\s*\"?([A-Za-z0-9_.:-]+)"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(contentType);
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

QString charsetFromHtml(const QByteArray &body) {
    static const QRegularExpression re(QStringLiteral("<meta[^>]+charset\\s*=\\s*[\"']?\\s*([A-Za-z0-9_.:-]+)"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(QString::fromLatin1(body.left(4096)));
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

QString decodeWith(const QByteArray &body, const QString &charset, bool *known) {
    *known = true;
    if (charset == QLatin1String("utf-8") || charset == QLatin1String("utf8")) {
        QStringDecoder dec(QStringDecoder::Utf8);
        return dec(body);
    }
    if (charset == QLatin1String("iso-8859-1") || charset == QLatin1String("latin1") || charset == QLatin1String("us-ascii") || charset == QLatin1String("ascii"))
        return QString::fromLatin1(body);
    if (charset == QLatin1String("windows-1252") || charset == QLatin1String("cp1252")) {
        QString s;
        s.reserve(body.size());
        for (const uchar c : body) s.append(c >= 0x80 && c < 0xA0 ? QChar(kCp1252[c - 0x80]) : QChar(c));
        return s;
    }
    if (charset == QLatin1String("utf-16le") || charset == QLatin1String("utf-16")) { QStringDecoder dec(QStringDecoder::Utf16LE); return dec(body); }
    if (charset == QLatin1String("utf-16be")) { QStringDecoder dec(QStringDecoder::Utf16BE); return dec(body); }
    *known = false;
    QStringDecoder dec(QStringDecoder::Utf8);
    return dec(body);
}
}  // namespace

DecodedText decodeBody(const QByteArray &body, const QString &contentType, bool html) {
    DecodedText out;
    QByteArray data = body;
    QString charset = charsetFromContentType(contentType);
    if (data.startsWith("\xEF\xBB\xBF")) { data.remove(0, 3); charset = QStringLiteral("utf-8"); }
    else if (data.startsWith("\xFF\xFE")) { data.remove(0, 2); charset = QStringLiteral("utf-16le"); }
    else if (data.startsWith("\xFE\xFF")) { data.remove(0, 2); charset = QStringLiteral("utf-16be"); }
    if (charset.isEmpty() && html) charset = charsetFromHtml(data);
    const bool declared = !charset.isEmpty();
    if (!declared) charset = QStringLiteral("utf-8");
    bool known = true;
    out.text = decodeWith(data, charset, &known);
    out.charset = known ? charset : QStringLiteral("utf-8");
    if (!known) out.note = QStringLiteral("Jeu de caractères « %1 » non géré : affiché en UTF-8, des caractères peuvent être erronés.").arg(charset);
    else if (charset == QLatin1String("utf-8") && !data.isEmpty()) {
        QStringDecoder strict(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
        const QString check = strict(data);
        Q_UNUSED(check);
        if (strict.hasError()) {
            if (declared) {
                out.text = QString::fromUtf8(data);   // invalid bytes become U+FFFD
                out.note = QStringLiteral("Le texte annoncé en UTF-8 contient des octets invalides.");
            } else {
                // Nothing declared and not UTF-8: legacy 8-bit text, shown as windows-1252 instead of garbage.
                out.text = decodeWith(data, QStringLiteral("windows-1252"), &known);
                out.charset = QStringLiteral("windows-1252");
                out.note = QStringLiteral("Texte non UTF-8 : interprété comme windows-1252.");
            }
        }
    }
    return out;
}

}  // namespace fortiq
