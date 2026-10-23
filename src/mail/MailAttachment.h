#pragma once

#include <QByteArray>
#include <QString>

namespace fortiq {

// Mail attachments stay on the server: `notmuch show --format=raw --part=N` writes the decoded part into a
// file there. "server" mode keeps it in ~/Downloads; "temp" mode (to fetch it to this computer through the
// ordinary SFTP download, then delete the temporary copy) uses ~/.cache/fortiq-mail. See docs/MAIL.md.

// A safe single file name: no path separators, control characters or leading dots; at most 120 characters.
QString sanitizeAttachmentName(const QString &name);

// mode: "server" | "temp". Empty script on invalid input (message id, part number, mode).
QByteArray buildAttachmentSaveScript(const QString &messageId, int part, const QString &fileName, const QString &mode, const QString &configPath);

struct AttachmentSaved {
    bool ok = false;
    QString path;      // absolute path on the server
    qint64 size = 0;
};
AttachmentSaved parseAttachmentSaved(const QByteArray &output);
QString describeAttachmentFailure(int exitCode, const QString &stderrText);

// Removes a temporary copy; only a directory below ~/.cache/fortiq-mail/att.* is ever touched.
QByteArray buildAttachmentCleanupScript(const QString &directory);
bool isAttachmentTempDirectory(const QString &directory);

}  // namespace fortiq
