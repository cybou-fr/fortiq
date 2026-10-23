#include "MailAccount.h"

#include <QHash>
#include <QRegularExpression>

namespace fortiq {

namespace {
QByteArray shQuote(const QString &value) {
    QByteArray v = value.toUtf8();
    v.replace("'", "'\\''");
    return "'" + v + "'";
}

bool hasControl(const QString &s) {
    for (const QChar c : s) if (c.unicode() < 0x20 || c.unicode() == 0x7f) return true;
    return false;
}

bool validHost(const QString &host) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9.-]{0,252}$"));
    return re.match(host).hasMatch();
}

bool validUser(const QString &user) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9@._+%=~-]{1,128}$"));
    return re.match(user).hasMatch();
}

bool localHost(const QString &host) {
    const QString h = host.toLower();
    return h == QLatin1String("localhost") || h == QLatin1String("127.0.0.1");
}

QString mbsyncEscape(const QString &value) {
    QString out = value;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
    return out;
}

QString beginMarker(const QString &name) { return QStringLiteral("# >>> FORTIQ account ") + name; }
QString endMarker(const QString &name) { return QStringLiteral("# <<< FORTIQ account ") + name; }
}  // namespace

MailAccountSpec accountSpecFromVariant(const QVariantMap &m) {
    MailAccountSpec s;
    s.name = m.value("name").toString().trimmed();
    s.displayName = m.value("displayName").toString().trimmed();
    s.email = m.value("email").toString().trimmed();
    s.imapHost = m.value("imapHost").toString().trimmed();
    s.imapUser = m.value("imapUser").toString().trimmed();
    s.imapSecurity = m.value("imapSecurity", "tls").toString();
    s.imapPort = m.value("imapPort", 993).toInt();
    s.smtpHost = m.value("smtpHost").toString().trimmed();
    s.smtpUser = m.value("smtpUser").toString().trimmed();
    s.smtpSecurity = m.value("smtpSecurity", "starttls").toString();
    s.smtpPort = m.value("smtpPort", 587).toInt();
    s.password = m.value("password").toString();
    s.smtpPassword = m.value("smtpPassword").toString();
    if (s.smtpUser.isEmpty()) s.smtpUser = s.imapUser;
    return s;
}

QString validatePassword(const QString &p) {
    if (p.isEmpty()) return QStringLiteral("Saisissez le mot de passe.");
    if (p.size() > 256) return QStringLiteral("Mot de passe trop long (256 caractères au plus).");
    if (hasControl(p)) return QStringLiteral("Le mot de passe contient un caractère de contrôle (saut de ligne, tabulation…) : non pris en charge.");
    if (p != p.trimmed()) return QStringLiteral("Le mot de passe commence ou finit par une espace : non pris en charge par msmtp.");
    if (p.startsWith(QLatin1Char('"'))) return QStringLiteral("Un mot de passe commençant par un guillemet n'est pas pris en charge par msmtp.");
    return {};
}

