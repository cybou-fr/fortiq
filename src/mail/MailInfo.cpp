#include "MailInfo.h"
#include "../core/SafeCast.h"

#include "../browser/HtmlSanitizer.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace fortiq {

namespace {
QByteArray shQuote(const QString &value) {
    QByteArray v = value.toUtf8();
    v.replace("'", "'\\''");
    return "'" + v + "'";
}

QByteArray prologue(const QString &configPath) {
    QByteArray s = "export LC_ALL=C.UTF-8\n";
    if (!configPath.isEmpty()) s += "export NOTMUCH_CONFIG=" + shQuote(configPath) + "\n";
    s += "command -v notmuch >/dev/null 2>&1 || { echo 'FQ-ERR missing-notmuch' >&2; exit 4; }\n";
    return s;
}

QHash<QString, QByteArray> sections(const QByteArray &out) {
    QHash<QString, QByteArray> map;
    QString current;
    QByteArray body;
    auto flush = [&] { if (!current.isEmpty()) map.insert(current, body); body.clear(); };
    for (const QByteArray &line : out.split('\n')) {
        if (line.startsWith("#section ")) { flush(); current = QString::fromLatin1(line.mid(9)).trimmed(); continue; }
        body += line + '\n';
    }
    flush();
    return map;
}

QString header(const QJsonObject &h, const char *name) {
    return h.value(QLatin1String(name)).toString();
}

void collectBody(const QJsonArray &parts, MailMessage &m, QString &text, QString &rawHtml);

// A name shown in the list: control characters and bidirectional overrides ("invoice<RLO>txt.exe" reads as "invoiceexe.txt") become "_".
QString displaySafeName(const QString &name) {
    QString out;
    for (const QChar c : name) {
        const ushort u = c.unicode();
        const bool bidi = (u >= 0x202A && u <= 0x202E) || (u >= 0x2066 && u <= 0x2069) || u == 0x200E || u == 0x200F || u == 0x061C;
        out += (u < 0x20 || u == 0x7f || bidi) ? QLatin1Char('_') : c;
    }
    return out.left(300);
}

void addAttachment(MailMessage &m, MailAttachment a) {
    if (m.attachments.size() >= kMaxAttachmentsShown) { ++m.attachmentsOmitted; return; }
    a.name = displaySafeName(a.name);
    m.attachments.push_back(a);
}

void collectPart(const QJsonObject &part, MailMessage &m, QString &text, QString &rawHtml) {
    const QString type = part.value("content-type").toString().toLower();
    const QString disposition = part.value("content-disposition").toString().toLower();
    if (type.startsWith(QLatin1String("multipart/"))) {
        collectBody(part.value("content").toArray(), m, text, rawHtml);
        return;
    }
    if (type == QLatin1String("message/rfc822")) {   // a forwarded message: its body is shown inline, labelled
        for (const QJsonValue &inner : part.value("content").toArray())
            if (inner.isObject()) collectBody(inner.toObject().value("body").toArray(), m, text, rawHtml);
        return;
    }
    const bool attachment = disposition == QLatin1String("attachment") || part.contains("filename");
    if (attachment) {
        MailAttachment a;
        a.name = part.value("filename").toString();
        a.type = type;
        a.size = toInt64(part.value("content-length").toDouble());
        a.part = part.value("id").toInt();
        if (a.name.isEmpty()) a.name = QStringLiteral("(sans nom)");
        addAttachment(m, a);
        return;
    }
    if (type == QLatin1String("text/plain")) {
        const QString t = part.value("content").toString();
        if (!text.isEmpty()) text += QLatin1Char('\n');
        text += t;
    } else if (type == QLatin1String("text/html")) {
        if (!rawHtml.isEmpty()) rawHtml += QLatin1Char('\n');
        rawHtml += part.value("content").toString();
    } else if (!type.isEmpty()) {   // inline non-text content (image, calendar, ...)
        MailAttachment a;
        a.name = part.value("filename").toString().isEmpty() ? type : part.value("filename").toString();
        a.type = type;
        a.size = toInt64(part.value("content-length").toDouble());
        a.part = part.value("id").toInt();
        addAttachment(m, a);
    }
}

void collectBody(const QJsonArray &parts, MailMessage &m, QString &text, QString &rawHtml) {
    for (const QJsonValue &p : parts) if (p.isObject()) collectPart(p.toObject(), m, text, rawHtml);
}

void walk(const QJsonValue &value, int depth, QList<MailMessage> &out) {
    if (!value.isArray()) return;
    const QJsonArray arr = value.toArray();
    if (!arr.isEmpty() && arr.first().isObject() && arr.first().toObject().contains("id")) {   // a node: [message, [replies...]]
        const QJsonObject o = arr.first().toObject();
        MailMessage m;
        m.id = o.value("id").toString();
        m.timestamp = toInt64(o.value("timestamp").toDouble());
        m.depth = depth;
        m.match = o.value("match").toBool(true);
        const QJsonObject h = o.value("headers").toObject();
        m.from = header(h, "From"); m.to = header(h, "To"); m.cc = header(h, "Cc");
        m.date = header(h, "Date"); m.subject = header(h, "Subject");
        for (const QJsonValue &t : o.value("tags").toArray()) m.tags << t.toString();
        QString text, rawHtml;
        collectBody(o.value("body").toArray(), m, text, rawHtml);
        m.text = text;
        if (!rawHtml.isEmpty()) {
            const SanitizedPage page = sanitizeHtml(rawHtml, QStringLiteral("https://mail.invalid/"), false);
            m.html = page.html;
            m.htmlPlain = page.plainText;
            m.htmlLinks = page.linkCount; m.htmlScripts = page.scriptCount; m.htmlImages = page.imageCount;
        }
        out.push_back(m);
        if (arr.size() > 1) walk(arr.at(1), depth + 1, out);
        return;
    }
    for (const QJsonValue &child : arr) walk(child, depth, out);
}
}  // namespace

