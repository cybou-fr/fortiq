#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace fortiq {

// Mail app: a GUI over `notmuch` on the selected Linux server (mail is fetched
// there by the user's own tools, for example mbsync). Everything passes through
// SSH as a `sh -s` script; queries, ids and tags are data (single-quoted, after
// `--`). Message HTML is reduced by the same sanitizer as the browser: no
// scripts, styles, remote images or forms. See docs/MAIL.md.

struct MailThread {
    QString id;                // notmuch thread id (hex)
    qint64 timestamp = 0;      // seconds since epoch
    int matched = 0, total = 0;
    QString authors, subject;
    QStringList tags;
    bool unread = false, flagged = false, attachment = false;
};

struct MailAttachment {
    QString name, type;
    qint64 size = 0;           // encoded size reported by notmuch (approximate)
    int part = 0;              // MIME part number for `notmuch show --part`
};

constexpr int kMaxAttachmentsShown = 200;

struct MailMessage {
    QString id;                // Message-ID without angle brackets
    qint64 timestamp = 0;
    int depth = 0;             // reply nesting inside the thread
    bool match = true;         // false: only shown because it belongs to the thread
    QString from, to, cc, date, subject;
    QStringList tags;
    QString text;              // text/plain body ("" when the message is HTML only)
    QString html;              // sanitized rich text ("" when there is no HTML part)
    QString htmlPlain;         // plain text of the HTML part (shown when there is no text part)
    int htmlLinks = 0, htmlScripts = 0, htmlImages = 0;   // what the sanitizer removed or kept
    QList<MailAttachment> attachments;   // at most kMaxAttachmentsShown
    int attachmentsOmitted = 0;          // parts beyond that cap (a hostile message can carry thousands)
};

// ---- validation (everything below is sent to the server as data) ----
QString validateMailQuery(const QString &query);     // "" when valid
bool validMailThreadId(const QString &id);           // hex only
bool validMailTag(const QString &tag);               // letters, digits, . _ - ; no leading sign

// ---- scripts ----
// `configPath` is an optional NOTMUCH_CONFIG for non-default installations.
QByteArray buildMailOverviewScript(const QString &configPath);
QByteArray buildMailSearchScript(const QString &query, int limit, int offset, const QString &configPath);
QByteArray buildMailThreadScript(const QString &threadId, const QString &configPath);
// add / remove: validated tag names; target is a thread id. Returns an empty script on invalid input.
QByteArray buildMailTagScript(const QString &threadId, const QStringList &add, const QStringList &remove, const QString &configPath);

// ---- parsing ----
struct MailOverview {
    bool ok = false;
    QString problem;           // user-facing text when !ok
    QString version;
    QVariantList folders;      // {key,title,query,count}
};
MailOverview parseMailOverview(const QByteArray &output);

QList<MailThread> parseMailSearch(const QByteArray &json, bool *ok = nullptr);
QVariantList threadsToVariant(const QList<MailThread> &threads);
QList<MailMessage> parseMailThread(const QByteArray &json, bool *ok = nullptr);
QVariantList messagesToVariant(const QList<MailMessage> &messages);

// Human text for a failed notmuch run (exit code and stderr).
QString describeMailFailure(int exitCode, const QString &stderrText);

// Compact local date text for lists, e.g. "22 oct. 08:30" (today: "08:30").
QString mailDateText(qint64 timestamp, qint64 nowSeconds);

}  // namespace fortiq
