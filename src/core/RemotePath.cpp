#include "RemotePath.h"
#include <QStringList>

namespace fortiq {
std::optional<QString> safeChildPath(const QString &parent, const QString &name)
{
    if (!parent.startsWith('/') || parent.contains(QChar::Null) ||
        name.isEmpty() || name == "." || name == ".." ||
        name.contains('/') || name.contains(QChar::Null)) {
        return std::nullopt;
    }
    // Not QDir::cleanPath: on Windows it turns a backslash in a Linux name into a separator,
    // so "a\b" would address the entry "b" inside "a" (another file).
    return cleanAbsolutePath(parent == "/" ? "/" + name : parent + "/" + name);
}

std::optional<QString> cleanAbsolutePath(const QString &path)
{
    if (!path.startsWith('/') || path.contains(QChar::Null)) return std::nullopt;
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);
    for (const QString &part : parts)
        if (part == "." || part == "..") return std::nullopt;
    return "/" + parts.join('/');
}

QString parentPath(const QString &path)
{
    if (!path.startsWith('/') || path == "/") return "/";
    const QString clean = cleanAbsolutePath(path).value_or(path);
    const qsizetype slash = clean.lastIndexOf('/');
    return slash <= 0 ? QStringLiteral("/") : clean.left(slash);
}
}