QString validateAccountSpec(const MailAccountSpec &s) {
    static const QRegularExpression nameRe(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,30}$"));
    if (!nameRe.match(s.name).hasMatch()) return QStringLiteral("Nom de compte invalide (lettres, chiffres, . _ - ; 31 caractères au plus).");
    if (s.displayName.size() > 100 || hasControl(s.displayName)) return QStringLiteral("Nom d'affichage invalide.");
    static const QRegularExpression emailRe(QStringLiteral("^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]{1,64}@[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?(\\.[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?)+$"));
    if (!emailRe.match(s.email).hasMatch()) return QStringLiteral("Adresse e-mail invalide (ASCII, de la forme nom@domaine).");
    const QStringList securities { QStringLiteral("tls"), QStringLiteral("starttls"), QStringLiteral("none") };
    if (!validHost(s.imapHost)) return QStringLiteral("Serveur IMAP invalide.");
    if (!validHost(s.smtpHost)) return QStringLiteral("Serveur SMTP invalide.");
    if (s.imapPort < 1 || s.imapPort > 65535) return QStringLiteral("Port IMAP invalide (1–65535).");
    if (s.smtpPort < 1 || s.smtpPort > 65535) return QStringLiteral("Port SMTP invalide (1–65535).");
    if (!securities.contains(s.imapSecurity)) return QStringLiteral("Sécurité IMAP invalide.");
    if (!securities.contains(s.smtpSecurity)) return QStringLiteral("Sécurité SMTP invalide.");
    if (s.imapSecurity == QLatin1String("none") && !localHost(s.imapHost)) return QStringLiteral("Sans chiffrement, le serveur IMAP doit être localhost : le mot de passe circulerait en clair.");
    if (s.smtpSecurity == QLatin1String("none") && !localHost(s.smtpHost)) return QStringLiteral("Sans chiffrement, le serveur SMTP doit être localhost : le mot de passe circulerait en clair.");
    if (!validUser(s.imapUser)) return QStringLiteral("Utilisateur IMAP invalide (lettres, chiffres et @ . _ + % = ~ - uniquement).");
    if (!validUser(s.smtpUser)) return QStringLiteral("Utilisateur SMTP invalide.");
    QString err = validatePassword(s.password);
    if (!err.isEmpty()) return err;
    if (!s.smtpPassword.isEmpty()) { err = validatePassword(s.smtpPassword); if (!err.isEmpty()) return err; }
    return {};
}

