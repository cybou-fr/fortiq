#pragma once

#include <QString>
#include <optional>

namespace fortiq {
// SFTP paths are not shell commands. Reject ambiguous names; never concatenate
// remote input into a shell string. Files with non-UTF8 names are unsupported
// by this first slice and skipped by the SSH adapter rather than corrupted.
std::optional<QString> safeChildPath(const QString &parent, const QString &name);
QString parentPath(const QString &path);
}
