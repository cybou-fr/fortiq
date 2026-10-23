#include "MailSend.h"

#include <QLocale>
#include <QRegularExpression>

namespace fortiq {

namespace {
QByteArray shQuote(const QString &value) {
    QByteArray v = value.toUtf8();
    v.replace("'", "'\\''");
    return "'" + v + "'";
}

bool isPlainAscii(const QString &s) {
    for (const QChar c : s) if (c.unicode() < 0x20 || c.unicode() > 0x7e) return false;
    return true;
}

bool hasControl(const QString &s) {
    for (const QChar c : s) if (c.unicode() < 0x20 || c.unicode() == 0x7f) return true;
    return false;
}

bool validAddrSpec(const QString &addr) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]{1,64}@[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?(\\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)+$"));
    // A leading "-" could be taken for an option by a mail tool (the lesson in DEVELOPMENT_MATRIX): refused although `--` is also used.
    return addr.size() <= 254 && re.match(addr).hasMatch() && !addr.contains(QLatin1String("..")) && !addr.startsWith(QLatin1Char('-'));
}

QString stripQuotes(QString name) {
    name = name.trimmed();
    if (name.size() >= 2 && name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"'))) {
        name = name.mid(1, name.size() - 2);
        QString out;
        for (qsizetype i = 0; i < name.size(); ++i) {
            if (name.at(i) == QLatin1Char('\\') && i + 1 < name.size()) ++i;
            out += name.at(i);
        }
        return out;
    }
    return name;
}
}  // namespace

bool validMailAccountName(const QString &name) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,30}$"));
    return re.match(name).hasMatch();
}

bool validMessageId(const QString &id) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~@-]{3,200}$"));
    return re.match(id).hasMatch();
}

bool parseMailAddresses(const QString &text, QList<MailAddress> *out, QString *error) {
    out->clear();
    if (hasControl(text)) { *error = QStringLiteral("Adresse invalide : caractère de contrôle."); return false; }
    QStringList items;
    QString current;
    bool quoted = false, angle = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            current += c;
            if (c == QLatin1Char('\\') && i + 1 < text.size()) current += text.at(++i);
            else if (c == QLatin1Char('"')) quoted = false;
            continue;
        }
        if (c == QLatin1Char('"')) { quoted = true; current += c; }
        else if (c == QLatin1Char('<')) { angle = true; current += c; }
        else if (c == QLatin1Char('>')) { angle = false; current += c; }
        else if ((c == QLatin1Char(',') || c == QLatin1Char(';')) && !angle) { items << current; current.clear(); }
        else current += c;
    }
    if (quoted || angle) { *error = QStringLiteral("Adresse invalide : guillemet ou chevron non fermé."); return false; }
    items << current;
    for (const QString &raw : items) {
        const QString item = raw.trimmed();
        if (item.isEmpty()) continue;
        MailAddress a;
        const qsizetype lt = item.lastIndexOf(QLatin1Char('<'));
        if (lt >= 0) {
            if (!item.endsWith(QLatin1Char('>'))) { *error = QStringLiteral("Adresse invalide : « %1 ».").arg(item); return false; }
            a.addr = item.mid(lt + 1, item.size() - lt - 2).trimmed();
            a.name = stripQuotes(item.left(lt));
        } else {
            a.addr = item;
        }
        if (!validAddrSpec(a.addr)) { *error = QStringLiteral("Adresse invalide : « %1 » (adresses ASCII de la forme nom@domaine).").arg(a.addr); return false; }
        if (a.name.size() > 100) { *error = QStringLiteral("Nom trop long dans une adresse."); return false; }
        out->push_back(a);
    }
    return true;
}

QString encodeHeaderText(const QString &text) {
    if (isPlainAscii(text)) return text;
    const QByteArray bytes = text.toUtf8();
    QStringList words;
    qsizetype start = 0;
    while (start < bytes.size()) {
        qsizetype end = qMin<qsizetype>(start + 45, bytes.size());
        while (end < bytes.size() && (static_cast<unsigned char>(bytes[end]) & 0xC0) == 0x80) --end;   // never split a UTF-8 sequence
        words << QStringLiteral("=?UTF-8?B?") + QString::fromLatin1(bytes.mid(start, end - start).toBase64()) + QStringLiteral("?=");
        start = end;
    }
    return words.join(QStringLiteral("\n "));
}