QList<MailSetupFile> buildSetupFiles(const MailAccountSpec &s, const QString &home, const QString &mailRoot, bool notmuchConfigExists) {
    QList<MailSetupFile> files;
    const QString smtpPassword = s.smtpPassword.isEmpty() ? s.password : s.smtpPassword;
    const QString mask = QString::fromUtf8(kPasswordMask);
    auto makeBlock = [&](bool masked) {
        QString imapTls = s.imapSecurity == QLatin1String("tls") ? QStringLiteral("IMAPS") : s.imapSecurity == QLatin1String("starttls") ? QStringLiteral("STARTTLS") : QStringLiteral("None");
        QString t;
        t += beginMarker(s.name) + QLatin1Char('\n');
        t += QStringLiteral("# Écrit par FORTIQ (Courrier). Ce fichier contient un mot de passe : il doit rester en mode 0600.\n");
        t += QStringLiteral("IMAPAccount %1\nHost %2\nPort %3\nUser \"%4\"\nPass \"%5\"\nTLSType %6\n").arg(s.name, s.imapHost).arg(s.imapPort).arg(s.imapUser, masked ? mask : mbsyncEscape(s.password), imapTls);
        if (s.imapSecurity == QLatin1String("none")) t += QStringLiteral("AuthMechs LOGIN\n");
        t += QStringLiteral("\nIMAPStore %1-remote\nAccount %1\n\n").arg(s.name);
        t += QStringLiteral("MaildirStore %1-local\nPath %2/%1/\nInbox %2/%1/INBOX\nSubFolders Verbatim\n\n").arg(s.name, mailRoot);
        t += QStringLiteral("Channel %1\nFar :%1-remote:\nNear :%1-local:\nPatterns *\nCreate Near\nExpunge None\nSync Pull\nSyncState *\n").arg(s.name);
        t += endMarker(s.name) + QLatin1Char('\n');
        return t;
    };
    auto makeMsmtp = [&](bool masked) {
        QString t;
        t += beginMarker(s.name) + QLatin1Char('\n');
        t += QStringLiteral("# Écrit par FORTIQ (Courrier). Ce fichier contient un mot de passe : il doit rester en mode 0600.\n");
        // The password is quoted: msmtp then keeps everything up to the last quote of the line (a bare " #" would start a comment).
        // Without TLS msmtp refuses "auth on"; only the loopback case reaches that branch and it uses CRAM-MD5 explicitly.
        const QString auth = s.smtpSecurity == QLatin1String("none") ? QStringLiteral("cram-md5") : QStringLiteral("on");
        t += QStringLiteral("account %1\nhost %2\nport %3\nprotocol smtp\nauth %4\nuser %5\npassword \"%6\"\nfrom %7\n").arg(s.name, s.smtpHost).arg(s.smtpPort).arg(auth, s.smtpUser, masked ? mask : smtpPassword, s.email);
        if (s.smtpSecurity == QLatin1String("none")) t += QStringLiteral("tls off\n");
        else t += QStringLiteral("tls on\ntls_starttls %1\ntls_trust_file system\n").arg(s.smtpSecurity == QLatin1String("starttls") ? QStringLiteral("on") : QStringLiteral("off"));
        t += endMarker(s.name) + QLatin1Char('\n');
        return t;
    };
    MailSetupFile mb;
    mb.key = QStringLiteral("mbsyncrc"); mb.relPath = QStringLiteral("/.mbsyncrc"); mb.path = home + mb.relPath; mb.kind = QStringLiteral("block");
    mb.beginMarker = beginMarker(s.name); mb.endMarker = endMarker(s.name); mb.content = makeBlock(false); mb.shown = makeBlock(true);
    mb.mkdirPath = mailRoot + QLatin1Char('/') + s.name;
    files << mb;
    MailSetupFile ms;
    ms.key = QStringLiteral("msmtprc"); ms.relPath = QStringLiteral("/.msmtprc"); ms.path = home + ms.relPath; ms.kind = QStringLiteral("block");
    ms.beginMarker = beginMarker(s.name); ms.endMarker = endMarker(s.name); ms.content = makeMsmtp(false); ms.shown = makeMsmtp(true);
    files << ms;
    if (!notmuchConfigExists) {
        MailSetupFile nm;
        nm.key = QStringLiteral("notmuch"); nm.relPath = QStringLiteral("/.notmuch-config"); nm.path = home + nm.relPath; nm.kind = QStringLiteral("new");
        nm.content = QStringLiteral("[database]\npath=%1\n\n[user]\nname=%2\nprimary_email=%3\n\n[new]\ntags=unread;inbox\n"
                                    "ignore=.mbsyncstate;.mbsyncstate.new;.mbsyncstate.journal;.uidvalidity;.isyncuidmap.db\n\n[search]\nexclude_tags=deleted;spam\n\n[maildir]\nsynchronize_flags=true\n")
                         .arg(mailRoot, s.displayName.isEmpty() ? s.email : s.displayName, s.email);
        nm.shown = nm.content;
        files << nm;
    }
    return files;
}

QVariantList setupFilesToVariant(const QList<MailSetupFile> &files) {
    QVariantList out;
    for (const MailSetupFile &f : files)
        out.push_back(QVariantMap { { "key", f.key }, { "path", f.path }, { "kind", f.kind }, { "shown", f.shown }, { "content", f.content } });
    return out;
}

