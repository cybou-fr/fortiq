#include "SshInternal.h"

namespace fortiq {
SshResult SshSession::moveToTrash(const QString &path)
{
    if (!isOpen()) return transferFail(QStringLiteral("La session SSH n'est pas ouverte."));
    const auto source = cleanAbsolutePath(path);
    const QString trash = d->home + QStringLiteral("/.local/share/Trash");
    if (!source || *source == "/" || d->home.isEmpty() || *source == d->home
        || d->home.startsWith(*source + "/") || trash.startsWith(*source + "/") || *source == trash)
        return transferFail(QStringLiteral("This location cannot be moved to the Trash."));
    if (source->startsWith(trash + "/"))
        return transferFail(QStringLiteral("Already in the Trash. Permanent deletion is not available yet."));

    auto statPath = [this](const QString &p, LIBSSH2_SFTP_ATTRIBUTES *a) {
        const QByteArray b = p.toUtf8();
        return libssh2_sftp_stat_ex(d->sftp, b.constData(), static_cast<unsigned int>(b.size()), LIBSSH2_SFTP_LSTAT, a) == 0;
    };
    auto ensureDir = [this, &statPath](const QString &p) {
        LIBSSH2_SFTP_ATTRIBUTES a{};
        if (statPath(p, &a)) return LIBSSH2_SFTP_S_ISDIR(a.permissions);
        const QByteArray b = p.toUtf8();
        return libssh2_sftp_mkdir_ex(d->sftp, b.constData(), static_cast<unsigned int>(b.size()), 0700) == 0;
    };
    LIBSSH2_SFTP_ATTRIBUTES srcAttr{};
    if (!statPath(*source, &srcAttr)) return transferFail(QStringLiteral("The item no longer exists."));
    for (const QString &p : { d->home + "/.local", d->home + "/.local/share", trash, trash + "/files", trash + "/info" })
        if (!ensureDir(p)) return transferFail(QStringLiteral("Cannot prepare the Trash folder in your home."));

    // Reserve a unique name by exclusively creating its .trashinfo first,
    // as the specification requires; files/<name> is then the rename target.
    const QString base = source->section('/', -1);
    QString name = base;
    LIBSSH2_SFTP_HANDLE *info = nullptr;
    QByteArray infoPath;
    for (int n = 1; n < 1000 && !info; ++n) {
        name = n == 1 ? base : numberedName(base, n);
        LIBSSH2_SFTP_ATTRIBUTES a{};
        if (statPath(trash + "/files/" + name, &a)) continue;
        infoPath = (trash + "/info/" + name + ".trashinfo").toUtf8();
        info = libssh2_sftp_open_ex(d->sftp, infoPath.constData(), static_cast<unsigned int>(infoPath.size()),
            LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL, 0600, LIBSSH2_SFTP_OPENFILE);
    }
    if (!info) return transferFail(QStringLiteral("Cannot reserve a name in the Trash."));
    const QByteArray content = "[Trash Info]\nPath=" + QUrl::toPercentEncoding(*source, "/")
        + "\nDeletionDate=" + QDateTime::currentDateTime().toString(Qt::ISODate).left(19).toUtf8() + "\n";
    const auto wrote = libssh2_sftp_write(info, content.constData(), static_cast<size_t>(content.size()));
    const bool written = static_cast<qsizetype>(wrote) == content.size();
    libssh2_sftp_close(info);
    auto dropInfo = [this, &infoPath] {
        libssh2_sftp_unlink_ex(d->sftp, infoPath.constData(), static_cast<unsigned int>(infoPath.size()));
    };
    if (!written) { dropInfo(); return transferFail(QStringLiteral("Cannot write the Trash record.")); }

    const QByteArray from = source->toUtf8(), to = (trash + "/files/" + name).toUtf8();
    if (sftpMutation(d->handle, "trash", [&] { return libssh2_sftp_rename_ex(d->sftp, from.constData(), static_cast<unsigned int>(from.size()),
                               to.constData(), static_cast<unsigned int>(to.size()), 0); }) != 0) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->closeLost();
            return transferFail(QStringLiteral("Connexion perdue ; vérifiez le dossier et la corbeille avant de réessayer."), QStringLiteral("unknown"));
        }
        dropInfo();
        // No copy-and-delete fallback: that would not be reversible or atomic.
        return transferFail(QStringLiteral("Cannot move to the Trash (permission error, or the item is on another filesystem)."));
    }
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = trash + "/files/" + name;
    r.message = QStringLiteral("Moved to the Trash");
    return r;
}

