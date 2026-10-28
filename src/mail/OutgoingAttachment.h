// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QByteArray>
#include <QVariantMap>
#include <QVariantList>

namespace fortiq {
inline constexpr qint64 OutgoingAttachmentLimit = 10 * 1024 * 1024;
inline constexpr int OutgoingAttachmentCount = 10;

// Python's standard email/hashlib/os libraries run over SSH, not a new server CLI.
QByteArray buildOutgoingProbeScript(const QString &path, const QString &name, qint64 remaining);
QVariantMap parseOutgoingProbe(const QByteArray &data, const QString &path, const QString &name);
// Re-checks every selected version and adds MIME parts to the private $t message.
// Failure always precedes msmtp. No attachment bytes are returned to the client.
QByteArray buildOutgoingMimeScript(const QVariantList &attachments);
}
