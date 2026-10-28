#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace fortiq {

// Mail app, sending: the message is built here (headers, RFC 2047 words, base64
// UTF-8 body) and handed to `msmtp` on the server through the stdin script. The
// message never travels as a command argument; recipients are validated and
// single-quoted. A copy is stored with `notmuch insert` (tag sent). See docs/MAIL.md.

struct MailAddress {
    QString name;   // display name, may be empty
    QString addr;   // addr-spec (ASCII)
};

// Parses "a@b, Name <c@d>, \"Doe, J\" <e@f>" (commas or semicolons). Returns false and sets
// `error` for syntax errors, non-ASCII or malformed addresses.
bool parseMailAddresses(const QString &text, QList<MailAddress> *out, QString *error);
QString formatMailAddress(const MailAddress &address);     // header form, encoded when needed

struct MailDraft {
    QString account;                 // msmtp account name
    QString fromAddress, fromName;   // from the account
    QString to, cc, bcc, subject, body;
    QString inReplyTo;               // Message-ID of the parent without angle brackets, or empty
};

struct BuiltMessage {
    bool ok = false;
    QString error;
    QByteArray data;                 // complete message with LF line ends (msmtp converts)
    QString messageId;               // without angle brackets
    QStringList recipients;          // envelope: to + cc + bcc, de-duplicated
};
// `token` is random hex used in the Message-ID.
BuiltMessage buildMessage(const MailDraft &draft, const QDateTime &when, const QString &token);

QString encodeHeaderText(const QString &text);   // RFC 2047 B words when not plain ASCII (folded with "\n ")
bool validMailAccountName(const QString &name);
bool validMessageId(const QString &id);

// ---- scripts ----
// Exit codes: 0 sent and copy stored; 10 refused/failed before delivery; 11 sent but the copy was not stored;
// 12 timeout: the delivery state is unknown; 4 msmtp missing.
QByteArray buildMailSendScript(const QString &account, const QStringList &recipients, const QByteArray &message, const QString &inReplyTo,
                               const QString &configPath, const QString &delimiter, const QVariantList &attachments = {});
QByteArray buildMailAccountsScript();   // account / from lines of the msmtp configuration (never the password)

struct MailSendAccount { QString name, from; };
QList<MailSendAccount> parseMailAccounts(const QByteArray &output);
QVariantList sendAccountsToVariant(const QList<MailSendAccount> &accounts);

// Text for the user from the script outcome.
QString describeSendOutcome(int exitCode, const QString &stderrText, bool *delivered, bool *unknown);

// ---- reply helpers ----
QString replySubject(const QString &subject);                    // "Re: x" (no stacking)
QString replyBody(const QString &from, const QString &date, const QString &text);   // attribution + "> " quoting

}  // namespace fortiq
