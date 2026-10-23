#include "HtmlSanitizer.h"

#include "UrlTools.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace fortiq {

namespace {
constexpr int kMaxOutput = 2 * 1024 * 1024;   // characters of sanitized HTML
constexpr int kMaxDepth = 96;
constexpr int kMaxLinks = 5000;

const QSet<QString> &voidTags() {
    static const QSet<QString> s { "br", "hr", "img", "input", "meta", "link", "base", "area", "col", "embed", "source", "track", "wbr", "param" };
    return s;
}
// Removed together with everything inside them.
const QSet<QString> &dropWithContent() {
    static const QSet<QString> s { "script", "style", "noscript", "template", "iframe", "frame", "frameset", "object", "embed", "applet",
        "svg", "math", "canvas", "audio", "video", "head", "select", "option", "textarea", "button", "datalist", "dialog", "map", "title" };
    return s;
}
// Page furniture removed in reader mode when there is no <main>/<article>.
const QSet<QString> &furniture() {
    static const QSet<QString> s { "nav", "aside", "footer", "header", "form" };
    return s;
}
// Kept tags (attribute-free except as noted when written).
const QSet<QString> &keepTags() {
    static const QSet<QString> s { "h1", "h2", "h3", "h4", "h5", "h6", "p", "ul", "ol", "li", "dl", "dt", "dd", "blockquote", "pre", "code",
        "em", "strong", "b", "i", "u", "s", "del", "ins", "sub", "sup", "small", "mark", "kbd", "samp", "q", "cite", "table", "thead",
        "tbody", "tfoot", "tr", "th", "td", "caption", "a" };
    return s;
}
// Structural wrappers shown as a plain block.
const QSet<QString> &blockLike() {
    static const QSet<QString> s { "div", "section", "article", "main", "figure", "figcaption", "address", "details", "summary", "center", "fieldset" };
    return s;
}

const QHash<QString, QString> &entityTable() {
    static const QHash<QString, QString> t {
        { "amp", "&" }, { "lt", "<" }, { "gt", ">" }, { "quot", "\"" }, { "apos", "'" }, { "nbsp", "\u00A0" },
        { "copy", "\u00A9" }, { "reg", "\u00AE" }, { "trade", "\u2122" }, { "hellip", "\u2026" }, { "mdash", "\u2014" },
        { "ndash", "\u2013" }, { "lsquo", "\u2018" }, { "rsquo", "\u2019" }, { "ldquo", "\u201C" }, { "rdquo", "\u201D" },
        { "laquo", "\u00AB" }, { "raquo", "\u00BB" }, { "bull", "\u2022" }, { "middot", "\u00B7" }, { "euro", "\u20AC" },
        { "pound", "\u00A3" }, { "yen", "\u00A5" }, { "cent", "\u00A2" }, { "sect", "\u00A7" }, { "deg", "\u00B0" },
        { "plusmn", "\u00B1" }, { "times", "\u00D7" }, { "divide", "\u00F7" }, { "larr", "\u2190" }, { "rarr", "\u2192" },
        { "uarr", "\u2191" }, { "darr", "\u2193" }, { "eacute", "\u00E9" }, { "egrave", "\u00E8" }, { "ecirc", "\u00EA" },
        { "agrave", "\u00E0" }, { "acirc", "\u00E2" }, { "ccedil", "\u00E7" }, { "ocirc", "\u00F4" }, { "ucirc", "\u00FB" },
        { "ugrave", "\u00F9" }, { "icirc", "\u00EE" }, { "iuml", "\u00EF" }, { "euml", "\u00EB" }, { "uuml", "\u00FC" },
        { "ouml", "\u00F6" }, { "auml", "\u00E4" }, { "szlig", "\u00DF" }, { "ntilde", "\u00F1" }, { "iexcl", "\u00A1" },
        { "iquest", "\u00BF" }, { "para", "\u00B6" }, { "hearts", "\u2665" }, { "check", "\u2713" } };
    return t;
}

bool isInlineTag(const QString &n) {
    static const QSet<QString> s { "a", "em", "strong", "b", "i", "u", "code", "s", "sub", "sup", "small", "mark", "kbd", "samp", "q", "cite", "del", "ins" };
    return s.contains(n);
}

QString escapeText(const QString &s) {
    QString o;
    o.reserve(s.size());
    for (const QChar c : s) {
        if (c == QLatin1Char('&')) o += QLatin1String("&amp;");
        else if (c == QLatin1Char('<')) o += QLatin1String("&lt;");
        else if (c == QLatin1Char('>')) o += QLatin1String("&gt;");
        else if (c.unicode() < 0x20 && c != QLatin1Char('\n') && c != QLatin1Char('\t')) continue;   // no control characters
        else o += c;
    }
    return o;
}
QString escapeAttr(const QString &s) {
    QString o = escapeText(s);
    o.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    return o;
}

struct Attr { QString name, value; };
struct Token {
    enum Kind { Text, Start, End } kind = Text;
    QString name;            // lower-case tag name
    QList<Attr> attrs;
    QString text;            // decoded text (Text)
    bool selfClosing = false;
};

// Tolerant tokenizer: never fails, always makes progress.
QList<Token> tokenize(const QString &in) {
    QList<Token> out;
    const qsizetype n = in.size();
    qsizetype i = 0;
    auto isNameChar = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char(':') || c == QLatin1Char('_'); };
    QString pendingText;
    auto flushText = [&] {
        if (pendingText.isEmpty()) return;
        Token t; t.kind = Token::Text; t.text = decodeEntities(pendingText);
        out.push_back(t);
        pendingText.clear();
    };
    while (i < n) {
        const QChar c = in.at(i);
        if (c != QLatin1Char('<')) {
            const qsizetype next = in.indexOf(QLatin1Char('<'), i);
            const qsizetype end = next < 0 ? n : next;
            pendingText += in.mid(i, end - i);
            i = end;
            continue;
        }
        // "<!--" comment
        if (in.mid(i, 4) == QLatin1String("<!--")) {
            const qsizetype e = in.indexOf(QLatin1String("-->"), i + 4);
            i = e < 0 ? n : e + 3;
            continue;
        }
        // "<!DOCTYPE", "<![CDATA[", "<?xml ?>"
        if (i + 1 < n && (in.at(i + 1) == QLatin1Char('!') || in.at(i + 1) == QLatin1Char('?'))) {
            const qsizetype e = in.indexOf(QLatin1Char('>'), i + 2);
            i = e < 0 ? n : e + 1;
            continue;
        }
        const bool closing = i + 1 < n && in.at(i + 1) == QLatin1Char('/');
        qsizetype j = i + (closing ? 2 : 1);
        if (j >= n || !in.at(j).isLetter()) {   // a stray "<" is text
            pendingText += QLatin1Char('<');
            ++i;
            continue;
        }
        flushText();
        qsizetype ns = j;
        while (j < n && isNameChar(in.at(j))) ++j;
        Token t;
        t.kind = closing ? Token::End : Token::Start;
        t.name = in.mid(ns, j - ns).toLower();
        // attributes
        while (j < n && in.at(j) != QLatin1Char('>')) {
            while (j < n && (in.at(j).isSpace() || in.at(j) == QLatin1Char('/'))) {
                if (in.at(j) == QLatin1Char('/')) t.selfClosing = true;
                ++j;
            }
            if (j >= n || in.at(j) == QLatin1Char('>')) break;
            qsizetype as = j;
            while (j < n && !in.at(j).isSpace() && in.at(j) != QLatin1Char('=') && in.at(j) != QLatin1Char('>') && in.at(j) != QLatin1Char('/')) ++j;
            Attr a;
            a.name = in.mid(as, j - as).toLower();
            while (j < n && in.at(j).isSpace()) ++j;
            if (j < n && in.at(j) == QLatin1Char('=')) {
                ++j;
                while (j < n && in.at(j).isSpace()) ++j;
                if (j < n && (in.at(j) == QLatin1Char('"') || in.at(j) == QLatin1Char('\''))) {
                    const QChar q = in.at(j++);
                    const qsizetype e = in.indexOf(q, j);
                    const qsizetype end = e < 0 ? n : e;
                    a.value = decodeEntities(in.mid(j, end - j));
                    j = e < 0 ? n : e + 1;
                } else {
                    const qsizetype vs = j;
                    while (j < n && !in.at(j).isSpace() && in.at(j) != QLatin1Char('>')) ++j;
                    a.value = decodeEntities(in.mid(vs, j - vs));
                }
            }
            if (!a.name.isEmpty()) t.attrs.push_back(a);
            else if (j < n && in.at(j) != QLatin1Char('>')) ++j;   // guarantee progress
        }
        if (j < n) ++j;   // '>'
        i = j;
        out.push_back(t);
        // Raw-text elements: swallow everything up to the matching end tag.
        if (!closing && (t.name == QLatin1String("script") || t.name == QLatin1String("style") || t.name == QLatin1String("textarea")
                         || t.name == QLatin1String("title"))) {
            const QString endTag = QStringLiteral("</") + t.name;
            qsizetype e = in.indexOf(endTag, i, Qt::CaseInsensitive);
            const qsizetype contentEnd = e < 0 ? n : e;
            if (t.name == QLatin1String("title")) {
                Token body; body.kind = Token::Text; body.text = decodeEntities(in.mid(i, contentEnd - i));
                out.push_back(body);
            }
            i = contentEnd;
        }
    }
    flushText();
    return out;
}

