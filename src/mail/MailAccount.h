#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace fortiq {

// Mail account wizard: writes ~/.mbsyncrc, ~/.msmtprc and (only when absent)
// ~/.notmuch-config on the server, with the password in the files (mode 0600) as the
// owner decided. Nothing is written without the user seeing the exact text and agreeing
// per file. Existing files are never replaced as a whole: FORTIQ manages only its own
// marked block per account and keeps a timestamped backup. See docs/MAIL.md.

struct MailAccountSpec {
    QString name;                       // account id used by msmtp and mbsync
    QString displayName;                // sender name (kept by the client, also in notmuch [user])
    QString email;
    QString imapHost, imapUser, imapSecurity;   // security: "tls" | "starttls" | "none" (localhost only)
    int imapPort = 993;
    QString smtpHost, smtpUser, smtpSecurity;
    int smtpPort = 587;
    QString password;                   // IMAP password
    QString smtpPassword;               // empty = same as the IMAP password
};

MailAccountSpec accountSpecFromVariant(const QVariantMap &map);
QString validateAccountSpec(const MailAccountSpec &spec);     // "" when valid
QString validatePassword(const QString &password);            // "" when it can be written safely
// What the wizard must say about the password it is about to write (audit R3): it ends up in clear text in files on the server.
QString passwordStorageWarning();
// Provider-specific advice (French) from the IMAP host or the address domain: an app password where the provider has them, a
// known obstacle (OAuth2 only, Bridge) otherwise. Empty for an unknown provider.
QString passwordAdvice(const QString &imapHost, const QString &email);

struct MailSetupFile {
    QString key;            // "mbsyncrc" | "msmtprc" | "notmuch"
    QString relPath;        // below the home directory, e.g. "/.mbsyncrc"
    QString path;           // shown to the user (home + relPath)
    QString kind;           // "block" (our marked block inside a possibly existing file) | "new" (whole file, only when absent)
    QString beginMarker, endMarker;   // block markers ("" for kind "new")
    QString content;        // exact text that will be written
    QString shown;          // the same text with the password(s) masked
    QString mkdirPath;      // directory created before writing (the account's Maildir root), "" for none
};

constexpr const char *kPasswordMask = "••••••••";

// `mailRoot`: where the Maildir of the account lives (notmuch database path, or home + "/Mail").
// `notmuchConfigExists`: then the notmuch configuration is left alone and not offered.
QList<MailSetupFile> buildSetupFiles(const MailAccountSpec &spec, const QString &home, const QString &mailRoot, bool notmuchConfigExists);
QVariantList setupFilesToVariant(const QList<MailSetupFile> &files);

// ---- scripts ----
QByteArray buildSetupProbeScript(const QString &accountName, const QString &configPath);
struct MailSetupState {
    bool ok = false;
    QString problem;
    QString home;
    bool hasMbsyncrc = false, hasMsmtprc = false, hasNotmuchConfig = false;
    bool blockMbsync = false, blockMsmtp = false;     // a FORTIQ block for this account already exists
    QString mbsyncVersion, msmtpVersion, notmuchVersion;   // empty = missing
    QString notmuchDbPath;
    QString mailRoot() const { return notmuchDbPath.isEmpty() ? home + QStringLiteral("/Mail") : notmuchDbPath; }
};
MailSetupState parseSetupProbe(const QByteArray &output);
QVariantMap setupStateToVariant(const MailSetupState &state);

// Writes the chosen files (`files` is a subset of buildSetupFiles). Empty script on invalid input.
QByteArray buildSetupWriteScript(const QString &accountName, const QList<MailSetupFile> &files, const QString &stamp, const QString &delimiterBase);
QStringList parseSetupWritten(const QByteArray &output);     // keys written

QByteArray buildSmtpTestScript(const QString &account);
QByteArray buildImapTestScript(const QString &account);
QByteArray buildSyncScript(const QString &account, const QString &configPath);
QString describeActionFailure(const QString &what, int exitCode, const QString &stderrText);

}  // namespace fortiq
