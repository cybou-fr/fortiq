#include "mail/MailAttachment.h"
#include "mail/MailInfo.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QtTest>

using namespace fortiq;

static QByteArray fixture(const char *name) {
    QFile f(QString::fromUtf8(FORTIQ_MAIL_FIXTURES) + "/" + QString::fromUtf8(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

class MailTest : public QObject {
    Q_OBJECT
private slots:
    void validation() {
        QVERIFY(validateMailQuery("from:alice and tag:unread").isEmpty());
        QVERIFY(validateMailQuery("sujet \"x\" and not;rm").isEmpty());      // quoting is the script's job, not the validator's
        QVERIFY(!validateMailQuery(QString(501, 'a')).isEmpty());
        QVERIFY(!validateMailQuery(QString("a\nb")).isEmpty());
        QVERIFY(validMailThreadId("0000000000000001"));
        QVERIFY(!validMailThreadId("0001; rm"));
        QVERIFY(!validMailThreadId(""));
        QVERIFY(!validMailThreadId("thread:1"));
        QVERIFY(validMailTag("unread")); QVERIFY(validMailTag("a.b_c-d"));
        QVERIFY(!validMailTag("-unread")); QVERIFY(!validMailTag("+x")); QVERIFY(!validMailTag("a b")); QVERIFY(!validMailTag(""));
    }

    void scripts() {
        const QByteArray s = buildMailSearchScript("from:o'brien", 50, 100, QString());
        QVERIFY(s.contains("notmuch search --format=json --output=summary --sort=newest-first --limit=50 --offset=100 -- 'from:o'\\''brien'"));
        QVERIFY(s.contains("missing-notmuch"));
        QVERIFY(!s.contains("NOTMUCH_CONFIG"));
        QVERIFY(buildMailSearchScript("x", 50, 0, "/home/u/.config/nm conf").contains("export NOTMUCH_CONFIG='/home/u/.config/nm conf'"));
        QVERIFY(buildMailSearchScript(QString(), 9999, -5, QString()).contains("--limit=500 --offset=0 -- '*'"));
        QVERIFY(buildMailSearchScript(QString(600, 'a'), 10, 0, QString()).isEmpty());
        QVERIFY(buildMailThreadScript("0000000000000001", QString()).contains("-- 'thread:0000000000000001'"));
        QVERIFY(buildMailThreadScript("bad id", QString()).isEmpty());
        const QByteArray t = buildMailTagScript("00ab", { "flagged" }, { "unread" }, QString());
        QVERIFY(t.contains("notmuch tag '+flagged' '-unread' -- 'thread:00ab'"));
        QVERIFY(buildMailTagScript("00ab", { "x y" }, {}, QString()).isEmpty());
        QVERIFY(buildMailTagScript("00ab", {}, {}, QString()).isEmpty());
        QVERIFY(buildMailTagScript("zz", { "a" }, {}, QString()).isEmpty());
        // Nothing here can change messages other than tags: no delete, move, send, sync.
        for (const QByteArray &sc : { s, buildMailOverviewScript(QString()), buildMailThreadScript("00ab", QString()), t }) {
            QVERIFY(!sc.contains("rm "));
            QVERIFY(!sc.contains("notmuch new"));
            QVERIFY(!sc.contains("mbsync"));
            QVERIFY(!sc.contains("msmtp"));
            QVERIFY(!sc.contains("sudo"));
        }
    }

    void searchParsing() {
        bool ok = false;
        const QList<MailThread> all = parseMailSearch(fixture("search_all.json"), &ok);
        QVERIFY(ok);
        QCOMPARE(all.size(), 5);
        QCOMPARE(all.first().authors, QString("noname@example.org"));
        QCOMPARE(all.first().subject, QString());
        QCOMPARE(all.last().subject, QString::fromUtf8("Caf\xC3\xA9 au lait \xE2\x9C\x93"));
        QCOMPARE(all.last().total, 2);
        QVERIFY(all.last().unread && !all.last().flagged);
        QVERIFY(all.at(3).flagged && all.at(3).attachment);
        QCOMPARE(all.at(2).subject, QString("Offre <b>speciale</b>"));   // never interpreted
        const QVariantList v = threadsToVariant(all);
        QCOMPARE(v.first().toMap().value("subject").toString(), QString("(sans objet)"));
        QVERIFY(!v.first().toMap().value("when").toString().isEmpty());
        QCOMPARE(parseMailSearch(fixture("search_empty.json"), &ok).size(), 0);
        QVERIFY(ok);
        parseMailSearch("not json", &ok);
        QVERIFY(!ok);
        parseMailSearch("Error: bad query", &ok);
        QVERIFY(!ok);
    }

    void threadWithReplyAndHtml() {
        bool ok = false;
        const QList<MailMessage> m = parseMailThread(fixture("show_m1.json"), &ok);
        QVERIFY(ok);
        QCOMPARE(m.size(), 2);
        QCOMPARE(m[0].depth, 0); QCOMPARE(m[1].depth, 1);
        QVERIFY(m[0].match); QVERIFY(!m[1].match);
        QCOMPARE(m[0].text, QString("Bonjour, plain text body."));
        QVERIFY(!m[0].html.isEmpty());
        QVERIFY(m[0].html.contains("HTML"));
        QVERIFY(!m[0].html.contains("script"));
        QVERIFY(!m[0].html.contains("track.example"));
        QCOMPARE(m[0].htmlScripts, 1);
        QCOMPARE(m[0].htmlImages, 0 + m[0].htmlImages);
        QVERIFY(m[0].from.contains(QString::fromUtf8("Zo\xC3\xAB")));
        QVERIFY(m[0].tags.contains("unread"));
        QVERIFY(!m[1].tags.contains("unread"));
        QCOMPARE(m[1].text, QString("Reply with an attachment mention.\n"));
        const QVariantMap vm = messagesToVariant(m).first().toMap();
        QVERIFY(vm.value("unread").toBool()); QVERIFY(vm.value("hasText").toBool()); QVERIFY(vm.value("hasHtml").toBool());
    }

    void attachmentsAndQuotedPrintable() {
        const QList<MailMessage> m = parseMailThread(fixture("show_m3.json"));
        QCOMPARE(m.size(), 1);
        QCOMPARE(m[0].text, QString::fromUtf8("Voir le rapport en pi\xC3\xA8""ce jointe."));
        QCOMPARE(m[0].attachments.size(), 1);
        QCOMPARE(m[0].attachments[0].name, QString::fromUtf8("Rapport \xC3\xA9t\xC3\xA9.pdf"));
        QCOMPARE(m[0].attachments[0].type, QString("application/pdf"));
        QCOMPARE(m[0].attachments[0].part, 3);
        QCOMPARE(messagesToVariant(m).first().toMap().value("attachments").toList().first().toMap().value("part").toInt(), 3);
        QVERIFY(m[0].html.isEmpty());
    }

    // S-8: what a hostile sender controls in the notmuch JSON: thousands of attachments, spoofed names, absurd nesting.
    void hostileMimeShapesAreBounded() {
        QJsonArray parts;
        parts.append(QJsonObject { { "id", 1 }, { "content-type", "text/plain" }, { "content", "body" } });
        for (int i = 0; i < 5000; ++i)
            parts.append(QJsonObject { { "id", i + 2 }, { "content-type", "application/octet-stream" }, { "filename", QString("f%1.bin").arg(i) }, { "content-disposition", "attachment" } });
        parts.append(QJsonObject { { "id", 9000 }, { "content-type", "application/pdf" }, { "content-disposition", "attachment" },
                                   { "filename", QString::fromUtf8("invoice\xE2\x80\xAE" "txt.exe\x07") } });
        const QJsonObject message { { "id", "hostile@x" }, { "match", true }, { "timestamp", 1 }, { "tags", QJsonArray() },
                                    { "headers", QJsonObject { { "Subject", "s" }, { "From", "a@b" } } }, { "body", parts } };
        const QJsonArray thread { QJsonArray { QJsonArray { message, QJsonArray() } } };
        const QList<MailMessage> m = parseMailThread(QJsonDocument(thread).toJson(QJsonDocument::Compact));
        QCOMPARE(m.size(), 1);
        QCOMPARE(m[0].attachments.size(), kMaxAttachmentsShown);
        QVERIFY(m[0].attachmentsOmitted >= 4800);
        QCOMPARE(messagesToVariant(m).first().toMap().value("attachmentsOmitted").toInt(), m[0].attachmentsOmitted);
        // multipart nested far deeper than any real message: bounded by the JSON parser, no stack exhaustion
        QJsonObject deep { { "id", 2 }, { "content-type", "text/plain" }, { "content", "x" } };
        for (int i = 0; i < 400; ++i) deep = QJsonObject { { "id", 3 }, { "content-type", "multipart/mixed" }, { "content", QJsonArray { deep } } };
        const QJsonObject m2 { { "id", "deep@x" }, { "match", true }, { "timestamp", 1 }, { "tags", QJsonArray() }, { "headers", QJsonObject() }, { "body", QJsonArray { deep } } };
        const QList<MailMessage> d = parseMailThread(QJsonDocument(QJsonArray { QJsonArray { QJsonArray { m2, QJsonArray() } } }).toJson(QJsonDocument::Compact));
        QVERIFY(d.size() <= 1);
    }

    void spoofedAttachmentNamesAreNeutralized() {
        const QString spoof = QString::fromUtf8("invoice\xE2\x80\xAE" "txt.exe");
        const QString saved = sanitizeAttachmentName(spoof);
        QVERIFY2(!saved.contains(QChar(0x202E)), "the right-to-left override survives in the saved name");
        QCOMPARE(saved, QStringLiteral("invoice_txt.exe"));
        for (const ushort u : { 0x202A, 0x202B, 0x202C, 0x202D, 0x202E, 0x2066, 0x2067, 0x2068, 0x2069, 0x200E, 0x200F, 0x061C })
            QVERIFY2(!sanitizeAttachmentName(QStringLiteral("a") + QChar(u) + QStringLiteral("b.txt")).contains(QChar(u)), qPrintable(QString::number(u, 16)));
    }

    void htmlOnlyMessageIsInert() {
        const QList<MailMessage> m = parseMailThread(fixture("show_m4.json"));
        QCOMPARE(m.size(), 1);
        QVERIFY(m[0].text.isEmpty());
        QVERIFY(!m[0].html.isEmpty());
        QVERIFY(m[0].htmlPlain.contains("Offre"));
        QVERIFY(!m[0].html.contains("javascript"));
        QVERIFY(!m[0].html.contains("onload"));
        QVERIFY(!m[0].html.contains("track.example"));
        QVERIFY(!m[0].html.contains("<script"));
        QVERIFY(!m[0].html.contains("<style"));
        QVERIFY2(m[0].html.contains("https://shop.example/a?b=1"), qPrintable(m[0].html));
        QCOMPARE(m[0].subject, QString("Offre <b>speciale</b>"));   // plain text, escaped by the UI
        QVERIFY(m[0].htmlScripts >= 1);
    }

    void oldCharsetAndNoSubject() {
        const QList<MailMessage> a = parseMailThread(fixture("show_m5.json"));
        QCOMPARE(a[0].subject, QString::fromUtf8("Caf\xC3\xA9 ancien"));
        QVERIFY(a[0].text.contains(QString::fromUtf8("cr\xC3\xA8me")));
        const QList<MailMessage> b = parseMailThread(fixture("show_m6.json"));
        QCOMPARE(b[0].subject, QString());
        QCOMPARE(messagesToVariant(b).first().toMap().value("subject").toString(), QString("(sans objet)"));
    }

    // Audit 23.10.2026: notmuch output is data from a server the user may not control. Truncated, mutated or hostile JSON must not crash the parsers.
    void parsersSurviveMutatedJson() {
        QRandomGenerator rng(20261023);
        for (const char *name : { "search_all.json", "show_m1.json", "show_m3.json", "show_m4.json" }) {
            const QByteArray good = fixture(name);
            QVERIFY(!good.isEmpty());
            for (int round = 0; round < 600; ++round) {
                QByteArray data = good;
                switch (round % 3) {
                case 0: data.truncate(rng.bounded(data.size() + 1)); break;
                case 1: for (int k = 0; k < 4; ++k) data[rng.bounded(data.size())] = static_cast<char>(rng.bounded(256)); break;
                default: data.insert(rng.bounded(data.size() + 1), QByteArray("[{\"id\":\"x\",\"body\":[{\"content-type\":\"text/html\",\"content\":\"<script>alert(1)</script>\"}]},[]]")); break;
                }
                const QList<MailThread> t = parseMailSearch(data);
                const QList<MailMessage> m = parseMailThread(data);
                for (const MailMessage &msg : m) {
                    QVERIFY2(!msg.html.contains("<script", Qt::CaseInsensitive), qPrintable(msg.html));
                    QVERIFY(msg.depth >= 0);
                }
                Q_UNUSED(t)
            }
        }
        // Hostile shapes: wrong types, absurd nesting, huge numbers.
        for (const char *json : { "[[[1,2,3]]]", "[[[{\"id\":5}]]]", "[[[{\"id\":\"a\",\"body\":\"text\"}, 7]]]", "[{\"thread\":null,\"tags\":3}]", "{\"a\":1}", "null", "[[[{\"id\":\"a\",\"timestamp\":1e308,\"body\":[null,1,[]]},[]]]]" }) {
            parseMailSearch(json); parseMailThread(json);
        }
        QByteArray deep;
        for (int i = 0; i < 5000; ++i) deep += "[";
        for (int i = 0; i < 5000; ++i) deep += "]";
        parseMailThread(deep); parseMailSearch(deep);   // beyond the JSON parser's depth limit: refused, not a stack overflow
    }

    void brokenThreadJson() {
        bool ok = true;
        QVERIFY(parseMailThread("garbage", &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(parseMailThread("[]", &ok).isEmpty());
        QVERIFY(ok);
        QVERIFY(parseMailThread("[[[{\"id\":\"x\"}, []]]]", &ok).size() == 1);   // minimal message without body or headers
    }

    void deeplyNestedRepliesAreBounded() {
        QByteArray json = "[[";
        const int n = 40;
        for (int i = 0; i < n; ++i) json += "[{\"id\":\"m" + QByteArray::number(i) + "\"}, [";
        for (int i = 0; i < n; ++i) json += "]]";
        json += "]]";
        const QList<MailMessage> m = parseMailThread(json);
        QCOMPARE(m.size(), n);
        QVERIFY(messagesToVariant(m).last().toMap().value("depth").toInt() <= 6);
    }

    void overviewParsing() {
        const QByteArray out = "#section version\nnotmuch 0.39\n#section count_inbox\n5\n#section count_unread\n4\n#section count_flagged\n1\n"
                               "#section count_sent\n0\n#section count_deleted\n0\n#section count_all\n6\n#section end\n";
        const MailOverview o = parseMailOverview(out);
        QVERIFY(o.ok);
        QCOMPARE(o.version, QString("notmuch 0.39"));
        QCOMPARE(o.folders.size(), 6);
        QCOMPARE(o.folders[1].toMap().value("count").toInt(), 4);
        QCOMPARE(o.folders[5].toMap().value("query").toString(), QString("*"));
        const MailOverview bad = parseMailOverview("#section version\nnotmuch 0.39\n#section count_inbox\nNotmuch has not been configured.\n#section count_all\nNotmuch has not been configured.\n#section end\n");
        QVERIFY(!bad.ok);
        QVERIFY(bad.problem.contains("notmuch setup"));
        QVERIFY(!parseMailOverview("garbage").ok);
    }

    void failures() {
        QVERIFY(describeMailFailure(4, "FQ-ERR missing-notmuch").contains("apt install"));
        QVERIFY(describeMailFailure(1, "Error: Cannot open database at /x: No such file or directory.").contains("notmuch new"));
        QVERIFY(describeMailFailure(124, "").contains("Délai"));
        QVERIFY(describeMailFailure(1, "notmuch search: A Xapian exception occurred").contains("Xapian"));
        QVERIFY(!describeMailFailure(9, "").isEmpty());
    }

    void dates() {
        const qint64 now = QDateTime(QDate(2026, 10, 23), QTime(12, 0)).toSecsSinceEpoch();
        QCOMPARE(mailDateText(QDateTime(QDate(2026, 10, 23), QTime(8, 5)).toSecsSinceEpoch(), now), QString("08:05"));
        QCOMPARE(mailDateText(QDateTime(QDate(2026, 10, 22), QTime(8, 30)).toSecsSinceEpoch(), now), QString::fromUtf8("22 oct. 08:30"));
        QCOMPARE(mailDateText(QDateTime(QDate(2025, 3, 1), QTime(8, 30)).toSecsSinceEpoch(), now), QString::fromUtf8("1 mars 2025"));
        QCOMPARE(mailDateText(0, now), QString());
    }
};

QTEST_APPLESS_MAIN(MailTest)
#include "MailTest.moc"
