#include "mail/MailAccount.h"
#include "mail/MailAttachment.h"
#include "mail/MailSend.h"
#include "mail/OutgoingAttachment.h"
#include <QJsonDocument>
#include <QJsonObject>

#include <QTimeZone>
#include <QtTest>

using namespace fortiq;

static QString decodeWords(const QString &encoded) {
    QString out;
    for (const QString &w : encoded.split(QStringLiteral("\n "))) {
        if (!w.startsWith("=?UTF-8?B?") || !w.endsWith("?=")) return QStringLiteral("<<bad word>>");
        out += QString::fromUtf8(QByteArray::fromBase64(w.mid(10, w.size() - 12).toLatin1()));
    }
    return out;
}

static MailAccountSpec goodSpec() {
    MailAccountSpec s;
    s.name = "perso"; s.displayName = QString::fromUtf8("Zo\xC3\xAB"); s.email = "me@example.net";
    s.imapHost = "imap.example.net"; s.imapPort = 993; s.imapUser = "me@example.net"; s.imapSecurity = "tls";
    s.smtpHost = "smtp.example.net"; s.smtpPort = 587; s.smtpUser = "me@example.net"; s.smtpSecurity = "starttls";
    s.password = QString("p\"a b#c\\d'e$f;g");
    return s;
}


static QByteArray buildMailWriteForTest(const QList<MailSetupFile> &files) {
    return buildSetupWriteScript("perso", files, "20261023120000", "FQW");
}

class MailSendTest : public QObject {
    Q_OBJECT
private slots:
    void addressParsing() {
        QList<MailAddress> a;
        QString err;
        QVERIFY(parseMailAddresses("a@b.org, Name <c@d.org>; \"Doe, J\" <e@f.org> , ", &a, &err));
        QCOMPARE(a.size(), 3);
        QCOMPARE(a[0].addr, QString("a@b.org"));
        QCOMPARE(a[1].name, QString("Name"));
        QCOMPARE(a[2].name, QString("Doe, J"));
        QCOMPARE(a[2].addr, QString("e@f.org"));
        QVERIFY(parseMailAddresses(QString(), &a, &err)); QVERIFY(a.isEmpty());
        QVERIFY(parseMailAddresses("o'brien@example.org", &a, &err));
        for (const char *bad : { "nobody", "a@b", "a@b..org", "x@y.org\nBcc: z@w.org", "Name <a@b.org", "\"open <a@b.org>", "a b@c.org", "zoë@example.org", "a@b.org>", "-oQ/tmp@evil.example", "-X@b.org" }) {
            QVERIFY2(!parseMailAddresses(QString::fromUtf8(bad), &a, &err), bad);
            QVERIFY(!err.isEmpty());
        }
        QVERIFY(!parseMailAddresses(QString(30, 'a') + QString("@") + QString(300, 'b') + ".org", &a, &err));
    }

    void addressFormatting() {
        QCOMPARE(formatMailAddress({ QString(), "a@b.org" }), QString("a@b.org"));
        QCOMPARE(formatMailAddress({ "Alice Doe", "a@b.org" }), QString("Alice Doe <a@b.org>"));
        QCOMPARE(formatMailAddress({ "Doe, J", "a@b.org" }), QString("\"Doe, J\" <a@b.org>"));
        QCOMPARE(formatMailAddress({ "Say \"hi\"", "a@b.org" }), QString("\"Say \\\"hi\\\"\" <a@b.org>"));
        const QString enc = formatMailAddress({ QString::fromUtf8("Zo\xC3\xAB M\xC3\xBCller"), "z@b.org" });
        QVERIFY(enc.contains("=?UTF-8?B?") && enc.endsWith(" <z@b.org>"));
        QCOMPARE(decodeWords(enc.left(enc.size() - 10)), QString::fromUtf8("Zo\xC3\xAB M\xC3\xBCller"));
    }

    void headerEncoding() {
        QCOMPARE(encodeHeaderText("Plain subject"), QString("Plain subject"));
        const QString text = QString::fromUtf8("Caf\xC3\xA9 au lait \xE2\x9C\x93");
        QCOMPARE(decodeWords(encodeHeaderText(text)), text);
        // Long text of two-byte letters: every word stays short and no UTF-8 sequence is split.
        const QString longText = QString(120, QChar(0x00E9)) + QString::fromUtf8("\xF0\x9F\x98\x80") + QString(40, QChar(0x00E8));
        const QString enc = encodeHeaderText(longText);
        for (const QString &w : enc.split("\n ")) QVERIFY(w.size() <= 75);
        QCOMPARE(decodeWords(enc), longText);
    }