QString attr(const Token &t, const char *name) {
    for (const Attr &a : t.attrs) if (a.name == QLatin1String(name)) return a.value;
    return {};
}
bool isDigits(const QString &s) {
    if (s.isEmpty() || s.size() > 4) return false;
    for (const QChar c : s) if (!c.isDigit()) return false;
    return true;
}
QString collapse(const QString &s) {
    QString o;
    o.reserve(s.size());
    bool space = false;
    for (const QChar c : s) {
        if (c.isSpace() && c != QChar(0x00A0)) { if (!space) o += QLatin1Char(' '); space = true; }
        else { o += c; space = false; }
    }
    return o;
}
}  // namespace

QString decodeEntities(const QString &text) {
    if (!text.contains(QLatin1Char('&'))) return text;
    QString out;
    out.reserve(text.size());
    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = text.at(i);
        if (c != QLatin1Char('&')) { out += c; continue; }
        const qsizetype semi = text.indexOf(QLatin1Char(';'), i + 1);
        if (semi < 0 || semi - i > 12) { out += c; continue; }
        const QString name = text.mid(i + 1, semi - i - 1);
        if (name.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const uint code = name.startsWith(QLatin1String("#x")) || name.startsWith(QLatin1String("#X")) ? name.mid(2).toUInt(&ok, 16) : name.mid(1).toUInt(&ok, 10);
            if (ok && code > 0 && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF)) {
                out += QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1);
                i = semi;
                continue;
            }
        } else if (const auto it = entityTable().constFind(name); it != entityTable().constEnd()) {
            out += it.value();
            i = semi;
            continue;
        }
        out += c;
    }
    return out;
}

