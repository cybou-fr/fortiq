#include "SshInternal.h"

namespace fortiq {
namespace {
constexpr qint64 kEditLimit = 4 * 1024 * 1024;
QByteArray sha256(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
}

SshResult SshSession::saveFile(const QString &path, const QByteArray &data, const QByteArray &expectedSha256, bool force)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    const auto requested = cleanAbsolutePath(path);
    if (!requested || data.size() > kEditLimit) return transferFail(QStringLiteral("Invalid file or content too large."));

    // Save through a symlink to its target instead of replacing the link.
    QString target = *requested;
    {
        const QByteArray p = target.toUtf8();
        char resolved[4096];
        const int n = libssh2_sftp_symlink_ex(d->sftp, p.constData(), static_cast<unsigned int>(p.size()),
                                              resolved, sizeof(resolved), LIBSSH2_SFTP_REALPATH);
        if (n > 0) {
            const auto clean = cleanAbsolutePath(QString::fromUtf8(resolved, n));
            if (clean) target = *clean;
        }
    }
    const QByteArray targetUtf8 = target.toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES original{};
    if (libssh2_sftp_stat_ex(d->sftp, targetUtf8.constData(), static_cast<unsigned int>(targetUtf8.size()),
                             LIBSSH2_SFTP_LSTAT, &original) != 0)
        return transferFail(QStringLiteral("The file no longer exists on the server."), QStringLiteral("conflict"));
    if (!(original.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) || !LIBSSH2_SFTP_S_ISREG(original.permissions))
        return transferFail(QStringLiteral("Not a regular file or missing permissions metadata."));

    auto currentHash = [&]() -> std::optional<QByteArray> {
        const SshResult now = readFile(target, kEditLimit + 1);
        if (now.status != SshResult::Status::Ok || now.truncated) return std::nullopt;
        return sha256(now.data);
    };
    if (!force) {
        const auto hash = currentHash();
        if (!hash) return transferFail(QStringLiteral("Cannot re-read the file to check for changes."));
        if (*hash != expectedSha256)
            return transferFail(QStringLiteral("The file was changed on the server since it was opened."), QStringLiteral("conflict"));
    }

    // Exclusive temporary in the same folder (same filesystem => atomic rename).
    const QString dir = parentPath(target);
    const QString name = target.section('/', -1);
    const QString tempPath = *safeChildPath(dir, QStringLiteral(".%1.fortiq-save-%2").arg(name.left(80), randomTag()));
    const QByteArray tempUtf8 = tempPath.toUtf8();
    LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open_ex(d->sftp, tempUtf8.constData(),
        static_cast<unsigned int>(tempUtf8.size()), LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL,
        0600, LIBSSH2_SFTP_OPENFILE);
    if (!file) return transferFail(QStringLiteral("Cannot write in this folder (a safe save needs write access to the folder)."));
    auto removeTemp = [this, &tempUtf8] {
        if (!d->sftp) return;
        libssh2_sftp_unlink_ex(d->sftp, tempUtf8.constData(), static_cast<unsigned int>(tempUtf8.size()));
    };

    LIBSSH2_SFTP_ATTRIBUTES mine{};
    if (libssh2_sftp_fstat_ex(file, &mine, 0) != 0 || !(mine.flags & LIBSSH2_SFTP_ATTR_UIDGID)
        || !(original.flags & LIBSSH2_SFTP_ATTR_UIDGID) || mine.uid != original.uid) {
        libssh2_sftp_close(file);
        removeTemp();
        return transferFail(QStringLiteral("This file belongs to another user; saving it safely would change its owner. Not saved."));
    }
    qint64 off = 0;
    while (off < data.size()) {
        const auto w = libssh2_sftp_write(file, data.constData() + off, static_cast<size_t>(data.size() - off));
        if (w <= 0) {
            const bool lost = libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL;
            libssh2_sftp_close(file);
            if (lost) {
                d->close();
                return transferFail(QStringLiteral("Connection lost while saving; the original file was not changed."));
            }
            removeTemp();
            return transferFail(QStringLiteral("Write failed (quota or disk full?); the original file was not changed."));
        }
        off += w;
    }
    // Restore and verify metadata after writing; do not silently widen rights
    // or change group when the server refuses fsetstat.
    bool metadataOk = true;
    if (mine.gid != original.gid) {
        LIBSSH2_SFTP_ATTRIBUTES group{};
        group.flags = LIBSSH2_SFTP_ATTR_UIDGID;
        group.uid = mine.uid; group.gid = original.gid;
        metadataOk = setFileAttributes(file, &group) == 0;
    }
    LIBSSH2_SFTP_ATTRIBUTES keep{};
    keep.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    keep.permissions = original.permissions & 07777;
    metadataOk = metadataOk && setFileAttributes(file, &keep) == 0;
    const bool flushed = flushFile(d->sftp, file);
    LIBSSH2_SFTP_ATTRIBUTES written{};
    const unsigned long required = LIBSSH2_SFTP_ATTR_SIZE | LIBSSH2_SFTP_ATTR_PERMISSIONS | LIBSSH2_SFTP_ATTR_UIDGID;
    const bool verified = libssh2_sftp_fstat_ex(file, &written, 0) == 0 && (written.flags & required) == required
        && static_cast<qint64>(written.filesize) == data.size()
        && (written.permissions & 07777) == (original.permissions & 07777)
        && written.uid == original.uid && written.gid == original.gid;
    const bool closed = closeStagingFile(file) == 0;
    if (!metadataOk || !flushed || !verified || !closed) {
        removeTemp();
        return transferFail(QStringLiteral("Staging size, permissions, owner/group, flush or close verification failed; the original file was not changed."));
    }
#ifdef FORTIQ_PUBLICATION_TESTS
    if (publication_test::staged) publication_test::staged();
#endif