    void buildsMessage() {
        MailDraft d;
        d.account = "perso"; d.fromAddress = "me@example.net"; d.fromName = QString::fromUtf8("Zo\xC3\xAB");
        d.to = "bob@example.com, Alice <alice@example.org>"; d.cc = "cc@example.org"; d.bcc = "secret@example.org, bob@example.com";
        d.subject = QString::fromUtf8("R\xC3\xA9sum\xC3\xA9 du jour"); d.body = QString::fromUtf8("Bonjour\r\nligne \xC3\xA9\rfin");
        d.inReplyTo = "orig123@example.org";
        const QDateTime when(QDate(2026, 10, 23), QTime(14, 5, 9), QTimeZone::fromSecondsAheadOfUtc(7200));
        const BuiltMessage m = buildMessage(d, when, "abc123");
        QVERIFY2(m.ok, qPrintable(m.error));
        QCOMPARE(m.messageId, QString("abc123@example.net"));
        QCOMPARE(m.recipients, QStringList({ "bob@example.com", "alice@example.org", "cc@example.org", "secret@example.org" }));
        const QString text = QString::fromUtf8(m.data);
        QVERIFY(text.contains("To: bob@example.com,\n Alice <alice@example.org>\n"));
        QVERIFY(text.contains("Cc: cc@example.org\n"));
        QVERIFY(!text.contains("secret@example.org"));              // Bcc is envelope only
        QVERIFY(!text.contains("Bcc"));
        QVERIFY(text.contains("Date: Fri, 23 Oct 2026 14:05:09 +0200\n"));
        QVERIFY(text.contains("Message-ID: <abc123@example.net>\n"));
        QVERIFY(text.contains("In-Reply-To: <orig123@example.org>\nReferences: <orig123@example.org>\n"));
        QVERIFY(text.contains("Content-Type: text/plain; charset=utf-8\nContent-Transfer-Encoding: base64\n\n"));
        QVERIFY(!text.contains(QChar('\r')));
        const QByteArray body = QByteArray::fromBase64(m.data.mid(m.data.indexOf("\n\n") + 2).replace("\n", ""));
        QCOMPARE(QString::fromUtf8(body), QString::fromUtf8("Bonjour\nligne \xC3\xA9\nfin\n"));
        for (const QByteArray &line : m.data.split('\n')) QVERIFY(line.size() <= 998);
        const int bodyStart = m.data.indexOf("\n\n") + 2;
        for (const QByteArray &line : m.data.mid(bodyStart).split('\n')) QVERIFY(line.size() <= 76);
        QVERIFY(text.contains("From: =?UTF-8?B?"));                  // non-ASCII display name is encoded
    }

    void refusesBadDrafts() {
        const QDateTime when = QDateTime::currentDateTime();
        MailDraft ok; ok.account = "perso"; ok.fromAddress = "me@example.net"; ok.to = "a@b.org"; ok.subject = "s"; ok.body = "b";
        QVERIFY(buildMessage(ok, when, "t").ok);
        auto bad = [&](auto edit) { MailDraft d = ok; edit(d); return buildMessage(d, when, "t"); };
        QVERIFY(!bad([](MailDraft &d) { d.account = "bad name"; }).ok);
        QVERIFY(!bad([](MailDraft &d) { d.to.clear(); }).ok);
        QVERIFY(!bad([](MailDraft &d) { d.to = "a@b.org\nBcc: x@y.org"; }).ok);
        QVERIFY(!bad([](MailDraft &d) { d.subject = "line1\nBcc: x@y.org"; }).ok);
        QVERIFY(!bad([](MailDraft &d) { d.fromName = "x\ny"; }).ok);
        QVERIFY(!bad([](MailDraft &d) { d.inReplyTo = "x y"; }).ok);
        QVERIFY(!bad([](MailDraft &d) { d.fromAddress = "not an address"; }).ok);
        QVERIFY(!bad([](MailDraft &d) { d.body = QString(1024 * 1024 + 1, 'x'); }).ok);
        QVERIFY(!bad([](MailDraft &d) { d.to.clear(); for (int i = 0; i < 51; ++i) d.to += QString("u%1@b.org,").arg(i); }).ok);
        QVERIFY(!buildMessage(ok, when, QString()).ok);
        QVERIFY(bad([](MailDraft &d) { d.to.clear(); d.bcc = "x@y.org"; }).ok);   // Bcc only is allowed
    }