// ---------------------------------------------------------------- validation
QString validateMailQuery(const QString &query) {
    if (query.size() > 500) return QStringLiteral("Recherche trop longue (500 caractères au plus).");
    for (const QChar c : query)
        if (c.unicode() < 0x20 || c.unicode() == 0x7f) return QStringLiteral("La recherche contient un caractère de contrôle.");
    return {};
}

bool validMailThreadId(const QString &id) {
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{1,64}$"));
    return re.match(id).hasMatch();
}

bool validMailTag(const QString &tag) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,40}$"));
    return re.match(tag).hasMatch();
}

// ---------------------------------------------------------------- scripts
QByteArray buildMailOverviewScript(const QString &configPath) {
    QByteArray s = prologue(configPath);
    s += "sec() { printf '#section %s\\n' \"$1\"; }\n"
         "sec version; notmuch --version 2>&1\n";
    const char *keys[][2] = { { "inbox", "tag:inbox" }, { "unread", "tag:unread" }, { "flagged", "tag:flagged" },
                              { "sent", "tag:sent" }, { "deleted", "tag:deleted" }, { "all", "*" } };
    for (const auto &k : keys)
        s += QByteArray("sec count_") + k[0] + "; notmuch count -- '" + k[1] + "' 2>&1\n";
    s += "sec end\n";
    return s;
}

QByteArray buildMailSearchScript(const QString &query, int limit, int offset, const QString &configPath) {
    if (!validateMailQuery(query).isEmpty()) return {};
    limit = qBound(1, limit, 500);
    offset = qMax(0, offset);
    QByteArray s = prologue(configPath);
    s += "notmuch search --format=json --output=summary --sort=newest-first --limit=" + QByteArray::number(limit)
       + " --offset=" + QByteArray::number(offset) + " -- " + shQuote(query.trimmed().isEmpty() ? QStringLiteral("*") : query.trimmed()) + "\n";
    return s;
}

QByteArray buildMailThreadScript(const QString &threadId, const QString &configPath) {
    if (!validMailThreadId(threadId)) return {};
    QByteArray s = prologue(configPath);
    s += "notmuch show --format=json --body=true --include-html --entire-thread=true -- " + shQuote(QStringLiteral("thread:") + threadId) + "\n";
    return s;
}

QByteArray buildMailTagScript(const QString &threadId, const QStringList &add, const QStringList &remove, const QString &configPath) {
    if (!validMailThreadId(threadId) || (add.isEmpty() && remove.isEmpty())) return {};
    QByteArray args;
    for (const QString &t : add) { if (!validMailTag(t)) return {}; args += " " + shQuote(QStringLiteral("+") + t); }
    for (const QString &t : remove) { if (!validMailTag(t)) return {}; args += " " + shQuote(QStringLiteral("-") + t); }
    QByteArray s = prologue(configPath);
    s += "notmuch tag" + args + " -- " + shQuote(QStringLiteral("thread:") + threadId) + "\n";
    return s;
}

// ---------------------------------------------------------------- parsing
MailOverview parseMailOverview(const QByteArray &output) {
    MailOverview o;
    const auto sec = sections(output);
    if (!sec.contains("end")) { o.problem = QStringLiteral("Réponse incomplète de notmuch."); return o; }
    o.version = QString::fromUtf8(sec.value("version")).trimmed();
    struct F { const char *key, *title, *query; };
    const F folders[] = { { "inbox", "Boîte de réception", "tag:inbox" }, { "unread", "Non lus", "tag:unread" }, { "flagged", "Suivis", "tag:flagged" },
                          { "sent", "Envoyés", "tag:sent" }, { "deleted", "Corbeille", "tag:deleted" }, { "all", "Tous les messages", "*" } };
    bool anyCount = false;
    for (const F &f : folders) {
        const QString raw = QString::fromUtf8(sec.value(QString::fromLatin1("count_") + QLatin1String(f.key))).trimmed();
        bool isNumber = false;
        const int count = raw.toInt(&isNumber);
        anyCount |= isNumber;
        o.folders.push_back(QVariantMap { { "key", QLatin1String(f.key) }, { "title", QString::fromUtf8(f.title) }, { "query", QLatin1String(f.query) }, { "count", isNumber ? count : -1 } });
    }
    if (!anyCount) {
        const QString first = QString::fromUtf8(sec.value("count_all")).trimmed();
        o.problem = describeMailFailure(1, first);
        return o;
    }
    o.ok = true;
    return o;
}