QByteArray buildSetupProbeScript(const QString &name, const QString &configPath) {
    static const QRegularExpression nameRe(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,30}$"));
    if (!nameRe.match(name).hasMatch()) return {};
    QByteArray s = "export LC_ALL=C.UTF-8\n";
    if (!configPath.isEmpty()) s += "export NOTMUCH_CONFIG=" + shQuote(configPath) + "\n";
    s += "sec() { printf '#section %s\\n' \"$1\"; }\n"
         "sec home; printf '%s\\n' \"$HOME\"\n"
         "sec files\n"
         "for p in .mbsyncrc .msmtprc .notmuch-config; do if [ -e \"$HOME/$p\" ]; then echo \"$p 1\"; else echo \"$p 0\"; fi; done\n"
         "sec blocks\n"
         "if grep -qxF " + shQuote(beginMarker(name)) + " \"$HOME/.mbsyncrc\" 2>/dev/null; then echo 'mbsync 1'; else echo 'mbsync 0'; fi\n"
         "if grep -qxF " + shQuote(beginMarker(name)) + " \"$HOME/.msmtprc\" 2>/dev/null; then echo 'msmtp 1'; else echo 'msmtp 0'; fi\n"
         "sec tools\n"
         "if command -v mbsync >/dev/null 2>&1; then echo \"mbsync $(mbsync --version 2>&1 | head -1)\"; else echo 'mbsync -'; fi\n"
         "if command -v msmtp >/dev/null 2>&1; then echo \"msmtp $(msmtp --version 2>&1 | head -1)\"; else echo 'msmtp -'; fi\n"
         "if command -v notmuch >/dev/null 2>&1; then echo \"notmuch $(notmuch --version 2>&1 | head -1)\"; else echo 'notmuch -'; fi\n"
         "sec dbpath\n"
         "if command -v notmuch >/dev/null 2>&1; then notmuch config get database.path 2>/dev/null; fi\n"
         "sec end\n";
    return s;
}

MailSetupState parseSetupProbe(const QByteArray &output) {
    MailSetupState st;
    QHash<QString, QByteArray> sections;
    QString current;
    QByteArray body;
    auto flush = [&] { if (!current.isEmpty()) sections.insert(current, body); body.clear(); };
    for (const QByteArray &line : output.split('\n')) {
        if (line.startsWith("#section ")) { flush(); current = QString::fromLatin1(line.mid(9)).trimmed(); continue; }
        body += line + '\n';
    }
    flush();
    if (!sections.contains("end")) { st.problem = QStringLiteral("Réponse incomplète du serveur."); return st; }
    st.home = QString::fromUtf8(sections.value("home")).trimmed();
    if (!st.home.startsWith(QLatin1Char('/'))) { st.problem = QStringLiteral("Dossier personnel introuvable."); return st; }
    for (const QByteArray &l : sections.value("files").split('\n')) {
        const QList<QByteArray> p = l.trimmed().split(' ');
        if (p.size() != 2) continue;
        if (p[0] == ".mbsyncrc") st.hasMbsyncrc = p[1] == "1";
        if (p[0] == ".msmtprc") st.hasMsmtprc = p[1] == "1";
        if (p[0] == ".notmuch-config") st.hasNotmuchConfig = p[1] == "1";
    }
    for (const QByteArray &l : sections.value("blocks").split('\n')) {
        const QList<QByteArray> p = l.trimmed().split(' ');
        if (p.size() != 2) continue;
        if (p[0] == "mbsync") st.blockMbsync = p[1] == "1";
        if (p[0] == "msmtp") st.blockMsmtp = p[1] == "1";
    }
    for (const QByteArray &l : sections.value("tools").split('\n')) {
        const QString t = QString::fromUtf8(l).trimmed();
        const qsizetype sp = t.indexOf(QLatin1Char(' '));
        if (sp < 0) continue;
        const QString name = t.left(sp), version = t.mid(sp + 1).trimmed();
        const QString v = version == QLatin1String("-") ? QString() : version;
        if (name == QLatin1String("mbsync")) st.mbsyncVersion = v;
        if (name == QLatin1String("msmtp")) st.msmtpVersion = v;
        if (name == QLatin1String("notmuch")) st.notmuchVersion = v;
    }
    st.notmuchDbPath = QString::fromUtf8(sections.value("dbpath")).trimmed();
    if (!st.notmuchDbPath.isEmpty() && !st.notmuchDbPath.startsWith(QLatin1Char('/'))) st.notmuchDbPath.clear();
    st.hasNotmuchConfig = st.hasNotmuchConfig || !st.notmuchDbPath.isEmpty();
    st.ok = true;
    return st;
}