    void sendScript() {
        MailDraft d; d.account = "perso"; d.fromAddress = "me@example.net"; d.to = "o'brien@example.org"; d.subject = "x"; d.body = "y";
        const BuiltMessage m = buildMessage(d, QDateTime::currentDateTime(), "tok");
        QVERIFY(m.ok);
        const QByteArray s = buildMailSendScript("perso", m.recipients, m.data, "orig@example.org", QString(), "FQDELIM1");
        QVERIFY(s.contains("timeout -k 5 90 msmtp -a 'perso' -- 'o'\\''brien@example.org' < \"$t\""));
        QVERIFY(s.contains("cat > \"$t\" <<'FQDELIM1'\nFrom: me@example.net\n"));
        QVERIFY(s.contains("\nFQDELIM1\n"));
        QVERIFY(s.contains("t=$(umask 077; mktemp)"));
        QVERIFY(s.contains("notmuch insert --create-folder --folder=Sent +sent -unread < \"$t\""));
        QVERIFY(s.contains("notmuch tag +replied -- 'id:orig@example.org'"));
        QVERIFY(!s.contains("rm -rf")); QVERIFY(!s.contains("sudo")); QVERIFY(!s.contains("PASSWORD")); QVERIFY(!s.contains("mbsync"));
        QVERIFY(buildMailSendScript("perso", m.recipients, m.data, QString(), "/home/u/nm conf", "FQDX").contains("NOTMUCH_CONFIG='/home/u/nm conf'"));
        QVERIFY(!buildMailSendScript("perso", m.recipients, m.data, QString(), QString(), "FQDX").contains("replied"));
        QVERIFY(buildMailSendScript("bad name", m.recipients, m.data, QString(), QString(), "FQDX").isEmpty());
        QVERIFY(buildMailSendScript("perso", {}, m.data, QString(), QString(), "FQDX").isEmpty());
        QVERIFY(buildMailSendScript("perso", m.recipients, m.data, QString(), QString(), "tok").isEmpty());   // delimiter occurs in the message
        QVERIFY(buildMailSendScript("perso", m.recipients, m.data, QString(), QString(), QString()).isEmpty());
    }

    void outgoingMetadataIsBoundedAndTiedToTheSelectedFile() {
        QVariantMap a{{"path",QString::fromUtf8("/home/u/rapport été.bin")},{"name",QString::fromUtf8("rapport été.bin")},
            {"size",3},{"mtimeNs","1790000000000000000"},{"device","2049"},{"inode","1234"},{"sha256",QString(64,'a')}};
        auto json=[&](const QVariantMap &v){return QJsonDocument(QJsonObject::fromVariantMap(v)).toJson();};
        QCOMPARE(parseOutgoingProbe(json(a),a["path"].toString(),a["name"].toString()),a);
        QVERIFY(parseOutgoingProbe(json(a),"/other/file.bin",a["name"].toString()).isEmpty());
        QVERIFY(parseOutgoingProbe(json(a),a["path"].toString(),"other.bin").isEmpty());
        for(const auto &field:QStringList{"mtimeNs","device","inode","sha256"}){
            auto bad=a;bad[field]="not-metadata";QVERIFY(parseOutgoingProbe(json(bad),a["path"].toString(),a["name"].toString()).isEmpty());
        }
        for(double size:{-1.0,1.5,double(OutgoingAttachmentLimit)+1}){
            auto bad=a;bad["size"]=size;QVERIFY(parseOutgoingProbe(json(bad),a["path"].toString(),a["name"].toString()).isEmpty());
        }
        QVERIFY(parseOutgoingProbe(QByteArray(20000,'x'),"/a","a").isEmpty());
    }
    void outgoingLimitsAndScriptInputs() {
        const QString path=QString::fromUtf8("/home/u/été ' ;$(touch injected).bin");
        const auto probe=buildOutgoingProbeScript(path,"safe.bin",OutgoingAttachmentLimit);
        QVERIFY(!probe.isEmpty());QVERIFY(!probe.contains(path.toUtf8()));
        QVERIFY(buildOutgoingProbeScript("relative","a",1).isEmpty());
        QVERIFY(buildOutgoingProbeScript("/a","bad\nname",1).isEmpty());
        QVERIFY(buildOutgoingProbeScript("/a","a",-1).isEmpty());
        QVariantMap a{{"path","/a"},{"name","a"},{"size",1},{"mtimeNs","1"},{"device","2"},{"inode","3"},{"sha256",QString(64,'b')}};
        QVERIFY(!buildOutgoingMimeScript({a}).isEmpty());
        QVERIFY(buildOutgoingMimeScript({a,a}).isEmpty());
        QVariantList many;for(int i=0;i<11;++i){auto n=a;n["path"]=QString("/file%1").arg(i);many<<n;}
        QVERIFY(buildOutgoingMimeScript(many).isEmpty());
        auto large=a;large["size"]=OutgoingAttachmentLimit;auto extra=a;extra["path"]="/b";
        QVERIFY(buildOutgoingMimeScript({large,extra}).isEmpty());
        auto bad=a;bad.remove("sha256");QVERIFY(buildOutgoingMimeScript({bad}).isEmpty());
    }
    void outgoingPreparationPrecedesDelivery() {
        MailDraft d;d.account="live";d.fromAddress="me@example.net";d.to="you@example.org";d.body="Bonjour";
        const auto msg=buildMessage(d,QDateTime::currentDateTime(),"outgoing-test");QVERIFY(msg.ok);
        QVariantMap a{{"path","/a"},{"name","a"},{"size",0},{"mtimeNs","1"},{"device","2"},{"inode","3"},{"sha256",QString(64,'c')}};
        const auto script=buildMailSendScript(d.account,msg.recipients,msg.data,{}, {},"FQMAILTEST",{a});
        QVERIFY(!script.isEmpty());
        QVERIFY(script.indexOf("python3 -")<script.indexOf("90 msmtp -a"));
        QVERIFY(script.indexOf("|| exit 15")<script.indexOf("90 msmtp -a"));
        bool delivered=true,unknown=true;
        QVERIFY(describeSendOutcome(15,{},&delivered,&unknown).contains("Rien n'a été envoyé"));QVERIFY(!delivered && !unknown);
        QVERIFY(describeSendOutcome(14,{},&delivered,&unknown).contains("Python 3"));
    }