QList<MailThread> parseMailSearch(const QByteArray &json, bool *ok) {
    QList<MailThread> out;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (ok) *ok = error.error == QJsonParseError::NoError && doc.isArray();
    if (!doc.isArray()) return out;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        MailThread t;
        t.id = o.value("thread").toString();
        t.timestamp = toInt64(o.value("timestamp").toDouble());
        t.matched = o.value("matched").toInt();
        t.total = o.value("total").toInt();
        t.authors = o.value("authors").toString();
        t.subject = o.value("subject").toString();
        for (const QJsonValue &tag : o.value("tags").toArray()) t.tags << tag.toString();
        t.unread = t.tags.contains(QLatin1String("unread"));
        t.flagged = t.tags.contains(QLatin1String("flagged"));
        t.attachment = t.tags.contains(QLatin1String("attachment"));
        if (!t.id.isEmpty()) out.push_back(t);
    }
    return out;
}

QVariantList threadsToVariant(const QList<MailThread> &threads) {
    QVariantList out;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const MailThread &t : threads)
        out.push_back(QVariantMap { { "id", t.id }, { "when", mailDateText(t.timestamp, now) }, { "timestamp", t.timestamp }, { "matched", t.matched }, { "total", t.total },
                                    { "authors", t.authors }, { "subject", t.subject.isEmpty() ? QStringLiteral("(sans objet)") : t.subject },
                                    { "tags", t.tags }, { "unread", t.unread }, { "flagged", t.flagged }, { "attachment", t.attachment } });
    return out;
}

QList<MailMessage> parseMailThread(const QByteArray &json, bool *ok) {
    QList<MailMessage> out;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (ok) *ok = error.error == QJsonParseError::NoError && doc.isArray();
    if (!doc.isArray()) return out;
    walk(doc.array(), 0, out);
    return out;
}

QVariantList messagesToVariant(const QList<MailMessage> &messages) {
    QVariantList out;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const MailMessage &m : messages) {
        QVariantList attachments;
        for (const MailAttachment &a : m.attachments) attachments.push_back(QVariantMap { { "name", a.name }, { "type", a.type }, { "size", a.size }, { "part", a.part } });
        out.push_back(QVariantMap { { "id", m.id }, { "depth", qMin(m.depth, 6) }, { "match", m.match }, { "from", m.from }, { "to", m.to }, { "cc", m.cc },
                                    { "date", m.date }, { "when", mailDateText(m.timestamp, now) }, { "subject", m.subject.isEmpty() ? QStringLiteral("(sans objet)") : m.subject },
                                    { "tags", m.tags }, { "unread", m.tags.contains(QLatin1String("unread")) },
                                    { "text", m.text }, { "html", m.html }, { "htmlPlain", m.htmlPlain }, { "hasText", !m.text.isEmpty() }, { "hasHtml", !m.html.isEmpty() },
                                    { "htmlLinks", m.htmlLinks }, { "htmlScripts", m.htmlScripts }, { "htmlImages", m.htmlImages }, { "attachments", attachments }, { "attachmentsOmitted", m.attachmentsOmitted } });
    }
    return out;
}

QString describeMailFailure(int exitCode, const QString &stderrText) {
    const QString err = stderrText.trimmed();
    if (err.contains(QLatin1String("FQ-ERR missing-notmuch"))) return QStringLiteral("notmuch n'est pas installé sur ce serveur (sudo apt install notmuch).");
    const QString low = err.toLower();
    if (low.contains(QLatin1String("not been configured")) || low.contains(QLatin1String("cannot open")) || low.contains(QLatin1String("cannot load config"))
        || low.contains(QLatin1String("cannot read config")) || low.contains(QLatin1String("no such file")))
        return QStringLiteral("notmuch n'est pas configuré ou la base est introuvable : exécutez « notmuch setup » puis « notmuch new » sur le serveur.");
    if (exitCode == 124 || exitCode == 137) return QStringLiteral("Délai dépassé.");
    if (err.isEmpty()) return QStringLiteral("notmuch s'est terminé avec le code %1 sans message.").arg(exitCode);
    return err.section(QLatin1Char('\n'), 0, 3).left(600);
}

QString mailDateText(qint64 timestamp, qint64 nowSeconds) {
    if (timestamp <= 0) return {};
    const QDateTime when = QDateTime::fromSecsSinceEpoch(timestamp);
    const QDateTime now = QDateTime::fromSecsSinceEpoch(nowSeconds);
    if (when.date() == now.date()) return when.toString(QStringLiteral("HH:mm"));
    static const char *months[] = { "janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc." };
    const QString day = QString::number(when.date().day()) + QLatin1Char(' ') + QString::fromUtf8(months[when.date().month() - 1]);
    if (when.date().year() != now.date().year()) return day + QLatin1Char(' ') + QString::number(when.date().year());
    return day + QLatin1Char(' ') + when.toString(QStringLiteral("HH:mm"));
}

}  // namespace fortiq
