#include "SshInternal.h"

namespace fortiq {
namespace {
bool validName(const QString &name) {
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/')
        && !name.contains(QChar::Null) && name.toUtf8().size() <= 255;
}
}

SshResult SshSession::makeDirectory(const QString &dir, const QString &name)
{
    if (!isOpen()) return transferFail(QStringLiteral("La session SSH n'est pas ouverte."));
    const auto parent = cleanAbsolutePath(dir);
    if (!parent || !validName(name.trimmed())) return transferFail(QStringLiteral("Invalid folder name."));
    const QString target = *safeChildPath(*parent, name.trimmed());
    const QByteArray p = target.toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES a{};
    if (libssh2_sftp_stat_ex(d->sftp, p.constData(), static_cast<unsigned int>(p.size()), LIBSSH2_SFTP_LSTAT, &a) == 0)
        return transferFail(QStringLiteral("« %1 » already exists.").arg(name.trimmed()), QStringLiteral("conflict"));
    if (sftpMutation(d->handle, "mkdir", [&] { return libssh2_sftp_mkdir_ex(d->sftp, p.constData(), static_cast<unsigned int>(p.size()), 0755); }) != 0) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->closeLost();
            return transferFail(QStringLiteral("Connexion perdue ; vérifiez si le dossier a été créé."), QStringLiteral("unknown"));
        }
        return transferFail(QStringLiteral("Cannot create the folder (permission error?)."));
    }
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = target;
    r.message = QStringLiteral("Folder created");
    return r;
}

SshResult SshSession::renameEntry(const QString &path, const QString &newName)
{
    if (!isOpen()) return transferFail(QStringLiteral("La session SSH n'est pas ouverte."));
    const auto source = cleanAbsolutePath(path);
    if (!source || *source == "/" || !validName(newName.trimmed())) return transferFail(QStringLiteral("Invalid name."));
    const auto target = safeChildPath(parentPath(*source), newName.trimmed());
    if (!target) return transferFail(QStringLiteral("Invalid name."));
    if (*target == *source) return transferFail(QStringLiteral("The name is unchanged."));
    const QByteArray from = source->toUtf8(), to = target->toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES a{};
    if (libssh2_sftp_stat_ex(d->sftp, to.constData(), static_cast<unsigned int>(to.size()), LIBSSH2_SFTP_LSTAT, &a) == 0)
        return transferFail(QStringLiteral("« %1 » already exists.").arg(newName.trimmed()), QStringLiteral("conflict"));
    // SFTP v3 rename never replaces an existing name.
    if (sftpMutation(d->handle, "rename", [&] { return libssh2_sftp_rename_ex(d->sftp, from.constData(), static_cast<unsigned int>(from.size()),
                               to.constData(), static_cast<unsigned int>(to.size()), 0); }) != 0) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->closeLost();
            return transferFail(QStringLiteral("Connexion perdue ; vérifiez le dossier avant de réessayer."), QStringLiteral("unknown"));
        }
        return transferFail(QStringLiteral("Cannot rename (permission error or name taken meanwhile)."));
    }
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = *target;
    r.message = QStringLiteral("Renamed");
    return r;
}
}  // namespace fortiq