    void accountsParsing() {
        const QByteArray out = "account perso\nfrom me@example.net\naccount work\nfrom me@corp.example\naccount bad name\nfrom x@y.z\n";
        const QList<MailSendAccount> a = parseMailAccounts(out);
        QCOMPARE(a.size(), 2);
        QCOMPARE(a[0].name, QString("perso")); QCOMPARE(a[0].from, QString("me@example.net"));
        QCOMPARE(a[1].from, QString("me@corp.example"));
        QVERIFY(parseMailAccounts("#none\n").isEmpty());
        const QByteArray script = buildMailAccountsScript();
        QVERIFY(script.contains("awk"));
        QVERIFY(!script.contains("password"));                        // the password line is never printed
    }

    void outcomes() {
        bool delivered = false, unknown = false;
        QVERIFY(describeSendOutcome(0, "", &delivered, &unknown).contains("envoyé")); QVERIFY(delivered && !unknown);
        QVERIFY(describeSendOutcome(11, "FQ-WARN copy-failed", &delivered, &unknown).contains("copie")); QVERIFY(delivered);
        QVERIFY(describeSendOutcome(12, "", &delivered, &unknown).contains("Délai")); QVERIFY(!delivered && unknown);
        QVERIFY(describeSendOutcome(4, "FQ-ERR missing-msmtp", &delivered, &unknown).contains("apt install msmtp")); QVERIFY(!delivered);
        QVERIFY(describeSendOutcome(10, "msmtp: authentication failed (method CRAM-MD5)\nmsmtp: server message: 535", &delivered, &unknown).contains("Authentification"));
        QVERIFY(describeSendOutcome(10, "msmtp: cannot connect to smtp.x: Connection refused\nFQ-SEND-FAILED 69", &delivered, &unknown).contains("Connexion"));
        QVERIFY(describeSendOutcome(10, "msmtp: the server sent an invalid certificate", &delivered, &unknown).contains("TLS"));
        QVERIFY(describeSendOutcome(10, "msmtp: recipient address <x@y> not accepted", &delivered, &unknown).contains("Destinataire"));
        QVERIFY(!describeSendOutcome(10, "FQ-SEND-FAILED 1", &delivered, &unknown).contains("FQ-"));
    }