SanitizedPage sanitizeHtml(const QString &html, const QString &baseUrl, bool reader) {
    SanitizedPage page;
    const QList<Token> tokens = tokenize(html);

    // Pass 1: title, refresh, and whether the page has a <main>/<article>.
    bool hasMain = false;
    for (qsizetype k = 0; k < tokens.size(); ++k) {
        const Token &t = tokens.at(k);
        if (t.kind == Token::Start) {
            if (t.name == QLatin1String("main") || t.name == QLatin1String("article")) hasMain = true;
            else if (t.name == QLatin1String("title") && page.title.isEmpty() && k + 1 < tokens.size() && tokens.at(k + 1).kind == Token::Text)
                page.title = collapse(tokens.at(k + 1).text).trimmed().left(200);
            else if (t.name == QLatin1String("meta") && attr(t, "http-equiv").compare(QLatin1String("refresh"), Qt::CaseInsensitive) == 0) {
                static const QRegularExpression re(QStringLiteral("url\\s*=\\s*['\"]?([^'\"\\s;]+)"), QRegularExpression::CaseInsensitiveOption);
                const auto m = re.match(attr(t, "content"));
                if (m.hasMatch()) page.refreshUrl = resolveLink(baseUrl, m.captured(1));
            }
        }
    }
    const bool extractMain = reader && hasMain;
    page.readerExtracted = extractMain;

    // Pass 2: emit.
    QString out;
    QString plain;
    struct Open { QString name; bool emitted; };
    QList<Open> stack;
    int dropDepth = 0;                 // inside a dropped element (script, style, ...)
    QString dropName;
    int mainDepth = extractMain ? 0 : 1;   // >0: inside the extracted region (always, unless extracting)
    int furnitureDepth = 0;
    bool inPre = false;
    bool pendingSpace = false;
    bool atBlockStart = true;
    auto room = [&] { return out.size() < kMaxOutput; };

    auto emitText = [&](const QString &raw) {
        if (!room()) { page.truncated = true; return; }
        QString t = raw;
        if (!inPre) {
            t = collapse(t);
            if (t.isEmpty()) return;
            if (t.startsWith(QLatin1Char(' ')) && (atBlockStart)) t.remove(0, 1);
            if (t.isEmpty()) return;
            if (pendingSpace && !t.startsWith(QLatin1Char(' ')) && !out.isEmpty() && !atBlockStart) out += QLatin1Char(' ');
            pendingSpace = t.endsWith(QLatin1Char(' '));
            if (pendingSpace) t.chop(1);
        } else {
            t.remove(QLatin1Char('\r'));
        }
        out += escapeText(t);
        plain += t;
        atBlockStart = false;
    };
    auto closeTop = [&](bool write) {
        const Open o = stack.takeLast();
        if (o.emitted && write) out += QStringLiteral("</") + o.name + QLatin1Char('>');
        if (o.name == QLatin1String("pre")) inPre = false;
    };

    for (const Token &t : tokens) {
        if (page.truncated) break;
        if (dropDepth > 0) {
            // A <head> without its end tag ends where <body> starts, as in every HTML parser.
            if (dropName == QLatin1String("head") && t.kind == Token::Start && t.name == QLatin1String("body")) {
                dropDepth = 0;
            } else {
                // Inside a dropped element: track nesting of the same name only.
                if (t.kind == Token::Start && t.name == QLatin1String("script") && dropName != QLatin1String("script")) ++page.scriptCount;
                if (t.kind == Token::Start && t.name == dropName && !voidTags().contains(t.name)) ++dropDepth;
                else if (t.kind == Token::End && t.name == dropName) --dropDepth;
                continue;
            }
        }
        if (t.kind == Token::Text) {
            if (mainDepth > 0 && furnitureDepth == 0) emitText(t.text);
            continue;
        }
        if (t.kind == Token::Start) {
            if (t.name == QLatin1String("script")) ++page.scriptCount;
            if (t.name == QLatin1String("img")) {
                ++page.imageCount;
                if (mainDepth > 0 && furnitureDepth == 0) {
                    const QString alt = collapse(attr(t, "alt")).trimmed();
                    if (!alt.isEmpty() && room()) emitText(QStringLiteral("[") + alt + QStringLiteral("]"));
                }
                continue;
            }
            if (t.name == QLatin1String("br")) { if (mainDepth > 0 && furnitureDepth == 0 && room()) { out += QLatin1String("<br>"); atBlockStart = true; pendingSpace = false; } continue; }
            if (t.name == QLatin1String("hr")) { if (mainDepth > 0 && furnitureDepth == 0 && room()) { out += QLatin1String("<hr>"); atBlockStart = true; } continue; }
            if (dropWithContent().contains(t.name)) {
                if (!voidTags().contains(t.name) && !t.selfClosing) { dropDepth = 1; dropName = t.name; }
                continue;
            }
            if (voidTags().contains(t.name)) continue;   // meta, link, input, ... : nothing to show

            // Region tracking for reader mode.
            const bool isMain = t.name == QLatin1String("main") || t.name == QLatin1String("article");
            if (extractMain && isMain && mainDepth == 0) mainDepth = 1;
            else if (extractMain && mainDepth > 0 && isMain) ++mainDepth;   // nested article/main inside the region
            if (reader && !extractMain && furniture().contains(t.name)) ++furnitureDepth;
            else if (reader && furnitureDepth > 0 && furniture().contains(t.name)) ++furnitureDepth;

            QString outName;
            QString tagText;
            if (keepTags().contains(t.name)) {
                outName = t.name;
                if (t.name == QLatin1String("a")) {
                    const QString link = resolveLink(baseUrl, attr(t, "href"));
                    if (!link.isEmpty() && page.linkCount < kMaxLinks) { tagText = QStringLiteral("<a href=\"") + escapeAttr(link) + QStringLiteral("\">"); ++page.linkCount; }
                    else tagText = QStringLiteral("<a>");   // keeps the text, no target
                } else if (t.name == QLatin1String("td") || t.name == QLatin1String("th")) {
                    tagText = QLatin1Char('<') + t.name;
                    if (isDigits(attr(t, "colspan"))) tagText += QStringLiteral(" colspan=\"") + attr(t, "colspan") + QLatin1Char('"');
                    if (isDigits(attr(t, "rowspan"))) tagText += QStringLiteral(" rowspan=\"") + attr(t, "rowspan") + QLatin1Char('"');
                    tagText += QLatin1Char('>');
                } else if (t.name == QLatin1String("ol") && isDigits(attr(t, "start"))) {
                    tagText = QStringLiteral("<ol start=\"") + attr(t, "start") + QStringLiteral("\">");
                } else {
                    tagText = QLatin1Char('<') + t.name + QLatin1Char('>');
                }
            } else if (blockLike().contains(t.name)) {
                outName = QStringLiteral("div");
                tagText = QStringLiteral("<div>");
            } else {
                // span, label, font, unknown elements: show the content only.
                stack.push_back({ t.name, false });
                if (stack.size() > kMaxDepth) closeTop(true);
                continue;
            }
            const bool visible = mainDepth > 0 && furnitureDepth == 0;
            if (visible && room()) {
                const bool inlineTag = isInlineTag(t.name);
                // A space owed before an inline element goes outside it; blocks start without one.
                if (inlineTag && pendingSpace && !atBlockStart && !out.isEmpty()) out += QLatin1Char(' ');
                out += tagText;
                if (inlineTag) pendingSpace = false;
                else { atBlockStart = true; pendingSpace = false; }
                if (t.name == QLatin1String("pre")) inPre = true;
            }
            stack.push_back({ outName, visible });
            if (t.selfClosing) closeTop(true);
            else if (stack.size() > kMaxDepth) closeTop(true);
            continue;
        }

        // End tag: close up to the matching open element; ignore strays.
        qsizetype idx = -1;
        for (qsizetype s = stack.size() - 1; s >= 0; --s) {
            const QString open = stack.at(s).name;
            const bool same = open == t.name || (open == QLatin1String("div") && blockLike().contains(t.name));
            if (same) { idx = s; break; }
        }
        // Names of dropped-inline wrappers (span...) are stored under their source name with emitted=false.
        if (idx < 0) {
            for (qsizetype s = stack.size() - 1; s >= 0; --s) if (stack.at(s).name == t.name) { idx = s; break; }
        }
        if (idx < 0) continue;
        bool closedBlock = false;
        while (stack.size() > idx) {
            const bool wasMain = (t.name == QLatin1String("main") || t.name == QLatin1String("article"));
            const QString closing = stack.last().name;
            if (!isInlineTag(closing) && stack.last().emitted) closedBlock = true;
            closeTop(true);
            if (stack.size() == idx) {
                if (extractMain && wasMain && mainDepth > 0) --mainDepth;
                if (reader && furnitureDepth > 0 && furniture().contains(t.name)) --furnitureDepth;
                if (!extractMain) {
                    // Nothing to do: furniture handled above.
                }
            }
        }
        if (closedBlock) { atBlockStart = true; pendingSpace = false; }
    }
    while (!stack.isEmpty()) closeTop(true);

    page.html = out;
    page.plainText = plain.trimmed();
    return page;
}

}  // namespace fortiq