    // Re-check right before publishing: someone may have saved meanwhile.
    if (!force) {
        const auto hash = currentHash();
        if (!hash || *hash != expectedSha256) {
            removeTemp();
            return transferFail(QStringLiteral("The file was changed on the server during the save."), QStringLiteral("conflict"));
        }
    }
    LIBSSH2_SFTP_ATTRIBUTES latest{};
    const unsigned long identityFlags = LIBSSH2_SFTP_ATTR_PERMISSIONS | LIBSSH2_SFTP_ATTR_UIDGID;
    if (!d->sftp || libssh2_sftp_stat_ex(d->sftp, targetUtf8.constData(), static_cast<unsigned int>(targetUtf8.size()),
                                      LIBSSH2_SFTP_LSTAT, &latest) != 0
        || (latest.flags & identityFlags) != identityFlags || !LIBSSH2_SFTP_S_ISREG(latest.permissions)
        || (latest.permissions & 07777) != (original.permissions & 07777)
        || latest.uid != original.uid || latest.gid != original.gid) {
        removeTemp();
        return transferFail(QStringLiteral("The destination type, permissions or owner/group changed during the save."), QStringLiteral("conflict"));
    }
    const int published = replaceFile(d->handle, d->sftp, tempUtf8, targetUtf8);
    if (published != 0) {
        if (published != LIBSSH2_ERROR_METHOD_NOT_SUPPORTED
            && libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->close();
            return transferFail(QStringLiteral("Connection lost while publishing; reopen the file to see which version is on the server."),
                                QStringLiteral("unknown"));
        }
        removeTemp();
        // No non-atomic fallback that could leave the file half-replaced.
        return transferFail(QStringLiteral("Atomic replacement was refused (unsupported extension or permissions); the original file was not changed."));
    }
    const SshResult back = readFile(target, kEditLimit + 1);
    if (back.status != SshResult::Status::Ok || back.truncated || back.fileSize != data.size() || sha256(back.data) != sha256(data))
        return transferFail(QStringLiteral("Saved, but the read-back check differs; reopen the file."), QStringLiteral("unknown"));
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = target;
    r.data = sha256(data);
    r.bytes = data.size();
    r.message = QStringLiteral("Saved");
    return r;
}

SshResult SshSession::saveCopy(const QString &dirPath, const QString &name, const QByteArray &data)
{
    if (!isOpen()) return transferFail(QStringLiteral("SSH session is not open."));
    const auto dir = cleanAbsolutePath(dirPath);
    if (!dir || !safeChildPath(*dir, name) || data.size() > kEditLimit) return transferFail(QStringLiteral("Invalid destination."));
    auto exists = [this](const QString &p) {
        const QByteArray b = p.toUtf8();
        LIBSSH2_SFTP_ATTRIBUTES a{};
        return libssh2_sftp_stat_ex(d->sftp, b.constData(), static_cast<unsigned int>(b.size()), LIBSSH2_SFTP_LSTAT, &a) == 0;
    };
    QString target = *safeChildPath(*dir, name);
    for (int n = 2; n < 1000 && exists(target); ++n) target = *safeChildPath(*dir, numberedName(name, n));
    const QByteArray t = target.toUtf8();
    // Written to an exclusive temporary, then published by a rename that
    // never overwrites: a broken connection never leaves a half copy.
    const QByteArray temp = safeChildPath(*dir, QStringLiteral(".%1.fortiq-save-%2").arg(name.left(80), randomTag()))->toUtf8();
    LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open_ex(d->sftp, temp.constData(), static_cast<unsigned int>(temp.size()),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL, 0644, LIBSSH2_SFTP_OPENFILE);
    if (!file) return transferFail(QStringLiteral("Cannot create the copy (permission error)."));
    auto dropTemp = [this, &temp] { libssh2_sftp_unlink_ex(d->sftp, temp.constData(), static_cast<unsigned int>(temp.size())); };
    qint64 off = 0;
    while (off < data.size()) {
        const auto w = libssh2_sftp_write(file, data.constData() + off, static_cast<size_t>(data.size() - off));
        if (w < 0) {
            libssh2_sftp_close(file);
            dropTemp();
            return transferFail(QStringLiteral("Write failed; no copy was kept."));
        }
        off += w;
    }
    libssh2_sftp_close(file);
    if (libssh2_sftp_rename_ex(d->sftp, temp.constData(), static_cast<unsigned int>(temp.size()),
                               t.constData(), static_cast<unsigned int>(t.size()), 0) != 0) {
        dropTemp();
        return transferFail(QStringLiteral("The copy name was taken meanwhile; nothing was written."), QStringLiteral("conflict"));
    }
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = target;
    r.data = sha256(data);
    r.bytes = data.size();
    r.message = QStringLiteral("Copy saved");
    return r;
}
}  // namespace fortiq