    void replyHelpers() {
        QCOMPARE(replySubject("Hello"), QString("Re: Hello"));
        QCOMPARE(replySubject("Re: Hello"), QString("Re: Hello"));
        QCOMPARE(replySubject("RE : Hello"), QString("RE : Hello"));
        QCOMPARE(replySubject("(sans objet)"), QString("Re: "));
        QCOMPARE(replyBody("Bob <b@x.org>", "Wed, 21 Oct 2026", "line1\n\nline3\n"), QString("\n\nLe Wed, 21 Oct 2026, Bob <b@x.org> a écrit :\n> line1\n>\n> line3\n"));
    }

    // ---- account wizard ----
    // Audit R3: the wizard says the password is written in clear text; advice follows the provider, by host or by address domain.
    void passwordStorageIsSaidPlainly() {
        const QString warning = passwordStorageWarning();
        QVERIFY(warning.contains("EN CLAIR") && warning.contains("~/.mbsyncrc") && warning.contains("~/.msmtprc") && warning.contains("0600"));
        QVERIFY(warning.contains("mot de passe d'application"));
        QVERIFY(passwordAdvice("imap.gmail.com", "").contains("mots de passe d'application"));
        QVERIFY(passwordAdvice("", "me@googlemail.com").contains("Gmail"));
        QVERIFY(passwordAdvice("IMAP.MAIL.ME.COM", "").contains("iCloud"));
        QVERIFY(passwordAdvice("imap.mail.yahoo.com", "").contains("Yahoo"));
        QVERIFY(passwordAdvice("outlook.office365.com", "").contains("OAuth2"));
        QVERIFY(passwordAdvice("imap.fastmail.com", "").contains("Fastmail"));
        QVERIFY(passwordAdvice("127.0.0.1", "me@pm.me").contains("Bridge"));
        QVERIFY(passwordAdvice("imap.example.org", "me@example.org").isEmpty());
        QVERIFY(passwordAdvice("", "").isEmpty());
        QVERIFY(passwordAdvice("notgmail.com", "me@xgmail.com").isEmpty());          // a name that merely ends in the same letters is not Gmail
        QVERIFY(passwordAdvice("evil-gmail.com.example.org", "").isEmpty());
    }
    void passwordRules() {
        QVERIFY(validatePassword("s3cret!").isEmpty());
        QVERIFY(validatePassword(QString("p\"a b#c\\d'e$f;g")).isEmpty());
        QVERIFY(!validatePassword(QString()).isEmpty());
        QVERIFY(!validatePassword("line\nbreak").isEmpty());
        QVERIFY(!validatePassword("tab\there").isEmpty());
        QVERIFY(!validatePassword(" lead").isEmpty());
        QVERIFY(!validatePassword("trail ").isEmpty());
        QVERIFY(!validatePassword("\"quoted").isEmpty());
        QVERIFY(!validatePassword(QString(300, 'x')).isEmpty());
    }

    void specValidation() {
        QVERIFY(validateAccountSpec(goodSpec()).isEmpty());
        auto bad = [](auto edit) { MailAccountSpec s = goodSpec(); edit(s); return validateAccountSpec(s); };
        QVERIFY(!bad([](MailAccountSpec &s) { s.name = "a b"; }).isEmpty());
        QVERIFY(!bad([](MailAccountSpec &s) { s.name = "x\nIMAPAccount evil"; }).isEmpty());
        QVERIFY(!bad([](MailAccountSpec &s) { s.email = "zoë@example.net"; }).isEmpty());
        QVERIFY(!bad([](MailAccountSpec &s) { s.imapHost = "imap.example.net\nHost evil"; }).isEmpty());
        QVERIFY(!bad([](MailAccountSpec &s) { s.smtpPort = 0; }).isEmpty());
        QVERIFY(!bad([](MailAccountSpec &s) { s.imapUser = "me\"; Pass \"x"; }).isEmpty());
        QVERIFY(!bad([](MailAccountSpec &s) { s.imapSecurity = "ssl3"; }).isEmpty());
        QVERIFY(!bad([](MailAccountSpec &s) { s.imapSecurity = "none"; }).isEmpty());             // clear text only for localhost
        QVERIFY(bad([](MailAccountSpec &s) { s.imapSecurity = "none"; s.imapHost = "127.0.0.1"; }).isEmpty());
        QVERIFY(!bad([](MailAccountSpec &s) { s.smtpSecurity = "none"; }).isEmpty());
        QVERIFY(!bad([](MailAccountSpec &s) { s.password.clear(); }).isEmpty());
        QVERIFY(!bad([](MailAccountSpec &s) { s.smtpPassword = "a\nb"; }).isEmpty());
        QVERIFY(!bad([](MailAccountSpec &s) { s.displayName = "x\ny"; }).isEmpty());
        const MailAccountSpec fromVariant = accountSpecFromVariant({ { "name", " perso " }, { "email", "me@example.net" }, { "imapUser", "u" }, { "password", "p" } });
        QCOMPARE(fromVariant.name, QString("perso")); QCOMPARE(fromVariant.smtpUser, QString("u"));
    }