QString formatMailAddress(const MailAddress &a) {
    if (a.name.isEmpty()) return a.addr;
    if (!isPlainAscii(a.name)) return encodeHeaderText(a.name) + QStringLiteral(" <") + a.addr + QLatin1Char('>');
    static const QRegularExpression atoms(QStringLiteral("^[A-Za-z0-9!#$%&'*+/=?^_`{|}~ -]+$"));
    if (atoms.match(a.name).hasMatch()) return a.name + QStringLiteral(" <") + a.addr + QLatin1Char('>');
    QString q = a.name;
    q.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + q + QStringLiteral("\" <") + a.addr + QLatin1Char('>');
}

BuiltMessage buildMessage(const MailDraft &d, const QDateTime &when, const QString &token) {
    BuiltMessage r;
    auto fail = [&](const QString &e) { r.error = e; return r; };
    if (!validMailAccountName(d.account)) return fail(QStringLiteral("Choisissez un compte d'envoi."));
    QList<MailAddress> from, to, cc, bcc;
    QString err;
    if (!parseMailAddresses(d.fromAddress, &from, &err) || from.size() != 1) return fail(QStringLiteral("Adresse d'expédition du compte invalide."));
    from[0].name = d.fromName.trimmed();
    if (hasControl(from[0].name)) return fail(QStringLiteral("Nom d'expéditeur invalide."));
    if (!parseMailAddresses(d.to, &to, &err)) return fail(err);
    if (!parseMailAddresses(d.cc, &cc, &err)) return fail(err);
    if (!parseMailAddresses(d.bcc, &bcc, &err)) return fail(err);
    QStringList recipients;
    for (const QList<MailAddress> *list : { &to, &cc, &bcc })
        for (const MailAddress &a : *list) if (!recipients.contains(a.addr, Qt::CaseInsensitive)) recipients << a.addr;
    if (recipients.isEmpty()) return fail(QStringLiteral("Ajoutez au moins un destinataire."));
    if (recipients.size() > 50) return fail(QStringLiteral("Trop de destinataires (50 au plus)."));
    if (d.subject.size() > 300 || hasControl(d.subject)) return fail(QStringLiteral("Objet invalide (300 caractères au plus, une seule ligne)."));
    if (d.body.size() > 1024 * 1024) return fail(QStringLiteral("Message trop long (1 Mio au plus)."));
    if (!d.inReplyTo.isEmpty() && !validMessageId(d.inReplyTo)) return fail(QStringLiteral("Identifiant du message d'origine invalide."));
    if (token.isEmpty()) return fail(QStringLiteral("Identifiant de message manquant."));

    const QString domain = from[0].addr.mid(from[0].addr.indexOf(QLatin1Char('@')) + 1);
    r.messageId = token + QLatin1Char('@') + domain;
    auto join = [](const QList<MailAddress> &list) { QStringList parts; for (const MailAddress &a : list) parts << formatMailAddress(a); return parts.join(QStringLiteral(",\n ")); };
    const int offset = when.offsetFromUtc();
    const QString zone = QStringLiteral("%1%2%3").arg(offset < 0 ? QLatin1Char('-') : QLatin1Char('+')).arg(qAbs(offset) / 3600, 2, 10, QLatin1Char('0')).arg(qAbs(offset) % 3600 / 60, 2, 10, QLatin1Char('0'));

    QString h;
    h += QStringLiteral("From: ") + formatMailAddress(from[0]) + QLatin1Char('\n');
    if (!to.isEmpty()) h += QStringLiteral("To: ") + join(to) + QLatin1Char('\n');
    if (!cc.isEmpty()) h += QStringLiteral("Cc: ") + join(cc) + QLatin1Char('\n');
    h += QStringLiteral("Subject: ") + encodeHeaderText(d.subject) + QLatin1Char('\n');
    h += QStringLiteral("Date: ") + QLocale::c().toString(when, QStringLiteral("ddd, dd MMM yyyy HH:mm:ss")) + QLatin1Char(' ') + zone + QLatin1Char('\n');
    h += QStringLiteral("Message-ID: <") + r.messageId + QStringLiteral(">\n");
    if (!d.inReplyTo.isEmpty()) h += QStringLiteral("In-Reply-To: <") + d.inReplyTo + QStringLiteral(">\nReferences: <") + d.inReplyTo + QStringLiteral(">\n");
    h += QStringLiteral("MIME-Version: 1.0\nContent-Type: text/plain; charset=utf-8\nContent-Transfer-Encoding: base64\n\n");

    QString body = d.body;
    body.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (!body.endsWith(QLatin1Char('\n'))) body += QLatin1Char('\n');
    const QByteArray b64 = body.toUtf8().toBase64();
    QByteArray wrapped;
    for (qsizetype i = 0; i < b64.size(); i += 76) wrapped += b64.mid(i, 76) + '\n';

    r.data = h.toUtf8() + wrapped;
    r.recipients = recipients;
    r.ok = true;
    return r;
}