QVariantMap setupStateToVariant(const MailSetupState &s) {
    return QVariantMap { { "ok", s.ok }, { "problem", s.problem }, { "home", s.home }, { "hasMbsyncrc", s.hasMbsyncrc }, { "hasMsmtprc", s.hasMsmtprc },
                         { "hasNotmuchConfig", s.hasNotmuchConfig }, { "blockMbsync", s.blockMbsync }, { "blockMsmtp", s.blockMsmtp },
                         { "mbsync", s.mbsyncVersion }, { "msmtp", s.msmtpVersion }, { "notmuch", s.notmuchVersion }, { "mailRoot", s.mailRoot() } };
}

QByteArray buildSetupWriteScript(const QString &name, const QList<MailSetupFile> &files, const QString &stamp, const QString &delimiterBase) {
    static const QRegularExpression nameRe(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,30}$")), stampRe(QStringLiteral("^[0-9]{14}$"));
    if (!nameRe.match(name).hasMatch() || !stampRe.match(stamp).hasMatch() || files.isEmpty() || delimiterBase.isEmpty()) return {};
    QByteArray s = "umask 077\nexport LC_ALL=C.UTF-8\n";
    int index = 0;
    for (const MailSetupFile &f : files) {
        const QString delimiter = delimiterBase + QString::number(++index);
        if (f.content.contains(delimiter) || (f.key != QLatin1String("mbsyncrc") && f.key != QLatin1String("msmtprc") && f.key != QLatin1String("notmuch"))) return {};
        const QByteArray rel = f.relPath.toUtf8();
        s += "f=\"$HOME" + rel + "\"\n";
        s += "new=$(mktemp \"$f.fortiq-new.XXXXXX\") || { echo 'FQ-ERR mktemp' >&2; exit 5; }\n";
        if (f.kind == QLatin1String("new")) {
            s += "if [ -e \"$f\" ]; then rm -f \"$new\"; echo 'FQ-ERR exists' >&2; exit 7; fi\n";
            const qsizetype p = f.content.indexOf(QLatin1String("path="));
            if (p >= 0) {
                const QString dbPath = f.content.mid(p + 5, f.content.indexOf(QLatin1Char('\n'), p) - p - 5);
                s += "mkdir -p " + shQuote(dbPath) + " || { rm -f \"$new\"; echo 'FQ-ERR mkdir' >&2; exit 8; }\n";
            }
        } else {
            if (!f.mkdirPath.isEmpty())
                s += "mkdir -p " + shQuote(f.mkdirPath) + " || { rm -f \"$new\"; echo 'FQ-ERR mkdir' >&2; exit 8; }\n";
            s += "if [ -e \"$f\" ]; then\n"
                 "  cp -p \"$f\" \"$f.fortiq-bak-" + stamp.toUtf8() + "\" && chmod 600 \"$f.fortiq-bak-" + stamp.toUtf8() + "\" || { rm -f \"$new\"; echo 'FQ-ERR backup' >&2; exit 6; }\n"
                 "  awk -v b=" + shQuote(f.beginMarker) + " -v e=" + shQuote(f.endMarker) + " '$0==b{skip=1;next} $0==e{skip=0;next} !skip{print}' \"$f\" > \"$new\" || { rm -f \"$new\"; echo 'FQ-ERR strip' >&2; exit 9; }\n"
                 "  if [ -s \"$new\" ]; then echo >> \"$new\"; fi\n"
                 "fi\n";
        }
        s += "cat >> \"$new\" <<'" + delimiter.toUtf8() + "'\n" + f.content.toUtf8();
        if (!f.content.endsWith(QLatin1Char('\n'))) s += '\n';
        s += delimiter.toUtf8() + "\n";
        s += "mv -f \"$new\" \"$f\" || { rm -f \"$new\"; echo 'FQ-ERR move' >&2; exit 10; }\n"
             "chmod 600 \"$f\"\n"
             "echo '#written " + f.key.toUtf8() + "'\n";
    }
    return s;
}