    void setupFileContents() {
        const MailAccountSpec s = goodSpec();
        const QList<MailSetupFile> files = buildSetupFiles(s, "/home/u", "/home/u/Mail", false);
        QCOMPARE(files.size(), 3);
        const MailSetupFile &mb = files[0], &ms = files[1], &nm = files[2];
        QCOMPARE(mb.path, QString("/home/u/.mbsyncrc"));
        QVERIFY(mb.content.startsWith("# >>> FORTIQ account perso\n") && mb.content.endsWith("# <<< FORTIQ account perso\n"));
        QVERIFY(mb.content.contains("Pass \"p\\\"a b#c\\\\d'e$f;g\"\n"));             // " and \ escaped for mbsync
        QVERIFY(mb.content.contains("TLSType IMAPS\n") && mb.content.contains("Port 993\n") && mb.content.contains("User \"me@example.net\"\n"));
        QVERIFY(mb.content.contains("Path /home/u/Mail/perso/\nInbox /home/u/Mail/perso/INBOX\n"));
        QVERIFY(mb.content.contains("Sync Pull\n") && mb.content.contains("Expunge None\n") && mb.content.contains("Create Near\n"));
        QVERIFY(!mb.content.contains("AuthMechs"));
        QVERIFY(ms.content.contains("password \"p\"a b#c\\d'e$f;g\"\n"));              // quoted, unescaped for msmtp: a " #" is then not a comment
        QVERIFY(ms.content.contains("tls on\ntls_starttls on\ntls_trust_file system\n") && ms.content.contains("from me@example.net\n") && ms.content.contains("auth on\n"));
        QVERIFY(nm.kind == "new" && nm.content.contains("path=/home/u/Mail\n") && nm.content.contains("primary_email=me@example.net\n"));
        // The text shown to the user is the same file with the password masked.
        for (const MailSetupFile &f : files) {
            QVERIFY(!f.shown.contains("a b#c"));
            QCOMPARE(f.shown.size() == f.content.size(), f.key == "notmuch");
        }
        QVERIFY(mb.shown.contains(QString::fromUtf8(kPasswordMask)) && ms.shown.contains(QString::fromUtf8(kPasswordMask)));
        // Variants: implicit TLS for SMTP, separate SMTP password, local clear text, existing notmuch configuration.
        MailAccountSpec t = s;
        t.smtpSecurity = "tls"; t.smtpPort = 465; t.smtpPassword = "other";
        const QList<MailSetupFile> v = buildSetupFiles(t, "/home/u", "/srv/mail", true);
        QCOMPARE(v.size(), 2);
        QVERIFY(v[1].content.contains("tls on\ntls_starttls off\n") && v[1].content.contains("password \"other\"\n"));
        QVERIFY(v[0].content.contains("Path /srv/mail/perso/\n"));
        MailAccountSpec l = s;
        l.imapSecurity = "none"; l.imapHost = "127.0.0.1"; l.smtpSecurity = "none"; l.smtpHost = "localhost";
        const QList<MailSetupFile> local = buildSetupFiles(l, "/home/u", "/home/u/Mail", true);
        QVERIFY(local[0].content.contains("TLSType None\nAuthMechs LOGIN\n"));
        QVERIFY(local[1].content.contains("tls off\n") && !local[1].content.contains("tls_starttls") && local[1].content.contains("auth cram-md5\n"));
    }