QByteArray buildMailSendScript(const QString &account, const QStringList &recipients, const QByteArray &message, const QString &inReplyTo,
                               const QString &configPath, const QString &delimiter) {
    if (!validMailAccountName(account) || recipients.isEmpty() || delimiter.isEmpty() || message.contains(delimiter.toUtf8())) return {};
    QByteArray rcpt;
    for (const QString &r : recipients) rcpt += " " + shQuote(r);
    QByteArray s = "export LC_ALL=C.UTF-8\n";
    if (!configPath.isEmpty()) s += "export NOTMUCH_CONFIG=" + shQuote(configPath) + "\n";
    s += "command -v msmtp >/dev/null 2>&1 || { echo 'FQ-ERR missing-msmtp' >&2; exit 4; }\n"
         "t=$(umask 077; mktemp) || { echo 'FQ-ERR mktemp' >&2; exit 5; }\n"
         "trap 'rm -f \"$t\"' EXIT\n"
         "cat > \"$t\" <<'" + delimiter.toUtf8() + "'\n" + message + delimiter.toUtf8() + "\n";
    s += "timeout -k 5 90 msmtp -a " + shQuote(account) + " --" + rcpt + " < \"$t\"\n"
         "rc=$?\n"
         "if [ $rc -ne 0 ]; then echo \"FQ-SEND-FAILED $rc\" >&2; if [ $rc -eq 124 ] || [ $rc -eq 137 ]; then exit 12; fi; exit 10; fi\n"
         "command -v notmuch >/dev/null 2>&1 || { echo 'FQ-WARN no-notmuch' >&2; exit 11; }\n"
         "notmuch insert --create-folder --folder=Sent +sent -unread < \"$t\" >/dev/null 2>&1 || { echo 'FQ-WARN copy-failed' >&2; exit 11; }\n";
    if (!inReplyTo.isEmpty() && validMessageId(inReplyTo))
        s += "notmuch tag +replied -- " + shQuote(QStringLiteral("id:") + inReplyTo) + " >/dev/null 2>&1\n";
    s += "exit 0\n";
    return s;
}

QByteArray buildMailAccountsScript() {
    return "f=\"$HOME/.msmtprc\"\n"
           "[ -r \"$f\" ] || f=\"${XDG_CONFIG_HOME:-$HOME/.config}/msmtp/config\"\n"
           "[ -r \"$f\" ] || { echo '#none'; exit 0; }\n"
           "awk '/^[[:space:]]*account[[:space:]]/ { if ($2 != \"default\") print \"account \" $2 } /^[[:space:]]*from[[:space:]]/ { print \"from \" $2 }' \"$f\"\n";
}