QStringList parseSetupWritten(const QByteArray &output) {
    QStringList keys;
    for (const QByteArray &l : output.split('\n'))
        if (l.startsWith("#written ")) keys << QString::fromLatin1(l.mid(9)).trimmed();
    return keys;
}

QByteArray buildSmtpTestScript(const QString &account) {
    static const QRegularExpression nameRe(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,30}$"));
    if (!nameRe.match(account).hasMatch()) return {};
    return "export LC_ALL=C.UTF-8\n"
           "command -v msmtp >/dev/null 2>&1 || { echo 'FQ-ERR missing-msmtp' >&2; exit 4; }\n"
           "timeout -k 3 30 msmtp -a " + shQuote(account) + " --serverinfo 2>&1\n";
}

QByteArray buildImapTestScript(const QString &account) {
    static const QRegularExpression nameRe(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,30}$"));
    if (!nameRe.match(account).hasMatch()) return {};
    return "export LC_ALL=C.UTF-8\n"
           "command -v mbsync >/dev/null 2>&1 || { echo 'FQ-ERR missing-mbsync' >&2; exit 4; }\n"
           "timeout -k 3 60 mbsync -l " + shQuote(account) + " 2>&1\n";
}

QByteArray buildSyncScript(const QString &account, const QString &configPath) {
    static const QRegularExpression nameRe(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,30}$"));
    if (!nameRe.match(account).hasMatch()) return {};
    QByteArray s = "export LC_ALL=C.UTF-8\n";
    if (!configPath.isEmpty()) s += "export NOTMUCH_CONFIG=" + shQuote(configPath) + "\n";
    s += "command -v mbsync >/dev/null 2>&1 || { echo 'FQ-ERR missing-mbsync' >&2; exit 4; }\n"
         "command -v notmuch >/dev/null 2>&1 || { echo 'FQ-ERR missing-notmuch' >&2; exit 4; }\n"
         "echo '== mbsync'\n"
         "timeout -k 10 900 mbsync " + shQuote(account) + " 2>&1\n"
         "rc=$?\n"
         "echo \"== mbsync terminé (code $rc)\"\n"
         "if [ $rc -ne 0 ]; then exit 20; fi\n"
         "echo '== notmuch new'\n"
         "timeout -k 10 600 notmuch new 2>&1\n"
         "exit $?\n";
    return s;
}

QString describeActionFailure(const QString &what, int exitCode, const QString &stderrText) {
    const QString err = stderrText.trimmed();
    if (err.contains(QLatin1String("FQ-ERR missing-msmtp"))) return QStringLiteral("msmtp n'est pas installé sur ce serveur (sudo apt install msmtp).");
    if (err.contains(QLatin1String("FQ-ERR missing-mbsync"))) return QStringLiteral("mbsync n'est pas installé sur ce serveur (sudo apt install isync).");
    if (err.contains(QLatin1String("FQ-ERR missing-notmuch"))) return QStringLiteral("notmuch n'est pas installé sur ce serveur (sudo apt install notmuch).");
    if (err.contains(QLatin1String("FQ-ERR exists"))) return QStringLiteral("Un fichier existe déjà : rien n'a été remplacé.");
    if (err.contains(QLatin1String("FQ-ERR backup"))) return QStringLiteral("La sauvegarde du fichier existant a échoué : rien n'a été modifié.");
    if (err.contains(QLatin1String("FQ-ERR"))) return QStringLiteral("%1 : écriture impossible sur le serveur (%2).").arg(what, err.section(QLatin1Char('\n'), 0, 0));
    if (exitCode == 124 || exitCode == 137) return QStringLiteral("%1 : délai dépassé.").arg(what);
    if (err.isEmpty()) return QStringLiteral("%1 : code %2.").arg(what).arg(exitCode);
    return QStringLiteral("%1 : %2").arg(what, err.section(QLatin1Char('\n'), 0, 3).left(600));
}

}  // namespace fortiq