    void writeScript() {
        const MailAccountSpec s = goodSpec();
        const QList<MailSetupFile> files = buildSetupFiles(s, "/home/u", "/home/u/Mail", false);
        const QByteArray script = buildMailWriteForTest(files);
        QVERIFY(script.startsWith("umask 077\n"));
        QVERIFY(script.contains("cp -p \"$f\" \"$f.fortiq-bak-20261023120000\""));
        QVERIFY(script.contains("chmod 600 \"$f.fortiq-bak-20261023120000\""));       // the backup may hold the old passwords: never group/world readable
        QVERIFY(script.contains("mktemp \"$f.fortiq-new.XXXXXX\""));
        QVERIFY(script.contains("awk -v b='# >>> FORTIQ account perso' -v e='# <<< FORTIQ account perso'"));
        QVERIFY(script.contains("chmod 600 \"$f\""));
        QVERIFY(script.contains("mkdir -p '/home/u/Mail'"));
        QVERIFY(script.contains("mkdir -p '/home/u/Mail/perso'"));       // Maildir root of the account
        QVERIFY(script.contains("if [ -e \"$f\" ]; then rm -f \"$new\"; echo 'FQ-ERR exists' >&2; exit 7; fi"));    // the notmuch configuration is never replaced
        QVERIFY(script.contains("echo '#written mbsyncrc'") && script.contains("echo '#written msmtprc'") && script.contains("echo '#written notmuch'"));
        // The password occurs only inside the heredoc bodies, never on a command line.
        QSet<QByteArray> contentLines;
        for (const MailSetupFile &f : files) for (const QByteArray &l : f.content.toUtf8().split('\n')) contentLines.insert(l);
        for (const QByteArray &line : script.split('\n'))
            if (line.contains("a b#c") || line.contains("p\\\"a")) QVERIFY2(contentLines.contains(line), line.constData());
        QVERIFY(!script.contains("sudo")); QVERIFY(!script.contains("rm -rf"));
        QVERIFY(buildSetupWriteScript("perso", files, "bad", "D").isEmpty());
        QVERIFY(buildSetupWriteScript("bad name", files, "20261023120000", "D").isEmpty());
        QVERIFY(buildSetupWriteScript("perso", {}, "20261023120000", "D").isEmpty());
        QCOMPARE(parseSetupWritten("x\n#written mbsyncrc\n#written msmtprc\n"), QStringList({ "mbsyncrc", "msmtprc" }));
    }

    void probe() {
        QVERIFY(buildSetupProbeScript("bad name", QString()).isEmpty());
        const QByteArray script = buildSetupProbeScript("perso", "/home/u/nm.conf");
        QVERIFY(script.contains("grep -qxF '# >>> FORTIQ account perso'"));
        QVERIFY(!script.contains("password"));
        const QByteArray out = "#section home\n/home/u\n#section files\n.mbsyncrc 1\n.msmtprc 0\n.notmuch-config 1\n#section blocks\nmbsync 1\nmsmtp 0\n"
                               "#section tools\nmbsync mbsync 1.5.1\nmsmtp -\nnotmuch notmuch 0.39\n#section dbpath\n/home/u/Maildir\n#section end\n";
        const MailSetupState st = parseSetupProbe(out);
        QVERIFY(st.ok);
        QCOMPARE(st.home, QString("/home/u"));
        QVERIFY(st.hasMbsyncrc && !st.hasMsmtprc && st.hasNotmuchConfig && st.blockMbsync && !st.blockMsmtp);
        QCOMPARE(st.mbsyncVersion, QString("mbsync 1.5.1")); QVERIFY(st.msmtpVersion.isEmpty());
        QCOMPARE(st.mailRoot(), QString("/home/u/Maildir"));
        MailSetupState none = parseSetupProbe("#section home\n/home/u\n#section dbpath\n\n#section end\n");
        QCOMPARE(none.mailRoot(), QString("/home/u/Mail"));
        QVERIFY(!parseSetupProbe("garbage").ok);
        QVERIFY(!parseSetupProbe("#section home\nrelative\n#section end\n").ok);
    }

    void attachmentNames() {
        QCOMPARE(sanitizeAttachmentName("report.pdf"), QString("report.pdf"));
        QCOMPARE(sanitizeAttachmentName(QString::fromUtf8("Rapport \xC3\xA9t\xC3\xA9.pdf")), QString::fromUtf8("Rapport \xC3\xA9t\xC3\xA9.pdf"));
        QCOMPARE(sanitizeAttachmentName("../../etc/passwd"), QString("_.._etc_passwd"));
        QCOMPARE(sanitizeAttachmentName("..\\..\\win.ini"), QString("_.._win.ini"));
        QCOMPARE(sanitizeAttachmentName(".bashrc"), QString("bashrc"));
        QCOMPARE(sanitizeAttachmentName("a\nb\tc.txt"), QString("a_b_c.txt"));
        QCOMPARE(sanitizeAttachmentName("   "), QString("piece-jointe"));
        QCOMPARE(sanitizeAttachmentName("..."), QString("piece-jointe"));
        QCOMPARE(sanitizeAttachmentName("name: x*?.txt"), QString("name_ x__.txt"));
        const QString cut = sanitizeAttachmentName(QString(300, 'a') + ".tar.gz");
        QVERIFY(cut.endsWith(".gz"));
        QVERIFY(cut.size() <= 120);
    }

