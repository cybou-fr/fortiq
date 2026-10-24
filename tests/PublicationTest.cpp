#include "ssh/SshClient.h"
#include "PublicationHooks.h"
#include <QtTest>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QUuid>

using namespace fortiq;
class PublicationTest : public QObject {
    Q_OBJECT
    ConnectionRequest request;
    SshSession observer;
    QTemporaryDir local;
    QString dir, source;
    static QByteArray hash(const QByteArray &data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256); }
    SshResult command(const QByteArray &script) { return observer.runScript(script, 4096); }
    bool put(const QString &name, const QByteArray &bytes) {
        return command("printf %s " + SshSession::shellQuote(QString::fromUtf8(bytes)) + " > "
                       + SshSession::shellQuote(dir + "/" + name) + "\n").code == "exit:0";
    }
    QByteArray read(const QString &name) { return observer.readFile(dir + "/" + name, 4 * 1024 * 1024).data; }
private slots:
    void initTestCase() {
        if (qEnvironmentVariableIsEmpty("FORTIQ_PUBLICATION_HOST")) QSKIP("Disposable SSH fixture not configured");
        request.host = qEnvironmentVariable("FORTIQ_PUBLICATION_HOST");
        request.port = qEnvironmentVariableIntValue("FORTIQ_PUBLICATION_PORT");
        request.user = qEnvironmentVariable("FORTIQ_PUBLICATION_USER");
        request.keyPath = qEnvironmentVariable("FORTIQ_PUBLICATION_KEY");
        request.expectedHostFingerprint = qEnvironmentVariable("FORTIQ_PUBLICATION_FINGERPRINT");
        const auto opened = observer.open(request);
        QVERIFY2(opened.status == SshResult::Status::Ok, qPrintable(opened.message));
        dir = observer.homeDirectory() + "/.fortiq-publication-" + QUuid::createUuid().toString(QUuid::Id128);
        QCOMPARE(command("mkdir -m 700 -- " + SshSession::shellQuote(dir)).code, QString("exit:0"));
        QVERIFY(local.isValid());
        source = local.filePath("upload.txt");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("new content\n"), 12);
    }
    void cleanup() {
        publication_test::replace = {}; publication_test::staged = {};
        publication_test::attributes = {}; publication_test::flush = {}; publication_test::close = {};
        publication_test::downloadStat = {}; publication_test::fault = {};
    }
    void cleanupTestCase() {
        if (!dir.isEmpty()) QCOMPARE(command("rm -rf -- " + SshSession::shellQuote(dir)).code, QString("exit:0"));
    }
    void atomicReplacementPreservesNameAndMode() {
        SshSession writer;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        const QString name = QString::fromUtf8("été ' résumé.txt");
        QVERIFY(put(name, "old content\n"));
        QCOMPARE(command("chmod 600 -- " + SshSession::shellQuote(dir + "/" + name)).code, QString("exit:0"));
        int calls = 0;
        bool oldVisible = false, newVisible = false;
        publication_test::replace = [&](auto *, auto *sftp, const QByteArray &from, const QByteArray &to) {
            ++calls;
            oldVisible = read(name) == QByteArray("old content\n");
            const int rc = libssh2_sftp_posix_rename_ex(sftp, from.constData(), from.size(), to.constData(), to.size());
            if (rc == 0) newVisible = read(name) == QByteArray("new content\n");
            return rc;
        };
        auto r = writer.upload(source, dir, name, ConflictPolicy::Replace, {});
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QCOMPARE(calls, 1);
        QVERIFY(oldVisible && newVisible);
        QCOMPARE(command("stat -c %a -- " + SshSession::shellQuote(dir + "/" + name)).data.trimmed(), QByteArray("600"));
        publication_test::replace = {};
        r = writer.saveFile(dir + "/" + name, "edited\n", hash("new content\n"), false);
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QCOMPARE(read(name), QByteArray("edited\n"));
        QCOMPARE(command("stat -c %a -- " + SshSession::shellQuote(dir + "/" + name)).data.trimmed(), QByteArray("600"));
    }
    void downloadPreflightKeepsDestination() {
        SshSession reader;
        QVERIFY(reader.open(request).status == SshResult::Status::Ok);
        const QString destination = local.filePath("download-existing.txt");
        { QFile f(destination); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write("keep me"), 7); }
        auto r = reader.download(dir + "/missing.txt", destination, {});
        QCOMPARE(r.status, SshResult::Status::Error);
        QVERIFY(r.code != "disconnected");
        QVERIFY(reader.isOpen()); // a missing file is not a broken transport
        publication_test::downloadStat = [](auto *session, auto *, const auto &, auto *) {
            libssh2_session_set_last_error(session, LIBSSH2_ERROR_SOCKET_RECV, "injected idle transport loss");
            return -1;
        };
        r = reader.download(dir + "/missing.txt", destination, {});
        QCOMPARE(r.code, QString("disconnected"));
        QVERIFY(!reader.isOpen());
        QFile f(destination); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), QByteArray("keep me"));
    }
    void publicationFailure_data() {
        QTest::addColumn<bool>("save");
        QTest::addColumn<int>("kind"); // protocol refusal, lost request, lost ack, unsupported method
        for (bool save : {false, true}) for (int kind = 0; kind < 4; ++kind)
            QTest::newRow(qPrintable(QString("%1-%2").arg(save ? "save" : "upload").arg(kind))) << save << kind;
    }
    void publicationFailure() {
        QFETCH(bool, save); QFETCH(int, kind);
        const QString name = "failure.txt";
        QVERIFY(put(name, "original\n"));
        SshSession writer;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        const auto stagingBefore = command("find " + SshSession::shellQuote(dir) + " -maxdepth 1 -name '*.fortiq-*' -print").data;
        int calls = 0;
        publication_test::replace = [&](auto *session, auto *sftp, const QByteArray &from, const QByteArray &to) {
            ++calls;
            if (kind == 3) {
                libssh2_session_set_last_error(session, LIBSSH2_ERROR_METHOD_NOT_SUPPORTED, "injected unsupported extension");
                return LIBSSH2_ERROR_METHOD_NOT_SUPPORTED;
            }
            if (kind == 2) {
                const int rc = libssh2_sftp_posix_rename_ex(sftp, from.constData(), from.size(), to.constData(), to.size());
                if (rc != 0) return rc;
            }
            libssh2_session_set_last_error(session, kind == 0 ? LIBSSH2_ERROR_SFTP_PROTOCOL : LIBSSH2_ERROR_SOCKET_RECV, "injected publication failure");
            return -1;
        };
        const auto r = save ? writer.saveFile(dir + "/" + name, "new content\n", hash("original\n"), false)
                            : writer.upload(source, dir, name, ConflictPolicy::Replace, {});
        QCOMPARE(r.status, SshResult::Status::Error);
        QCOMPARE(calls, 1); // never retry or fall back to moving the original away
        QCOMPARE(r.code, (kind == 0 || kind == 3) ? QString() : QString("unknown"));
        QCOMPARE(read(name), kind == 2 ? QByteArray("new content\n") : QByteArray("original\n"));
        if (kind == 0 || kind == 3)
            QCOMPARE(command("find " + SshSession::shellQuote(dir) + " -maxdepth 1 -name '*.fortiq-*' -print").data, stagingBefore);
    }
    void createAndKeepBoth() {
        SshSession writer;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        auto r = writer.upload(source, dir, "new.txt", ConflictPolicy::Fail, {});
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        r = writer.upload(source, dir, "new.txt", ConflictPolicy::Fail, {});
        QCOMPARE(r.code, QString("conflict"));
        r = writer.upload(source, dir, "new.txt", ConflictPolicy::KeepBoth, {});
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QVERIFY(r.directory.endsWith("new (2).txt"));
        QCOMPARE(read("new.txt"), QByteArray("new content\n"));
        QCOMPARE(read("new (2).txt"), QByteArray("new content\n"));
        r = writer.upload(source, dir, "absent.txt", ConflictPolicy::Replace, {});
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QCOMPARE(read("absent.txt"), QByteArray("new content\n"));
    }
    void stagingFailure_data() {
        QTest::addColumn<bool>("save");
        QTest::addColumn<int>("kind");
        for (bool save : {false, true}) for (int kind = 0; kind < 4; ++kind)
            QTest::newRow(qPrintable(QString("%1-%2").arg(save ? "save" : "upload").arg(kind))) << save << kind;
    }
    void stagingFailure() {
        QFETCH(bool, save); QFETCH(int, kind);
        const QString name = "staging.txt";
        QVERIFY(put(name, "original\n"));
        QCOMPARE(command("chmod 640 -- " + SshSession::shellQuote(dir + "/" + name)).code, QString("exit:0"));
        SshSession writer;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        const auto before = command("find " + SshSession::shellQuote(dir) + " -maxdepth 1 -name '*.fortiq-*' -print").data;
        // A server can reject metadata or claim success without applying it.
        if (kind < 2) publication_test::attributes = [kind](auto *, auto *) { return kind == 0 ? -1 : 0; };
        if (kind == 2) publication_test::flush = [](auto *) { return LIBSSH2_ERROR_SOCKET_RECV; };
        if (kind == 3) publication_test::close = [](auto *file) { libssh2_sftp_close(file); return -1; };
        int publications = 0;
        publication_test::replace = [&](auto *, auto *, const auto &, const auto &) { ++publications; return -1; };
        const auto r = save ? writer.saveFile(dir + "/" + name, "new content\n", hash("original\n"), false)
                            : writer.upload(source, dir, name, ConflictPolicy::Replace, {});
        QCOMPARE(r.status, SshResult::Status::Error);
        QVERIFY(r.code != "unknown"); // publication has not been attempted
        QCOMPARE(publications, 0);
        QCOMPARE(read(name), QByteArray("original\n"));
        QCOMPARE(command("find " + SshSession::shellQuote(dir) + " -maxdepth 1 -name '*.fortiq-*' -print").data, before);
    }
    void readBackMismatchIsUnknown() {
        QVERIFY(put("readback.txt", "original\n"));
        SshSession writer;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        bool changed = false;
        publication_test::replace = [&](auto *, auto *sftp, const QByteArray &from, const QByteArray &to) {
            const int rc = libssh2_sftp_posix_rename_ex(sftp, from.constData(), from.size(), to.constData(), to.size());
            if (rc == 0) changed = put("readback.txt", "another writer\n");
            return rc;
        };
        const auto r = writer.saveFile(dir + "/readback.txt", "my edit\n", hash("original\n"), false);
        QVERIFY(changed);
        QCOMPARE(r.code, QString("unknown"));
        QCOMPARE(read("readback.txt"), QByteArray("another writer\n"));
    }
    void concurrentEditDuringStaging() {
        QVERIFY(put("concurrent.txt", "original\n"));
        SshSession writer;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        bool changed = false;
        publication_test::staged = [&] { changed = put("concurrent.txt", "external writer\n"); };
        const auto r = writer.saveFile(dir + "/concurrent.txt", "my edit\n", hash("original\n"), false);
        QVERIFY(changed);
        QCOMPARE(r.code, QString("conflict"));
        QCOMPARE(read("concurrent.txt"), QByteArray("external writer\n"));
    }
    void concurrentPermissionChangeIsNotUndone() {
        QVERIFY(put("permissions.txt", "original\n"));
        QCOMPARE(command("chmod 640 -- " + SshSession::shellQuote(dir + "/permissions.txt")).code, QString("exit:0"));
        SshSession writer;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        bool changed = false;
        publication_test::staged = [&] { changed = command("chmod 600 -- " + SshSession::shellQuote(dir + "/permissions.txt")).code == "exit:0"; };
        const auto r = writer.saveFile(dir + "/permissions.txt", "my edit\n", hash("original\n"), false);
        QVERIFY(changed);
        QCOMPARE(r.code, QString("conflict"));
        QCOMPARE(read("permissions.txt"), QByteArray("original\n"));
        QCOMPARE(command("stat -c %a -- " + SshSession::shellQuote(dir + "/permissions.txt")).data.trimmed(), QByteArray("600"));
    }
    void unsupportedFlushAndEmptyCancellation() {
        SshSession writer;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        QVERIFY(put("fsync.txt", "original\n"));
        publication_test::flush = [](auto *) { return LIBSSH2_ERROR_METHOD_NOT_SUPPORTED; };
        auto r = writer.saveFile(dir + "/fsync.txt", "edited\n", hash("original\n"), false);
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QCOMPARE(read("fsync.txt"), QByteArray("edited\n"));
        publication_test::flush = {};
        const QString empty = local.filePath("empty.txt");
        { QFile f(empty); QVERIFY(f.open(QIODevice::WriteOnly)); }
        r = writer.upload(empty, dir, "fsync.txt", ConflictPolicy::Replace, [](auto, auto) { return false; });
        QCOMPARE(r.code, QString("cancelled"));
        QCOMPARE(read("fsync.txt"), QByteArray("edited\n"));
    }
    void replaceRefusesSymlinksAndDirectories() {
        SshSession writer;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        QVERIFY(put("linked.txt", "original\n"));
        QCOMPARE(command("ln -s linked.txt " + SshSession::shellQuote(dir + "/link")
                         + " && mkdir " + SshSession::shellQuote(dir + "/directory")).code, QString("exit:0"));
        for (const QString &name : {QString("link"), QString("directory")}) {
            const auto r = writer.upload(source, dir, name, ConflictPolicy::Replace, {});
            QCOMPARE(r.status, SshResult::Status::Error);
        }
        QCOMPARE(read("linked.txt"), QByteArray("original\n"));
        QCOMPARE(read("link"), QByteArray("original\n"));
    }
    void concurrentNewFileIsNotOverwritten() {
        SshSession writer;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        for (const auto policy : {ConflictPolicy::Fail, ConflictPolicy::KeepBoth, ConflictPolicy::Replace}) {
            const QString name = QString("raced-%1.txt").arg(static_cast<int>(policy));
            bool changed = false;
            publication_test::staged = [&] { changed = put(name, "external writer\n"); };
            const auto r = writer.upload(source, dir, name, policy, {});
            QVERIFY(changed);
            QCOMPARE(r.code, QString("conflict"));
            QCOMPARE(read(name), QByteArray("external writer\n"));
        }
    }
    void cancelLeavesOriginalAndNoStagingFile() {
        QVERIFY(put("cancel.txt", "original\n"));
        SshSession writer;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        const auto before = command("find " + SshSession::shellQuote(dir) + " -maxdepth 1 -name '*.fortiq-*' -print").data;
        const auto r = writer.upload(source, dir, "cancel.txt", ConflictPolicy::Replace, [](auto, auto) { return false; });
        QCOMPARE(r.code, QString("cancelled"));
        QCOMPARE(read("cancel.txt"), QByteArray("original\n"));
        QCOMPARE(command("find " + SshSession::shellQuote(dir) + " -maxdepth 1 -name '*.fortiq-*' -print").data, before);
    }

    // S-4: every SFTP mutation, under the three failures a client cannot tell apart from success or from each other:
    // 1 the server refuses, 2 the request is lost, 3 the work is done but the acknowledgement is lost. A refusal
    // is a plain error; a lost link is "unknown" (never "nothing happened" and never retried); the server state
    // matches what the table in docs/STABILIZATION_PLAN.md promises.
    void ackLoss_data() {
        QTest::addColumn<QString>("op");
        QTest::addColumn<int>("mode");
        for (const char *op : { "mkdir", "rename", "copy", "trash", "restore", "delete", "empty" })
            for (int mode = 1; mode <= 3; ++mode)
                QTest::newRow(qPrintable(QString("%1-%2").arg(op).arg(mode))) << QString(op) << mode;
    }
    bool exists(const QString &path) { return command("test -e " + SshSession::shellQuote(path) + "\n").code == "exit:0"; }
    void ackLoss() {
        QFETCH(QString, op); QFETCH(int, mode);
        const QString tag = QUuid::createUuid().toString(QUuid::Id128).left(12);
        const QString trashFiles = observer.homeDirectory() + "/.local/share/Trash/files";
        const auto trashedHere = [&](const QString &stem) {
            return command("ls " + SshSession::shellQuote(trashFiles) + " | grep -c -F -- " + SshSession::shellQuote(stem) + "\n").data.trimmed().toInt();
        };
        SshSession writer;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        int calls = 0;
        const auto arm = [&](const QString &which) {
            publication_test::fault = [&, which](const char *name) { if (which != name) return 0; return ++calls == 1 ? mode : 0; };
        };
        SshResult r;
        const bool lost = mode != 1;
        const QString expectedCode = lost ? QStringLiteral("unknown") : QString();

        if (op == "mkdir") {
            const QString name = "mk-" + tag;
            arm("mkdir");
            r = writer.makeDirectory(dir, name);
            QCOMPARE(r.status, SshResult::Status::Error);
            QCOMPARE(r.code, expectedCode);
            QCOMPARE(exists(dir + "/" + name), mode == 3);
        } else if (op == "rename") {
            const QString from = "rn-" + tag, to = "rn2-" + tag;
            QVERIFY(put(from, "x"));
            arm("rename");
            r = writer.renameEntry(dir + "/" + from, to);
            QCOMPARE(r.status, SshResult::Status::Error);
            QCOMPARE(r.code, expectedCode);
            QCOMPARE(exists(dir + "/" + to), mode == 3);
            QCOMPARE(exists(dir + "/" + from), mode != 3);
        } else if (op == "copy") {
            const QString name = "cp-" + tag;
            arm("copy");
            r = writer.saveCopy(dir, name, "copied\n");
            QCOMPARE(r.status, SshResult::Status::Error);
            // Refusal means the name was taken meanwhile ("conflict"); a lost link is "unknown", never "nothing was written".
            QCOMPARE(r.code, lost ? QStringLiteral("unknown") : QStringLiteral("conflict"));
            QCOMPARE(exists(dir + "/" + name), mode == 3);
            if (mode == 3) QCOMPARE(read(name), QByteArray("copied\n"));
            if (!lost) QCOMPARE(command("ls -A " + SshSession::shellQuote(dir) + " | grep -c -F -- " + SshSession::shellQuote(".cp-" + tag)).data.trimmed(), QByteArray("0"));
        } else if (op == "trash") {
            const QString name = "tr-" + tag;
            QVERIFY(put(name, "x"));
            arm("trash");
            r = writer.moveToTrash(dir + "/" + name);
            QCOMPARE(r.status, SshResult::Status::Error);
            QCOMPARE(r.code, expectedCode);
            QCOMPARE(exists(dir + "/" + name), mode != 3);
            QCOMPARE(trashedHere(name), mode == 3 ? 1 : 0);
            if (mode == 1) // a refusal leaves no reserved .trashinfo behind
                QCOMPARE(command("ls " + SshSession::shellQuote(observer.homeDirectory() + "/.local/share/Trash/info") + " | grep -c -F -- " + SshSession::shellQuote(name)).data.trimmed(), QByteArray("0"));
        } else if (op == "restore") {
            const QString name = "rs-" + tag;
            QVERIFY(put(name, "x"));
            const SshResult trashed = writer.moveToTrash(dir + "/" + name);
            QVERIFY2(trashed.status == SshResult::Status::Ok, qPrintable(trashed.message));
            arm("restore");
            r = writer.restoreFromTrash(trashed.directory, ConflictPolicy::Fail);
            QCOMPARE(r.status, SshResult::Status::Error);
            QCOMPARE(r.code, expectedCode);
            QCOMPARE(exists(dir + "/" + name), mode == 3);
            QCOMPARE(exists(trashed.directory), mode != 3);
        } else if (op == "delete") {
            const QString name = "dl-" + tag;
            QCOMPARE(command("mkdir -- " + SshSession::shellQuote(dir + "/" + name) + " && printf a > " + SshSession::shellQuote(dir + "/" + name + "/a")
                             + " && printf b > " + SshSession::shellQuote(dir + "/" + name + "/b") + "\n").code, QString("exit:0"));
            const SshResult trashed = writer.moveToTrash(dir + "/" + name);
            QVERIFY2(trashed.status == SshResult::Status::Ok, qPrintable(trashed.message));
            arm("unlink");
            r = writer.deleteFromTrash(trashed.directory);
            QCOMPARE(r.status, SshResult::Status::Error);
            QCOMPARE(r.code, expectedCode);
            QCOMPARE(calls, 1); // no retry after the failure
            QVERIFY(exists(trashed.directory)); // the folder itself is only removed after its files
            const int left = command("ls " + SshSession::shellQuote(trashed.directory) + " | wc -l\n").data.trimmed().toInt();
            QCOMPARE(left, mode == 3 ? 1 : 2); // lost ack: the first file really went
        } else { // empty
            const QString name = "em-" + tag;
            QVERIFY(put(name, "x"));
            QVERIFY(writer.moveToTrash(dir + "/" + name).status == SshResult::Status::Ok);
            arm("unlink");
            r = writer.emptyTrash();
            QCOMPARE(r.status, SshResult::Status::Error);
            QCOMPARE(r.code, expectedCode); // was: "N item(s) could not be deleted" with no sign that the link was gone
            // A refusal skips only that item and goes on (calls counts every unlink); a lost link stops at once.
            if (!lost) calls = 1;
        }
        QCOMPARE(calls, 1); // the mutation was attempted once and never retried
        QCOMPARE(writer.isOpen(), !lost); // a lost link closes the session; a refusal keeps it
        if (!lost) {
            publication_test::fault = {};
            QVERIFY(writer.list(dir).status == SshResult::Status::Ok);
        }
    }
};
QTEST_GUILESS_MAIN(PublicationTest)
#include "PublicationTest.moc"
