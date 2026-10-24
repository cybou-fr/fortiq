#include "SshInternal.h"

namespace fortiq {
SshResult SshSession::download(const QString &remotePath, const QString &localPath, const TransferProgress &progress)
{
    if (!isOpen()) return transferFail(QStringLiteral("La session SSH n'est pas ouverte."));
    const auto clean = cleanAbsolutePath(remotePath);
    if (!clean || localPath.isEmpty()) return transferFail(QStringLiteral("Invalid transfer paths."));
    const QByteArray pathUtf8 = clean->toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES attr{};
    int statResult;
#ifdef FORTIQ_PUBLICATION_TESTS
    if (publication_test::downloadStat)
        statResult = publication_test::downloadStat(d->handle, d->sftp, pathUtf8, &attr);
    else
#endif
        statResult = libssh2_sftp_stat_ex(d->sftp, pathUtf8.constData(), static_cast<unsigned int>(pathUtf8.size()),
                                         LIBSSH2_SFTP_STAT, &attr);
    if (statResult != 0) {
        if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) {
            d->closeLost();
            return transferFail(QStringLiteral("Connexion SSH perdue avant le début du téléchargement."), QStringLiteral("disconnected"));
        }
        return transferFail(QStringLiteral("Cannot read remote file (permission or path error; SFTP %1).")
                            .arg(libssh2_sftp_last_error(d->sftp)));
    }
    if ((attr.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && !LIBSSH2_SFTP_S_ISREG(attr.permissions))
        return transferFail(QStringLiteral("Only regular files can be downloaded."));
    const qint64 total = (attr.flags & LIBSSH2_SFTP_ATTR_SIZE) ? static_cast<qint64>(attr.filesize) : -1;

    LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open_ex(d->sftp, pathUtf8.constData(),
        static_cast<unsigned int>(pathUtf8.size()), LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE);
    if (!file) return transferFail(QStringLiteral("Cannot open remote file (permission error)."));

    const bool document = isContentDocument(localPath);
    QSaveFile native(localPath);
    QTemporaryFile staging;
    if (document) {
        const QString pathTemplate = documentStagingTemplate();
        if (pathTemplate.isEmpty()) {
            libssh2_sftp_close(file);
            return transferFail(QStringLiteral("Cannot prepare private download storage."));
        }
        staging.setFileTemplate(pathTemplate);
    }
    QFileDevice &out = document ? static_cast<QFileDevice &>(staging) : static_cast<QFileDevice &>(native);
    if (!(document ? staging.open() : native.open(QIODevice::WriteOnly))) {
        libssh2_sftp_close(file);
        return transferFail(QStringLiteral("Cannot create a private download staging file."));
    }
    auto cancelLocal = [&] { if (!document) native.cancelWriting(); };
    std::vector<char> buffer(256 * 1024);
    qint64 done = 0;
    while (true) {
        const auto n = libssh2_sftp_read(file, buffer.data(), buffer.size());
        if (n == 0) break;
        if (n < 0) {
            libssh2_sftp_close(file);
            cancelLocal();
            if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL) d->closeLost();
            return transferFail(QStringLiteral("Download interrupted; the local file was not created."));
        }
        if (out.write(buffer.data(), n) != n) {
            libssh2_sftp_close(file);
            cancelLocal();
            return transferFail(QStringLiteral("Local write failed (disk full?)."));
        }
        done += n;
        if (progress && !progress(done, total)) {
            libssh2_sftp_close(file);
            cancelLocal();
            return transferFail(QStringLiteral("Cancelled; the local file was not created."), QStringLiteral("cancelled"));
        }
    }
    libssh2_sftp_close(file);
    if (total >= 0 && done != total) {
        cancelLocal();
        return transferFail(QStringLiteral("Size mismatch (%1 of %2 bytes); the local file was not created.").arg(done).arg(total));
    }
    if (document) {
        if (!staging.flush()) return transferFail(QStringLiteral("Download staging flush failed; the selected document was not opened."));
        const auto result = exportDocument(staging, [&]() -> std::unique_ptr<QIODevice> {
            auto destination = std::make_unique<QFile>(localPath);
            if (!destination->open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
            return destination;
        }, [&] { return !progress || progress(done, done); });
        if (!result.ok) return transferFail(result.attempted
            ? QStringLiteral("Document export did not finish. The selected document may contain partial data; check it before retrying.")
            : QStringLiteral("Download was not exported; the selected document was not opened."),
            result.attempted ? QStringLiteral("unknown") : (result.cancelled ? QStringLiteral("cancelled") : QString()));
    } else if (!native.commit()) return transferFail(QStringLiteral("Cannot finalise the local file: %1").arg(native.errorString()));
    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = localPath;
    r.bytes = done;
    r.message = QStringLiteral("Downloaded");
    return r;
}

SshResult SshSession::upload(const QString &localPath, const QString &remoteDir, const QString &name,
                             ConflictPolicy policy, const TransferProgress &progress)
{
    if (!isOpen()) return transferFail(QStringLiteral("La session SSH n'est pas ouverte."));
    const auto dir = cleanAbsolutePath(remoteDir);
    const auto target = dir ? safeChildPath(*dir, name) : std::nullopt;
    if (!target) return transferFail(QStringLiteral("Invalid remote destination."));

    QFile in(localPath);
    if (!in.open(QIODevice::ReadOnly)) return transferFail(QStringLiteral("Cannot read the local file: %1").arg(in.errorString()));
    if (!isContentDocument(localPath) && !QFileInfo(localPath).isFile()) return transferFail(QStringLiteral("Only regular files can be uploaded."));
    const qint64 size = in.size();
    const qint64 total = isContentDocument(localPath) && (size <= 0 || in.isSequential()) ? -1 : size;

    auto exists = [this](const QString &path) {
        const QByteArray p = path.toUtf8();
        LIBSSH2_SFTP_ATTRIBUTES a{};
        return libssh2_sftp_stat_ex(d->sftp, p.constData(), static_cast<unsigned int>(p.size()), LIBSSH2_SFTP_LSTAT, &a) == 0;
    };
    QString finalPath = *target;
    std::optional<LIBSSH2_SFTP_ATTRIBUTES> replacement;
    if (policy == ConflictPolicy::Replace) {
        const QByteArray p = finalPath.toUtf8();
        LIBSSH2_SFTP_ATTRIBUTES attr{};
        if (libssh2_sftp_stat_ex(d->sftp, p.constData(), static_cast<unsigned int>(p.size()), LIBSSH2_SFTP_LSTAT, &attr) == 0) {
            const unsigned long needed = LIBSSH2_SFTP_ATTR_PERMISSIONS | LIBSSH2_SFTP_ATTR_UIDGID;
            if ((attr.flags & needed) != needed || !LIBSSH2_SFTP_S_ISREG(attr.permissions))
                return transferFail(QStringLiteral("Only a regular file with known permissions and owner can be replaced."));
            replacement = attr;
        } else if (libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL
                   || libssh2_sftp_last_error(d->sftp) != LIBSSH2_FX_NO_SUCH_FILE) {
            return transferFail(QStringLiteral("Cannot inspect the destination; replacement was not attempted."));
        }
    }
    if (exists(finalPath)) {
        if (policy == ConflictPolicy::Fail)
            return transferFail(QStringLiteral("« %1 » already exists in this folder.").arg(name), QStringLiteral("conflict"));
        if (policy == ConflictPolicy::KeepBoth) {
            for (int n = 2; n < 1000 && exists(finalPath); ++n) finalPath = *safeChildPath(*dir, numberedName(name, n));
            if (exists(finalPath)) return transferFail(QStringLiteral("No free name for this upload."), QStringLiteral("conflict"));
        }
    }

    // Unique, exclusively created temporary owned by this transfer.
    const QString tempPath = *safeChildPath(*dir, QStringLiteral(".%1.fortiq-upload-%2").arg(name.left(80), randomTag()));
    const QByteArray tempUtf8 = tempPath.toUtf8();
    LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open_ex(d->sftp, tempUtf8.constData(),
        static_cast<unsigned int>(tempUtf8.size()), LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL,
        0644, LIBSSH2_SFTP_OPENFILE);
    if (!file) return transferFail(QStringLiteral("Cannot create a file in this folder (permission error?)."));
    auto removeTemp = [this, &tempUtf8] {
        if (!d->sftp) return;
        libssh2_sftp_unlink_ex(d->sftp, tempUtf8.constData(), static_cast<unsigned int>(tempUtf8.size()));
    };

    // Capture the server's creation mode (including its umask) while empty,
    // then restrict the staging file before writing any source bytes.
    LIBSSH2_SFTP_ATTRIBUTES created{};
    const bool creationKnown = libssh2_sftp_fstat_ex(file, &created, 0) == 0
        && (created.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS);
    LIBSSH2_SFTP_ATTRIBUTES privateMode{};
    privateMode.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    privateMode.permissions = 0600;
    const bool restricted = creationKnown && setFileAttributes(file, &privateMode) == 0;
    LIBSSH2_SFTP_ATTRIBUTES secured{};
    const bool privateVerified = restricted && libssh2_sftp_fstat_ex(file, &secured, 0) == 0
        && (secured.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && (secured.permissions & 07777) == 0600;
    if (!privateVerified) {
        libssh2_sftp_close(file); removeTemp();
        return transferFail(QStringLiteral("Cannot secure the empty staging file; no content was uploaded."));
    }

    std::vector<char> buffer(256 * 1024);
    qint64 done = 0;
    while (true) {
        const qint64 n = in.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (n < 0) { libssh2_sftp_close(file); removeTemp(); return transferFail(QStringLiteral("Local read failed.")); }
        if (n == 0) break;
        qint64 off = 0;
        while (off < n) {
            const auto w = libssh2_sftp_write(file, buffer.data() + off, static_cast<size_t>(n - off));
            if (w <= 0) {
                const bool lost = libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL;
                libssh2_sftp_close(file);
                if (lost) {
                    d->closeLost();
                    // Nothing was published; only the owned temporary may remain.
                    return transferFail(QStringLiteral("Connexion perdue pendant l'envoi ; rien n'a été publié (un fichier temporaire caché %1 peut subsister).")
                                            .arg(tempPath.section('/', -1)));
                }
                removeTemp();
                return transferFail(QStringLiteral("Remote write failed (quota or disk full?)."));
            }
            off += w;
        }
        done += n;
        if (progress && !progress(done, total)) {
            libssh2_sftp_close(file);
            removeTemp();
            return transferFail(QStringLiteral("Cancelled before publication; nothing changed on the server."), QStringLiteral("cancelled"));
        }
    }
    LIBSSH2_SFTP_ATTRIBUTES mine{};
    const bool mineOk = libssh2_sftp_fstat_ex(file, &mine, 0) == 0 && (mine.flags & LIBSSH2_SFTP_ATTR_UIDGID);
    bool metadataOk = mineOk && (!replacement || mine.uid == replacement->uid);
    if (metadataOk && replacement && mine.gid != replacement->gid) {
        LIBSSH2_SFTP_ATTRIBUTES group{};
        group.flags = LIBSSH2_SFTP_ATTR_UIDGID;
        group.uid = mine.uid; group.gid = replacement->gid;
        metadataOk = setFileAttributes(file, &group) == 0;
    }
    LIBSSH2_SFTP_ATTRIBUTES mode{};
    mode.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    mode.permissions = replacement ? replacement->permissions & 07777 : created.permissions & 0777;
    metadataOk = metadataOk && setFileAttributes(file, &mode) == 0;
    const bool flushed = flushFile(d->sftp, file);
    LIBSSH2_SFTP_ATTRIBUTES written{};
    const bool statOk = libssh2_sftp_fstat_ex(file, &written, 0) == 0;
    const bool closed = closeStagingFile(file) == 0;
    const unsigned long required = LIBSSH2_SFTP_ATTR_SIZE | LIBSSH2_SFTP_ATTR_PERMISSIONS | LIBSSH2_SFTP_ATTR_UIDGID;
    const bool metadataVerified = (written.flags & required) == required && (written.permissions & 07777) == mode.permissions
        && written.uid == (replacement ? replacement->uid : mine.uid)
        && written.gid == (replacement ? replacement->gid : mine.gid);
    if (!metadataOk || !metadataVerified || !flushed || !closed || (total >= 0 && done != total) || !statOk || static_cast<qint64>(written.filesize) != done) {
        removeTemp();
        return transferFail(QStringLiteral("Staging size, metadata, flush or close verification failed; nothing was published."));
    }
    if (progress && !progress(done, done)) {
        removeTemp();
        return transferFail(QStringLiteral("Cancelled before publication; the destination was not changed."), QStringLiteral("cancelled"));
    }

    // Replace is one POSIX rename: the old pathname stays visible until
    // publication. Fail/KeepBoth retain SFTP v3's no-overwrite semantics.
    const QByteArray finalUtf8 = finalPath.toUtf8();
#ifdef FORTIQ_PUBLICATION_TESTS
    if (publication_test::staged) publication_test::staged();
#endif
    if (replacement) {
        LIBSSH2_SFTP_ATTRIBUTES latest{};
        const unsigned long flags = LIBSSH2_SFTP_ATTR_PERMISSIONS | LIBSSH2_SFTP_ATTR_UIDGID;
        if (libssh2_sftp_stat_ex(d->sftp, finalUtf8.constData(), static_cast<unsigned int>(finalUtf8.size()),
                               LIBSSH2_SFTP_LSTAT, &latest) != 0
            || (latest.flags & flags) != flags || !LIBSSH2_SFTP_S_ISREG(latest.permissions)
            || (latest.permissions & 07777) != (replacement->permissions & 07777)
            || latest.uid != replacement->uid || latest.gid != replacement->gid) {
            removeTemp();
            return transferFail(QStringLiteral("Destination type, permissions or owner/group changed during upload."), QStringLiteral("conflict"));
        }
    }
    const int published = replacement
        ? replaceFile(d->handle, d->sftp, tempUtf8, finalUtf8)
        : libssh2_sftp_rename_ex(d->sftp, tempUtf8.constData(), static_cast<unsigned int>(tempUtf8.size()),
                                finalUtf8.constData(), static_cast<unsigned int>(finalUtf8.size()), 0);
    if (published != 0) {
        if (!isOpen() || (published != LIBSSH2_ERROR_METHOD_NOT_SUPPORTED
            && libssh2_session_last_errno(d->handle) != LIBSSH2_ERROR_SFTP_PROTOCOL)) {
            d->closeLost();
            return transferFail(QStringLiteral("Connexion perdue pendant la publication ; vérifiez le dossier avant de réessayer."), QStringLiteral("unknown"));
        }
        removeTemp();
        if (replacement)
            return transferFail(QStringLiteral("Atomic replacement was refused (unsupported extension or permissions); the original was not moved or removed."));
        return transferFail(QStringLiteral("Cannot publish « %1 » (name taken or permission denied); nothing was replaced.").arg(finalPath.section('/', -1)),
                            QStringLiteral("conflict"));
    }

    SshResult r;
    r.status = SshResult::Status::Ok;
    r.directory = finalPath;
    r.bytes = done;
    r.message = QStringLiteral("Uploaded");
    return r;
}
}  // namespace fortiq