QList<MailSendAccount> parseMailAccounts(const QByteArray &output) {
    QList<MailSendAccount> out;
    for (const QByteArray &raw : output.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1String("account "))) {
            const QString name = line.mid(8).trimmed();
            if (validMailAccountName(name)) out.push_back({ name, QString() });
        } else if (line.startsWith(QLatin1String("from ")) && !out.isEmpty() && out.last().from.isEmpty()) {
            out.last().from = line.mid(5).trimmed();
        }
    }
    return out;
}

QVariantList sendAccountsToVariant(const QList<MailSendAccount> &accounts) {
    QVariantList out;
    for (const MailSendAccount &a : accounts) out.push_back(QVariantMap { { "name", a.name }, { "from", a.from } });
    return out;
}

QString describeSendOutcome(int exitCode, const QString &stderrText, bool *delivered, bool *unknown) {
    if (delivered) *delivered = false;
    if (unknown) *unknown = false;
    QString err = stderrText;
    err.remove(QRegularExpression(QStringLiteral("FQ-(SEND-FAILED|WARN|ERR)[^\\n]*\\n?")));
    err = err.trimmed();
    if (exitCode == 0) { if (delivered) *delivered = true; return QStringLiteral("Message envoyé et copié dans le dossier Sent."); }
    if (exitCode == 11) { if (delivered) *delivered = true; return QStringLiteral("Message envoyé, mais la copie n'a pas pu être enregistrée dans Sent (notmuch)."); }
    if (exitCode == 12) { if (unknown) *unknown = true; return QStringLiteral("Délai dépassé : on ne sait pas si le message est parti. Vérifiez avant de le renvoyer."); }
    if (exitCode == 4) return QStringLiteral("msmtp n'est pas installé sur ce serveur (sudo apt install msmtp).");
    const QString low = err.toLower();
    if (low.contains(QLatin1String("authentication failed")) || low.contains(QLatin1String("auth")) && low.contains(QLatin1String("535")))
        return QStringLiteral("Authentification SMTP refusée : vérifiez l'utilisateur et le mot de passe du compte.");
    if (low.contains(QLatin1String("cannot connect")) || low.contains(QLatin1String("could not connect")) || low.contains(QLatin1String("connection refused")) || low.contains(QLatin1String("network is unreachable")))
        return QStringLiteral("Connexion au serveur SMTP impossible depuis le serveur Linux : ") + err.section(QLatin1Char('\n'), 0, 1).left(300);
    if (low.contains(QLatin1String("certificate")) || low.contains(QLatin1String("tls")))
        return QStringLiteral("Échec TLS : ") + err.section(QLatin1Char('\n'), 0, 1).left(300);
    if (low.contains(QLatin1String("recipient")) || low.contains(QLatin1String("rejected")))
        return QStringLiteral("Destinataire refusé par le serveur : ") + err.section(QLatin1Char('\n'), 0, 1).left(300);
    if (low.contains(QLatin1String("account")) && (low.contains(QLatin1String("not found")) || low.contains(QLatin1String("no account"))))
        return QStringLiteral("Compte msmtp introuvable : ouvrez « Comptes » pour le créer.");
    if (err.isEmpty()) return QStringLiteral("L'envoi a échoué (code %1) sans message.").arg(exitCode);
    return QStringLiteral("L'envoi a échoué : ") + err.section(QLatin1Char('\n'), 0, 3).left(600);
}

QString replySubject(const QString &subject) {
    const QString s = subject.trimmed();
    if (s.isEmpty() || s == QLatin1String("(sans objet)")) return QStringLiteral("Re: ");
    static const QRegularExpression re(QStringLiteral("^re\\s*:"), QRegularExpression::CaseInsensitiveOption);
    return re.match(s).hasMatch() ? s : QStringLiteral("Re: ") + s;
}

QString replyBody(const QString &from, const QString &date, const QString &text) {
    QString out = QStringLiteral("\n\nLe %1, %2 a écrit :\n").arg(date, from);
    QString t = text;
    t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    if (t.endsWith(QLatin1Char('\n'))) t.chop(1);
    for (const QString &line : t.split(QLatin1Char('\n'))) out += line.isEmpty() ? QStringLiteral(">\n") : QStringLiteral("> ") + line + QLatin1Char('\n');
    return out;
}

}  // namespace fortiq
