// Fuzz targets for everything FORTIQ parses or sanitizes that comes from outside: web pages, server output, user text.
// Two drivers share this file (CMake, option FORTIQ_FUZZ / target fortiq_fuzz_smoke):
//   * libFuzzer (clang, FORTIQ_LIBFUZZER):  first input byte selects the target, the rest is the data.
//   * smoke driver (any compiler):          replays seed files and a fixed number of seeded mutations through EVERY target;
//                                           this is the ctest `fortiq_fuzz_smoke`, so a regression fails on every platform.
// A target fails by crashing, by a sanitizer report, or by `invariant()` (abort with a message): the checks below are the
// security claims made in docs (inert HTML, one read-only SQL statement, no header injection, safe file names, honest URLs).
#include <QByteArray>
#include <QDateTime>
#include <QRegularExpression>
#include <QString>
#include <QTimeZone>
#include <QStringList>
#include <QUrl>

#include <cstdio>
#include <cstdlib>

#include "browser/HtmlSanitizer.h"
#include "browser/UrlPolicy.h"
#include "browser/UrlTools.h"
#include "db/DbInfo.h"
#include "mail/MailAccount.h"
#include "mail/MailAttachment.h"
#include "mail/MailInfo.h"
#include "mail/MailSend.h"
#include "network/NetworkInfo.h"
#include "system/SystemInfo.h"

#include "fuzz_targets.h"

