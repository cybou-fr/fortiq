#include "MailAttachment.h"

#include "MailSend.h"

#include <QRegularExpression>

namespace fortiq {

namespace {
QByteArray shQuote(const QString &value) {
    QByteArray v = value.toUtf8();
    v.replace("'", "'\\''");
    return "'" + v + "'";
}
}  // namespace

QString sanitizeAttachmentName(const QString &name) {
    QString out;
    for (const QChar c : name) {
        const ushort u = c.unicode();
        const bool bidi = (u >= 0x202A && u <= 0x202E) || (u >= 0x2066 && u <= 0x2069) || u == 0x200E || u == 0x200F || u == 0x061C;   // spoofing: "invoice<RLO>txt.exe"
        if (bidi || u < 0x20 || u == 0x7f || c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':') || c == QLatin1Char('*') || c == QLatin1Char('?')
            || c == QLatin1Char('"') || c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('|'))
            out += QLatin1Char('_');
        else out += c;
    }
    out = out.trimmed();
    while (out.startsWith(QLatin1Char('.'))) out.remove(0, 1);
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' '))) out.chop(1);
    if (out.size() > 120) {
        // keep the extension when there is a short one
        const qsizetype dot = out.lastIndexOf(QLatin1Char('.'));
        const QString ext = dot > 0 && out.size() - dot <= 12 ? out.mid(dot) : QString();
        out = out.left(120 - ext.size());
        if (out.size() > 0 && out.at(out.size() - 1).isHighSurrogate()) out.chop(1);
        out += ext;
    }
    return out.isEmpty() ? QStringLiteral("piece-jointe") : out;
}

QByteArray buildAttachmentSaveScript(const QString &messageId, int part, const QString &fileName, const QString &mode, const QString &configPath) {
    if (!validMessageId(messageId) || part < 1 || part > 100000 || (mode != QLatin1String("server") && mode != QLatin1String("temp"))) return {};
    QByteArray s = "export LC_ALL=C.UTF-8\n";
    if (!configPath.isEmpty()) s += "export NOTMUCH_CONFIG=" + shQuote(configPath) + "\n";
    s += "command -v notmuch >/dev/null 2>&1 || { echo 'FQ-ERR missing-notmuch' >&2; exit 4; }\n";
    if (mode == QLatin1String("server")) {
        s += "umask 077\n"
             "dir=\"$HOME/Downloads\"\n"
             "mkdir -p \"$dir\" || { echo 'FQ-ERR mkdir' >&2; exit 5; }\n";
    } else {
        s += "umask 077\n"
             "base=\"$HOME/.cache/fortiq-mail\"\n"
             "mkdir -p \"$base\" || { echo 'FQ-ERR mkdir' >&2; exit 5; }\n"
             "find \"$base\" -mindepth 1 -maxdepth 1 -type d -name 'att.*' -mmin +1440 -exec rm -rf {} + 2>/dev/null\n"
             "dir=$(mktemp -d \"$base/att.XXXXXX\") || { echo 'FQ-ERR mkdir' >&2; exit 5; }\n";
    }
    s += "name=" + shQuote(sanitizeAttachmentName(fileName)) + "\n"
         "stem=${name%.*}; ext=.${name##*.}\n"
         "if [ \"$stem\" = \"$name\" ] || [ -z \"$stem\" ]; then stem=$name; ext=; fi\n"
         "final=\"$dir/$name\"; i=1\n"
         "while [ -e \"$final\" ]; do final=\"$dir/$stem ($i)$ext\"; i=$((i+1)); if [ $i -gt 999 ]; then echo 'FQ-ERR toomany' >&2; exit 9; fi; done\n"
         "tmp=$(mktemp \"$dir/.fortiq-part.XXXXXX\") || { echo 'FQ-ERR mktemp' >&2; exit 5; }\n"
         "notmuch show --format=raw --part=" + QByteArray::number(part) + " -- " + shQuote(QStringLiteral("id:") + messageId) + " > \"$tmp\"\n"
         "rc=$?\n"
         "if [ $rc -ne 0 ]; then rm -f \"$tmp\"; echo 'FQ-ERR notmuch' >&2; exit 6; fi\n"
         "if [ ! -s \"$tmp\" ]; then rm -f \"$tmp\"; echo 'FQ-ERR empty' >&2; exit 7; fi\n"
         "mv \"$tmp\" \"$final\" || { rm -f \"$tmp\"; echo 'FQ-ERR move' >&2; exit 8; }\n";
    s += "chmod 600 \"$final\"\n";   // attachments of private mail stay private on the server too
    s += "printf '#path %s\\n' \"$final\"\n"
         "printf '#size %s\\n' \"$(wc -c < \"$final\")\"\n";
    return s;
}

AttachmentSaved parseAttachmentSaved(const QByteArray &output) {
    AttachmentSaved r;
    for (const QByteArray &line : output.split('\n')) {
        if (line.startsWith("#path ")) r.path = QString::fromUtf8(line.mid(6));
        else if (line.startsWith("#size ")) r.size = line.mid(6).trimmed().toLongLong();
    }
    r.ok = r.path.startsWith(QLatin1Char('/')) && r.size > 0;
    return r;
}

QString describeAttachmentFailure(int exitCode, const QString &stderrText) {
    const QString err = stderrText;
    if (err.contains(QLatin1String("FQ-ERR missing-notmuch"))) return QStringLiteral("notmuch n'est pas installé sur ce serveur.");
    if (err.contains(QLatin1String("FQ-ERR empty"))) return QStringLiteral("La pièce jointe est introuvable ou vide : rien n'a été enregistré.");
    if (err.contains(QLatin1String("FQ-ERR notmuch"))) return QStringLiteral("notmuch n'a pas pu lire cette pièce jointe (message introuvable ?).");
    if (err.contains(QLatin1String("FQ-ERR toomany"))) return QStringLiteral("Trop de fichiers du même nom dans ~/Downloads.");
    if (err.contains(QLatin1String("FQ-ERR"))) return QStringLiteral("Écriture impossible sur le serveur (dossier ou espace disque).");
    if (exitCode == 124 || exitCode == 137) return QStringLiteral("Délai dépassé.");
    return QStringLiteral("L'enregistrement de la pièce jointe a échoué (code %1).").arg(exitCode);
}

bool isAttachmentTempDirectory(const QString &directory) {
    static const QRegularExpression re(QStringLiteral("^/[^\\n\\r\\0]+/\\.cache/fortiq-mail/att\\.[A-Za-z0-9]{6}$"));
    return re.match(directory).hasMatch() && !directory.contains(QLatin1String(".."));
}

QByteArray buildAttachmentCleanupScript(const QString &directory) {
    if (!isAttachmentTempDirectory(directory)) return {};
    return "d=" + shQuote(directory) + "\n"
           "case \"$d\" in \"$HOME\"/.cache/fortiq-mail/att.*) rm -rf -- \"$d\" ;; esac\n";
}

}  // namespace fortiq