namespace {
// Removes a tree without following symlinks. Returns false on the first
// failure; `removed` counts deleted entries.
bool removeTree(LIBSSH2_SESSION *session, LIBSSH2_SFTP *sftp, const QString &path, int depth, qint64 &removed)
{
    if (depth > 128) return false;
    const QByteArray p = path.toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES a{};
    if (libssh2_sftp_stat_ex(sftp, p.constData(), static_cast<unsigned int>(p.size()), LIBSSH2_SFTP_LSTAT, &a) != 0)
        return false;
    if (!LIBSSH2_SFTP_S_ISDIR(a.permissions)) {
        if (sftpMutation(session, "unlink", [&] { return libssh2_sftp_unlink_ex(sftp, p.constData(), static_cast<unsigned int>(p.size())); }) != 0) return false;
        ++removed;
        return true;
    }
    QStringList children;
    LIBSSH2_SFTP_HANDLE *dir = libssh2_sftp_opendir(sftp, p.constData());
    if (!dir) return false;
    char name[8192];
    while (true) {
        LIBSSH2_SFTP_ATTRIBUTES ca{};
        const int n = libssh2_sftp_readdir_ex(dir, name, sizeof(name), nullptr, 0, &ca);
        if (n == 0) break;
        if (n < 0) { libssh2_sftp_closedir(dir); return false; }
        const QByteArray raw(name, n);
        const QString child = QString::fromUtf8(raw);
        if (child == "." || child == "..") continue;
        if (child.toUtf8() != raw) { libssh2_sftp_closedir(dir); return false; } // non-UTF-8 name: refuse, not guess
        children << child;
    }
    libssh2_sftp_closedir(dir);
    for (const QString &child : children) {
        const auto childPath = safeChildPath(path, child);
        if (!childPath || !removeTree(session, sftp, *childPath, depth + 1, removed)) return false;
    }
    if (sftpMutation(session, "rmdir", [&] { return libssh2_sftp_rmdir_ex(sftp, p.constData(), static_cast<unsigned int>(p.size())); }) != 0) return false;
    ++removed;
    return true;
}
}

SshResult SshSession::restoreFromTrash(const QString &trashedPath, ConflictPolicy policy)
{
    if (!isOpen()) return transferFail(QStringLiteral("La session SSH n'est pas ouverte."));
    const QString files = d->home + QStringLiteral("/.local/share/Trash/files");
    const auto source = cleanAbsolutePath(trashedPath);
    if (!source || parentPath(*source) != files) return transferFail(QStringLiteral("Only items at the top of the Trash can be restored."));
    const QString name = source->section('/', -1);
    const QString infoPath = d->home + QStringLiteral("/.local/share/Trash/info/") + name + QStringLiteral(".trashinfo");
    const SshResult info = readFile(infoPath, 64 * 1024);
    if (info.status != SshResult::Status::Ok) return transferFail(QStringLiteral("No Trash record for this item; its original location is unknown."));
    QString original;
    for (const QByteArray &line : info.data.split('\n'))
        if (line.startsWith("Path=")) original = QUrl::fromPercentEncoding(line.mid(5).trimmed());
    if (original.isEmpty()) return transferFail(QStringLiteral("The Trash record has no original path."));
    if (!original.startsWith('/')) original = d->home + '/' + original; // relative to the Trash's top dir
    const auto cleanOriginal = cleanAbsolutePath(original);
    if (!cleanOriginal || *cleanOriginal == "/") return transferFail(QStringLiteral("The Trash record has an invalid path."));

    auto exists = [this](const QString &path, bool wantDir = false) {
        const QByteArray b = path.toUtf8();
        LIBSSH2_SFTP_ATTRIBUTES a{};
        if (libssh2_sftp_stat_ex(d->sftp, b.constData(), static_cast<unsigned int>(b.size()), LIBSSH2_SFTP_LSTAT, &a) != 0) return false;
        return !wantDir || LIBSSH2_SFTP_S_ISDIR(a.permissions);
    };
    const QString folder = parentPath(*cleanOriginal);
    if (!exists(folder, true))
        return transferFail(QStringLiteral("The original folder %1 no longer exists.").arg(folder));
    QString target = *cleanOriginal;
    if (exists(target)) {
        if (policy != ConflictPolicy::KeepBoth)
            return transferFail(QStringLiteral("« %1 » already exists in %2.").arg(target.section('/', -1), folder), QStringLiteral("conflict"));
        const QString base = target.section('/', -1);
        for (int n = 2; n < 1000 && exists(target); ++n) target = *safeChildPath(folder, numberedName(base, n));
        if (exists(target)) return transferFail(QStringLiteral("No free name to restore to."), QStringLiteral("conflict"));
    }
    const QByteArray from = source->toUtf8(), to = target.toUtf8();
    if (sftpMutation(d->handle, "restore", [&] { return libssh2_sftp_rename_ex(d->sftp, from.constData(), static_cast<unsigned int>(from.size()),
                               to.constData(), static_cast<unsigned int>(to.size()), 0); }) != 0) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->closeLost();
            return transferFail(QStringLiteral("Connexion perdue ; vérifiez la corbeille et %1 avant de réessayer.").arg(folder), QStringLiteral("unknown"));
        }
        return transferFail(QStringLiteral("Cannot restore (permission error, or another filesystem)."));
    }
    const QByteArray infoUtf8 = infoPath.toUtf8();
    libssh2_sftp_unlink_ex(d->sftp, infoUtf8.constData(), static_cast<unsigned int>(infoUtf8.size()));
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = target;
    r.message = QStringLiteral("Restored");
    return r;
}