namespace fortiq::fuzz {

[[noreturn]] static void invariant(const char *what, const QString &detail) {
    std::fprintf(stderr, "INVARIANT BROKEN: %s\n  %s\n", what, qPrintable(detail.left(600)));
    std::abort();
}
#define CHECK(cond, detail) do { if (!(cond)) invariant(#cond, (detail)); } while (0)

static QString text(const QByteArray &d) { return QString::fromUtf8(d); }
// Two strings from one input: split at the first NUL.
static void split2(const QByteArray &d, QString *a, QString *b) {
    const int i = d.indexOf('\0');
    *a = text(i < 0 ? d : d.left(i));
    *b = i < 0 ? QString() : text(d.mid(i + 1));
}

static bool isHttpUrl(const QString &u) { return u.startsWith("http://") || u.startsWith("https://"); }

static void htmlSanitizer(const QByteArray &d, bool reader) {
    QString html, base; split2(d, &html, &base);
    const SanitizedPage page = sanitizeHtml(html, base.isEmpty() ? QStringLiteral("https://example.org/a/b") : base, reader);
    // Inert subset: no active or loading element, only the documented attributes, only http(s) links.
    static const QRegularExpression forbidden(QStringLiteral("<\\s*/?\\s*(script|style|iframe|frame|object|embed|img|video|audio|source|form|input|button|link|meta|svg|base)\\b"),
                                              QRegularExpression::CaseInsensitiveOption);
    CHECK(!forbidden.match(page.html).hasMatch(), page.html);
    // (a "javascript:" inside the PATH of an https link is harmless; the scheme of every href is checked below)
    static const QRegularExpression tag(QStringLiteral("<([a-zA-Z][a-zA-Z0-9]*)([^<>]*)>"));
    static const QRegularExpression attr(QStringLiteral("\\s([a-zA-Z_:][-a-zA-Z0-9_:.]*)\\s*=\\s*(\"([^\"]*)\"|'([^']*)')"));
    for (auto it = tag.globalMatch(page.html); it.hasNext();) {
        const auto m = it.next();
        const QString attrs = m.captured(2);
        QString rest = attrs;
        for (auto at = attr.globalMatch(attrs); at.hasNext();) {
            const auto a = at.next();
            const QString name = a.captured(1).toLower();
            const QString value = a.captured(3) + a.captured(4);
            CHECK(name == "href" || name == "colspan" || name == "rowspan" || name == "start", page.html);
            if (name == "href") CHECK(isHttpUrl(value), value);
            else CHECK(QRegularExpression(QStringLiteral("^\\d{1,4}$")).match(value).hasMatch(), value);
            rest.remove(a.captured(0));
        }
        CHECK(rest.trimmed().isEmpty() || rest.trimmed() == "/", page.html);   // nothing else survives inside a tag
    }
    if (!page.refreshUrl.isEmpty()) (void)resolveLink(base, page.refreshUrl);
    (void)decodeEntities(html);
}

static void sql(const QByteArray &d, DbEngine engine) {
    const QString s = text(d);
    const SqlScan scan = scanSql(s, engine);
    const QString refusal = readOnlyRefusal(s, engine);
    if (refusal.isEmpty()) {
        // What may be sent: one statement, closed, no client meta-command, a read keyword.
        CHECK(scan.statements <= 1 && !scan.unterminated && !scan.metaCommand, s);
        static const QStringList allowed = { "", "select", "with", "values", "explain", "show", "table", "pragma" };
        CHECK(allowed.contains(scan.firstKeyword), s + " -> " + scan.firstKeyword);
    }
    DbProfile p; p.engine = engine; p.path = "/tmp/x.db"; p.user = "u"; p.database = "d";
    if (validateProfile(p).isEmpty() && refusal.isEmpty()) {
        DbLimits limits;
        const QByteArray script = engine == DbEngine::Sqlite ? buildSqliteScript(p, s, limits, "FQDELIM") : buildSchemaScript(p, "pw", limits, "NULL", "FQDELIM");
        CHECK(!script.isEmpty(), s);
    }
}

static void profile(const QByteArray &d) {
    QString a, b; split2(d, &a, &b);
    DbProfile p; p.engine = (d.size() % 2) ? DbEngine::Postgres : DbEngine::Sqlite;
    p.path = a; p.host = b; p.user = a.left(8); p.database = b.left(8);
    if (validateProfile(p).isEmpty()) {
        DbLimits limits;
        (void)buildRoleCheckScript(p, "pw", limits, "NULL", "FQDELIM");
        (void)profileToVariant(p);
    }
    (void)quoteIdentifier(a);
    const QString q = quoteIdentifier(a);
    CHECK(q.size() >= 2 && q.startsWith('"') && q.endsWith('"'), q);
}

static void urlTools(const QByteArray &d) {
    QString a, b; split2(d, &a, &b);
    const UrlCheck c = normalizeUrl(a);
    if (c.ok) {
        const QUrl u(c.url);
        CHECK(isHttpUrl(c.url) && u.isValid() && u.userInfo().isEmpty() && !u.host().isEmpty(), c.url);
        for (QChar ch : c.url) CHECK(ch.unicode() > 0x20 && ch.unicode() != 0x7f, c.url);
        const HostPlan plan = planHost(c.url);
        (void)plan;
        const QString r = resolveLink(c.url, b);
        CHECK(r.isEmpty() || isHttpUrl(r), r);
    }
}

static void policy(const QByteArray &d) {
    QStringList ips;
    for (const QByteArray &line : d.split('\n')) ips << text(line);
    const AddressVerdict v = classifyAddress(ips.value(0));
    (void)v;
    const PolicyDecision dec = decideAddresses(ips);
    if (dec.kind == PolicyDecision::Kind::Block) CHECK(dec.pinIp.isEmpty(), ips.join(","));
    QString problem;
    (void)parseResolveOutput(d, &problem);
    (void)buildResolveScript(text(d));
    (void)curlResolveSpec(text(d), 443, ips.value(0));
}

static void mailSend(const QByteArray &d) {
    QString a, b; split2(d, &a, &b);
    QList<MailAddress> list; QString err;
    if (parseMailAddresses(a, &list, &err)) {
        for (const MailAddress &m : list) {
            CHECK(!m.addr.isEmpty(), a);
            for (QChar ch : m.addr) CHECK(ch.unicode() > 0x20 && ch.unicode() < 0x7f && ch != '<' && ch != '>' && ch != '"', m.addr);
        }
    }
    MailDraft draft;
    draft.account = "acc"; draft.fromAddress = "me@example.org"; draft.fromName = b.left(20);
    draft.to = a; draft.cc = b.left(40); draft.bcc = b.mid(40, 40);
    draft.subject = b; draft.body = a + b; draft.inReplyTo = b.left(30);
    const BuiltMessage m = buildMessage(draft, QDateTime::fromMSecsSinceEpoch(1700000000000LL, QTimeZone::utc()), "abcdef0123456789");
    if (m.ok) {
        const int end = m.data.indexOf("\n\n");
        CHECK(end > 0, QString::fromLatin1(m.data.left(300)));
        const QList<QByteArray> lines = m.data.left(end).split('\n');
        CHECK(!m.data.left(end).contains('\r'), QString::fromLatin1(m.data.left(end)));
        for (const QByteArray &line : lines) {
            const bool continuation = line.startsWith(' ') || line.startsWith('\t');
            const bool header = QRegularExpression(QStringLiteral("^[A-Za-z][A-Za-z0-9-]*: ")).match(QString::fromLatin1(line)).hasMatch();
            CHECK(continuation || header, QString::fromLatin1(line));
            CHECK(!line.startsWith("Bcc:") && !line.startsWith("bcc:"), QString::fromLatin1(line));   // blind copies never appear in the message
        }
        // RFC 5322 atext such as & ; $ ' is legal in an address; the script single-quotes every recipient after `--` (shQuote).
        for (const QString &r : m.recipients) {
            CHECK(!r.contains(QRegularExpression("[\\s\\x00-\\x1f<>\"\\\\]")) && r.count('@') == 1, r);
            const QByteArray script = buildMailSendScript("acc", QStringList{ r }, "x\n", QString(), QString(), "FQMSGDELIM");
            CHECK(script.contains(" -- '"), r);
        }
        CHECK(validMessageId(m.messageId), m.messageId);
    }
    (void)encodeHeaderText(a);
    (void)replySubject(a);
    (void)replyBody(a, b, a);
    (void)validMailAccountName(a);
    (void)parseMailAccounts(d);
    bool delivered = false, unknown = false;
    (void)describeSendOutcome(int(d.size()) % 70, a, &delivered, &unknown);
}

static void mailAccount(const QByteArray &d) {
    QString a, b; split2(d, &a, &b);
    const QString bad = validatePassword(a);
    (void)bad;
    (void)parseSetupProbe(d);
    (void)parseSetupWritten(d);
    (void)buildSetupProbeScript(a, b);
    (void)buildSmtpTestScript(a);
    (void)buildImapTestScript(a);
    (void)buildSyncScript(a, b);
    (void)describeActionFailure(a, int(d.size()), b);
    const QString name = sanitizeAttachmentName(a);
    CHECK(!name.isEmpty() && name != "." && name != "..", a);
    CHECK(!name.contains(QRegularExpression("[/\\\\:\\x00-\\x1f*?\"<>|]")), name);
    (void)buildAttachmentSaveScript(a, int(d.size() % 9), b, "save", QString());
    const AttachmentSaved saved = parseAttachmentSaved(d);
    (void)saved;
    (void)isAttachmentTempDirectory(a);
    (void)buildAttachmentCleanupScript(a);
}

static void mailInfo(const QByteArray &d) {
    QString a, b; split2(d, &a, &b);
    bool ok = false;
    (void)parseMailOverview(d);
    (void)threadsToVariant(parseMailSearch(d, &ok));
    (void)messagesToVariant(parseMailThread(d, &ok));
    (void)describeMailFailure(int(d.size()) % 256, a);
    (void)mailDateText(qint64(d.size()) * 1000003, 1700000000);
    const QString problem = validateMailQuery(a);
    if (problem.isEmpty()) (void)buildMailSearchScript(a, 20, 0, QString());
    if (validMailThreadId(a)) (void)buildMailThreadScript(a, QString());
    (void)validMailTag(b);
    (void)buildMailTagScript(a, QStringList{ b }, QStringList{ a }, QString());
}

static void dbParsers(const QByteArray &d) {
    (void)parsePostgresCsv(d, "\x01NULL", 200, false);
    (void)parsePostgresCsv(d, "", 3, true);
    (void)parseSqliteQuote(d, 200, false);
    (void)parseSqliteQuote(d, 2, true);
    (void)parseSchema(DbEngine::Postgres, d, "\x01NULL");
    (void)parseSchema(DbEngine::Sqlite, d, "\x01NULL");
    (void)describeDbFailure(DbEngine::Postgres, int(d.size()) % 130, text(d), 15);
    DbProfile p; p.engine = DbEngine::Postgres; p.user = "u"; p.database = "d";
    (void)assessRoleCheck(p, d, "\x01NULL");
    p.engine = DbEngine::Sqlite; p.path = "/x.db";
    (void)assessRoleCheck(p, d, "\x01NULL");
}

static void netSystem(const QByteArray &d) {
    (void)parseNetSnapshot(d);
    (void)parseIpAddrJson(d);
    QList<NetInterface> ifs = parseIpAddrJson(d);
    mergeLinkStats(ifs, d);
    (void)parseIpRouteJson(d, "inet");
    (void)parseSs(d);
    QStringList servers, search; QString note;
    parseResolvConf(d, servers, search, note);
    QString err;
    (void)buildProbeScript("ping", text(d), &err);
    (void)isValidProbeTarget(text(d));
    (void)parseSystemSnapshot(d);
    (void)parsePs(d);
    (void)parseServices(d);
    SystemOverview ov;
    parseOverviewSection("os", d, ov);
    parseOverviewSection("memory", d, ov);
    parseOverviewSection("uptime", d, ov);
}

struct Entry { const char *name; void (*run)(const QByteArray &); };
static const Entry kEntries[] = {
    { "html",         [](const QByteArray &d) { htmlSanitizer(d, false); } },
    { "html-reader",  [](const QByteArray &d) { htmlSanitizer(d, true); } },
    { "sql-sqlite",   [](const QByteArray &d) { sql(d, DbEngine::Sqlite); } },
    { "sql-postgres", [](const QByteArray &d) { sql(d, DbEngine::Postgres); } },
    { "db-profile",   profile },
    { "db-parsers",   dbParsers },
    { "url",          urlTools },
    { "policy",       policy },
    { "mail-send",    mailSend },
    { "mail-account", mailAccount },
    { "mail-info",    mailInfo },
    { "net-system",   netSystem },
};

int targetCount() { return int(sizeof(kEntries) / sizeof(kEntries[0])); }
const char *targetName(int i) { return kEntries[i].name; }
void runTarget(int i, const QByteArray &data) { kEntries[i].run(data); }

}  // namespace fortiq::fuzz

#ifdef FORTIQ_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;
    const int i = data[0] % fortiq::fuzz::targetCount();
    fortiq::fuzz::runTarget(i, QByteArray(reinterpret_cast<const char *>(data) + 1, int(size - 1)));
    return 0;
}
#endif