    void attachmentScripts() {
        const QByteArray s = buildAttachmentSaveScript("m3@example.org", 3, "Rapport.pdf", "server", QString());
        QVERIFY(s.contains("notmuch show --format=raw --part=3 -- 'id:m3@example.org' > \"$tmp\""));
        QVERIFY(s.contains("dir=\"$HOME/Downloads\"") && s.contains("name='Rapport.pdf'"));
        QVERIFY(s.contains("final=\"$dir/$stem ($i)$ext\""));                 // never overwrites
        QVERIFY(s.contains("umask 077") && s.contains("chmod 600") && !s.contains("chmod 644") && !s.contains("rm -rf"));   // private mail stays private on the server
        const QByteArray t = buildAttachmentSaveScript("m3@example.org", 3, "x'y.pdf", "temp", "/home/u/nm.conf");
        QVERIFY(t.contains("name='x'\\''y.pdf'") && t.contains("NOTMUCH_CONFIG='/home/u/nm.conf'"));
        QVERIFY(t.contains("mktemp -d \"$base/att.XXXXXX\"") && t.contains("chmod 600") && t.contains("-mmin +1440"));
        QVERIFY(buildAttachmentSaveScript("bad id", 3, "a", "server", QString()).isEmpty());
        QVERIFY(buildAttachmentSaveScript("m@x.org", 0, "a", "server", QString()).isEmpty());
        QVERIFY(buildAttachmentSaveScript("m@x.org", 3, "a", "elsewhere", QString()).isEmpty());
        const AttachmentSaved ok = parseAttachmentSaved("#path /home/u/Downloads/Rapport.pdf\n#size 16\n");
        QVERIFY(ok.ok && ok.size == 16 && ok.path == "/home/u/Downloads/Rapport.pdf");
        QVERIFY(!parseAttachmentSaved("#path relative\n#size 5\n").ok);
        QVERIFY(!parseAttachmentSaved("").ok);
        QVERIFY(describeAttachmentFailure(7, "FQ-ERR empty").contains("introuvable ou vide"));
        QVERIFY(describeAttachmentFailure(6, "FQ-ERR notmuch").contains("notmuch"));
        QVERIFY(describeAttachmentFailure(5, "FQ-ERR mkdir").contains(QString::fromUtf8("\xC3\x89""criture impossible")));
        // Only a temporary directory made by FORTIQ can be removed.
        QVERIFY(isAttachmentTempDirectory("/home/u/.cache/fortiq-mail/att.Ab12Cd"));
        for (const char *bad : { "/home/u", "/home/u/.cache/fortiq-mail/att.Ab12Cd/../..", "/home/u/.cache/fortiq-mail/", "/home/u/.cache/fortiq-mail/att.x", "/etc/att.Ab12Cd", "/home/u/.cache/fortiq-mail/att.Ab12Cd/sub" })
            QVERIFY2(!isAttachmentTempDirectory(bad), bad);
        QVERIFY(buildAttachmentCleanupScript("/home/u").isEmpty());
        const QByteArray c = buildAttachmentCleanupScript("/home/u/.cache/fortiq-mail/att.Ab12Cd");
        QVERIFY(c.contains("case \"$d\" in \"$HOME\"/.cache/fortiq-mail/att.*) rm -rf -- \"$d\" ;; esac"));
    }

    void actionScripts() {
        QVERIFY(buildSmtpTestScript("perso").contains("msmtp -a 'perso' --serverinfo"));
        QVERIFY(buildImapTestScript("perso").contains("mbsync -l 'perso'"));
        const QByteArray sync = buildSyncScript("perso", QString());
        QVERIFY(sync.contains("mbsync 'perso'") && sync.contains("notmuch new"));
        QVERIFY(!sync.contains("-a ") && !sync.contains("rm "));
        QVERIFY(buildSmtpTestScript("a b").isEmpty() && buildImapTestScript("a;b").isEmpty() && buildSyncScript("", QString()).isEmpty());
        QVERIFY(describeActionFailure("Test", 4, "FQ-ERR missing-mbsync").contains("isync"));
        QVERIFY(describeActionFailure("Test", 124, "").contains("délai"));
        QVERIFY(describeActionFailure("Test", 7, "FQ-ERR exists").contains("rien n'a été remplacé"));
    }

};

QTEST_APPLESS_MAIN(MailSendTest)
#include "MailSendTest.moc"