SshResult SshSession::deleteFromTrash(const QString &trashedPath)
{
    if (!isOpen()) return transferFail(QStringLiteral("La session SSH n'est pas ouverte."));
    const QString files = d->home + QStringLiteral("/.local/share/Trash/files");
    const auto source = cleanAbsolutePath(trashedPath);
    // Only whole items of the Trash: permanent deletion never reaches elsewhere.
    if (d->home.isEmpty() || !source || parentPath(*source) != files)
        return transferFail(QStringLiteral("Permanent deletion is only available for items in the Trash."));
    qint64 removed = 0;
    if (!removeTree(d->handle, d->sftp, *source, 0, removed)) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL && libssh2_session_last_errno(d->handle) != 0) {
            d->closeLost();
            return transferFail(QStringLiteral("Connexion perdue pendant la suppression ; une partie de l'élément a peut-être déjà disparu."), QStringLiteral("unknown"));
        }
        return transferFail(QStringLiteral("Could not delete everything (%1 entries removed; permission error?).").arg(removed));
    }
    const QByteArray info = (d->home + QStringLiteral("/.local/share/Trash/info/") + source->section('/', -1) + QStringLiteral(".trashinfo")).toUtf8();
    libssh2_sftp_unlink_ex(d->sftp, info.constData(), static_cast<unsigned int>(info.size()));
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.bytes = removed;
    r.message = QStringLiteral("Deleted permanently");
    return r;
}

SshResult SshSession::emptyTrash()
{
    if (!isOpen() || d->home.isEmpty()) return transferFail(QStringLiteral("La session SSH n'est pas ouverte."));
    const QString trash = d->home + QStringLiteral("/.local/share/Trash");
    qint64 removed = 0;
    int failed = 0;
    for (const QString &sub : { QStringLiteral("files"), QStringLiteral("info") }) {
        const SshResult listing = list(trash + '/' + sub);
        if (listing.status != SshResult::Status::Ok) {
            if (!isOpen()) return transferFail(QStringLiteral("Connexion perdue pendant le vidage de la corbeille."), QStringLiteral("unknown"));
            continue; // no such folder: nothing to empty
        }
        for (const QVariant &v : listing.entries) {
            const QString path = v.toMap().value("path").toString();
            if (parentPath(path) != trash + '/' + sub || !removeTree(d->handle, d->sftp, path, 0, removed)) {
                const int err = libssh2_session_last_errno(d->handle);
                if (err != 0 && err != LIBSSH2_ERROR_SFTP_PROTOCOL) {
                    d->closeLost();
                    return transferFail(QStringLiteral("Connexion perdue pendant le vidage de la corbeille ; une partie a peut-être disparu."), QStringLiteral("unknown"));
                }
                ++failed;
            }
        }
    }
    if (!isOpen()) return transferFail(QStringLiteral("Connexion perdue pendant le vidage de la corbeille."), QStringLiteral("unknown"));
    if (failed > 0) return transferFail(QStringLiteral("%1 item(s) could not be deleted (permission error?).").arg(failed));
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.bytes = removed;
    r.message = QStringLiteral("Trash emptied");
    return r;
}
}  // namespace fortiq
