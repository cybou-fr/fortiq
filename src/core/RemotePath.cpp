#include "RemotePath.h"
#include <QDir>

namespace fortiq {
std::optional<QString> safeChildPath(const QString &parent, const QString &name)
{
    if (!parent.startsWith('/') || parent.contains(QChar::Null) ||
        name.isEmpty() || name == "." || name == ".." ||
        name.contains('/') || name.contains(QChar::Null)) {
        return std::nullopt;
    }
    return QDir::cleanPath(parent == "/" ? "/" + name : parent + "/" + name);
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
    return QDir::cleanPath(path + "/..");
}
}
