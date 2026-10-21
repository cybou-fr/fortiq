// Live SSH/SFTP check against a disposable test server (e.g. the WSL
// `fortiq-test` distro). Skipped unless FORTIQ_IT_HOST, FORTIQ_IT_PORT,
// FORTIQ_IT_USER, FORTIQ_IT_PASSWORD and FORTIQ_IT_FINGERPRINT (the host key
// read independently on the server) are set; FORTIQ_IT_KEY adds an unencrypted
// private-key login check. Secrets come only from the
// environment and are never printed.
#include "core/RemoteController.h"
#include "ssh/SshClient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QTemporaryDir>
#include <QSettings>
#include <QSignalSpy>

#include <QElapsedTimer>
#include <QtTest>

using fortiq::ConnectionRequest;
using fortiq::SshClient;
using fortiq::SshResult;

class SshIntegrationTest : public QObject {
    Q_OBJECT

    ConnectionRequest base;
    QString fingerprint;

    static QStringList names(const SshResult &result) {
        QStringList out;
        for (const QVariant &entry : result.entries) out << entry.toMap().value("name").toString();
        return out;
    }

private slots:
    void initTestCase() {
        if (qEnvironmentVariableIsEmpty("FORTIQ_IT_HOST")) QSKIP("FORTIQ_IT_* not set");
        base.host = qEnvironmentVariable("FORTIQ_IT_HOST");
        base.port = qEnvironmentVariableIntValue("FORTIQ_IT_PORT");
        base.user = qEnvironmentVariable("FORTIQ_IT_USER");
        base.password = qEnvironmentVariable("FORTIQ_IT_PASSWORD");
        fingerprint = qEnvironmentVariable("FORTIQ_IT_FINGERPRINT");
    }

    void unknownHostRequiresTrust() {
        const SshResult result = SshClient::list(base);
        QCOMPARE(result.status, SshResult::Status::TrustRequired);
        QCOMPARE(result.fingerprint, fingerprint);
    }

    void changedKeyIsRejected() {
        ConnectionRequest request = base;
        request.expectedHostFingerprint = QStringLiteral("SHA256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
        QCOMPARE(SshClient::list(request).status, SshResult::Status::Error);
    }

    void wrongPasswordFails() {
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        request.password += QStringLiteral("-wrong");
        QCOMPARE(SshClient::list(request).status, SshResult::Status::Error);
    }

    void privateKeyAuthenticates() {
        if (qEnvironmentVariableIsEmpty("FORTIQ_IT_KEY")) QSKIP("FORTIQ_IT_KEY not set");
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        request.password.clear();
        request.keyPath = qEnvironmentVariable("FORTIQ_IT_KEY");
        const SshResult result = SshClient::list(request);
        QVERIFY2(result.status == SshResult::Status::Ok, qPrintable(result.message));
    }

    void persistentSessionNavigates() {
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        if (request.password.isEmpty()) request.keyPath = qEnvironmentVariable("FORTIQ_IT_KEY");
        fortiq::SshSession session;
        const SshResult home = session.open(request);
        QVERIFY2(home.status == SshResult::Status::Ok, qPrintable(home.message));
        QVERIFY(!home.home.isEmpty());
        QElapsedTimer timer;
        timer.start();
        const SshResult root = session.list(QStringLiteral("/"));
        QVERIFY2(root.status == SshResult::Status::Ok, qPrintable(root.message));
        QVERIFY(names(root).contains(QStringLiteral("etc")));
        QVERIFY(session.list(QStringLiteral("/etc")).status == SshResult::Status::Ok);
        const SshResult back = session.list(home.home);
        QVERIFY(back.status == SshResult::Status::Ok);
        qInfo("3 listings on one session: %lld ms", static_cast<long long>(timer.elapsed()));
        // A missing folder is an SFTP error, not a lost connection.
        QVERIFY(session.list(QStringLiteral("/nonexistent-fortiq")).status == SshResult::Status::Error);
        QVERIFY(session.isOpen());
        // Entries carry metadata for the list view.
        const QVariantMap first = back.entries.constFirst().toMap();
        QVERIFY(first.contains("mtime") && first.contains("mode") && first.contains("size"));
    }

    void readFileIsBoundedAndReadOnly() {
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        if (request.password.isEmpty()) request.keyPath = qEnvironmentVariable("FORTIQ_IT_KEY");
        fortiq::SshSession session;
        const SshResult home = session.open(request);
        QVERIFY2(home.status == SshResult::Status::Ok, qPrintable(home.message));
        if (!names(home).contains(QStringLiteral("big.txt"))) QSKIP("preview fixtures not present on this server");

        const SshResult text = session.readFile(home.home + QStringLiteral("/Notes été.txt"), 1024);
        QVERIFY2(text.status == SshResult::Status::Ok, qPrintable(text.message));
        QVERIFY(QString::fromUtf8(text.data).startsWith(QStringLiteral("Bonjour été")));
        QVERIFY(!text.truncated);

        const SshResult big = session.readFile(home.home + QStringLiteral("/big.txt"), 4096);
        QVERIFY(big.status == SshResult::Status::Ok);
        QCOMPARE(big.data.size(), 4096);
        QVERIFY(big.truncated);
        QVERIFY(big.fileSize > 1000000);

        QVERIFY(session.readFile(home.home, 10).status == SshResult::Status::Error);          // directory
        QVERIFY(session.readFile(QStringLiteral("/nonexistent-fortiq"), 10).status == SshResult::Status::Error);
        QVERIFY(session.isOpen());
    }

    void controllerPreviewsTextImageBinary() {
        fortiq::RemoteController controller;
        QSignalSpy changed(&controller, &fortiq::RemoteController::changed);
        QSignalSpy preview(&controller, &fortiq::RemoteController::previewChanged);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        // Pin the verified test host for this run (the app's trust prompt is bypassed only here).
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        SshResult listing;
        listing.entries = controller.entries();
        if (!names(listing).contains(QStringLiteral("big.txt"))) QSKIP("preview fixtures not present on this server");
        const QString home = controller.homeDirectory();

        controller.openFile(home + QStringLiteral("/Notes été.txt"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.previewBusy() && controller.previewKind() == "text", 10000);
        QVERIFY(controller.previewText().contains(QStringLiteral("привет")));

        controller.openFile(home + QStringLiteral("/logo.png"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.previewBusy() && !controller.previewKind().isEmpty(), 10000);
        QCOMPARE(controller.previewKind(), QStringLiteral("image"));
        QVERIFY(controller.previewImage().startsWith(QStringLiteral("data:image/png;base64,iVBOR")));

        controller.openFile(home + QStringLiteral("/blob.bin"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.previewBusy() && !controller.previewKind().isEmpty(), 10000);
        QCOMPARE(controller.previewKind(), QStringLiteral("binary"));

        controller.openFile(home + QStringLiteral("/big.txt"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.previewBusy() && !controller.previewKind().isEmpty(), 10000);
        QCOMPARE(controller.previewKind(), QStringLiteral("text"));
        QVERIFY(controller.previewInfo().contains(QStringLiteral("premier Mo")));

        controller.closePreview();
        QVERIFY(!controller.previewOpen());
        controller.disconnectServer();
        settings.remove(pin);
    }

    // Needs a writable folder FORTIQ_IT_UPLOAD_DIR on the test server.
    void transfersAreSafe() {
        const QString remoteDir = qEnvironmentVariable("FORTIQ_IT_UPLOAD_DIR");
        if (remoteDir.isEmpty()) QSKIP("FORTIQ_IT_UPLOAD_DIR not set");
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        if (request.password.isEmpty()) request.keyPath = qEnvironmentVariable("FORTIQ_IT_KEY");
        fortiq::SshSession session;
        QVERIFY(session.open(request).status == SshResult::Status::Ok);

        QTemporaryDir local;
        QVERIFY(local.isValid());
        const QString tag = QString::number(QDateTime::currentMSecsSinceEpoch());
        const QString name = QStringLiteral("Été %1 — résumé.txt").arg(tag);
        const QString source = local.filePath(QStringLiteral("source.bin"));
        QByteArray payload(3 * 1024 * 1024 + 17, Qt::Uninitialized);
        for (qsizetype i = 0; i < payload.size(); ++i) payload[i] = char((i * 131) ^ (i >> 7));
        { QFile f(source); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(payload); }
        const auto noProgress = fortiq::TransferProgress([](qint64, qint64) { return true; });

        // 1. New file is published under its own name and reads back identically.
        SshResult up = session.upload(source, remoteDir, name, fortiq::ConflictPolicy::Fail, noProgress);
        QVERIFY2(up.status == SshResult::Status::Ok, qPrintable(up.message));
        QCOMPARE(up.directory, remoteDir + "/" + name);
        const QString back = local.filePath(QStringLiteral("back.bin"));
        SshResult down = session.download(up.directory, back, noProgress);
        QVERIFY2(down.status == SshResult::Status::Ok, qPrintable(down.message));
        { QFile f(back); QVERIFY(f.open(QIODevice::ReadOnly)); QVERIFY(f.readAll() == payload); }

        // 2. Same name: refused, then "keep both", then explicit replace.
        up = session.upload(source, remoteDir, name, fortiq::ConflictPolicy::Fail, noProgress);
        QCOMPARE(up.code, QStringLiteral("conflict"));
        up = session.upload(source, remoteDir, name, fortiq::ConflictPolicy::KeepBoth, noProgress);
        QVERIFY2(up.status == SshResult::Status::Ok, qPrintable(up.message));
        QVERIFY(up.directory.endsWith(QStringLiteral("résumé (2).txt")));
        const QString smaller = local.filePath(QStringLiteral("small.txt"));
        { QFile f(smaller); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("replaced\n"); }
        up = session.upload(smaller, remoteDir, name, fortiq::ConflictPolicy::Replace, noProgress);
        QVERIFY2(up.status == SshResult::Status::Ok, qPrintable(up.message));
        QCOMPARE(session.readFile(remoteDir + "/" + name, 100).data, QByteArray("replaced\n"));

        // 3. Cancel during upload: nothing published, no owned temporary left.
        const QString cancelled = QStringLiteral("cancel-%1.bin").arg(tag);
        int calls = 0;
        up = session.upload(source, remoteDir, cancelled, fortiq::ConflictPolicy::Fail,
                            [&](qint64, qint64) { return ++calls < 2; });
        QCOMPARE(up.code, QStringLiteral("cancelled"));
        const QStringList after = names(session.list(remoteDir));
        QVERIFY(!after.contains(cancelled));
        for (const QString &n : after) QVERIFY2(!n.contains(QStringLiteral(".fortiq-")), qPrintable(n));

        // 4. Cancelled download leaves no local file.
        const QString partial = local.filePath(QStringLiteral("partial.bin"));
        calls = 0;
        down = session.download(remoteDir + "/" + name.left(name.size() - 4) + " (2).txt", partial,
                                [&](qint64, qint64) { return ++calls < 2; });
        QCOMPARE(down.code, QStringLiteral("cancelled"));
        QVERIFY(!QFile::exists(partial));

        // 5. Directories are not downloadable; invalid names are rejected.
        QVERIFY(session.download(remoteDir, partial, noProgress).status == SshResult::Status::Error);
        QVERIFY(session.upload(source, remoteDir, QStringLiteral("../escape"), fortiq::ConflictPolicy::Fail, noProgress).status
                == SshResult::Status::Error);
        QVERIFY(session.isOpen());
    }

    void controllerQueueResolvesConflicts() {
        const QString remoteDir = qEnvironmentVariable("FORTIQ_IT_UPLOAD_DIR");
        if (remoteDir.isEmpty()) QSKIP("FORTIQ_IT_UPLOAD_DIR not set");
        fortiq::RemoteController controller;
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);

        QTemporaryDir local;
        const QString file = local.filePath(QStringLiteral("queue-%1.txt").arg(QDateTime::currentMSecsSinceEpoch()));
        { QFile f(file); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("queue\n"); }
        auto last = [&] { return controller.transfers().constLast().toMap(); };
        auto finished = [&] {
            const QString s = last().value("state").toString();
            return s != "queued" && s != "running";
        };

        controller.upload({ QUrl::fromLocalFile(file) }, remoteDir);
        QTRY_VERIFY_WITH_TIMEOUT(finished(), 15000);
        QCOMPARE(last().value("state").toString(), QStringLiteral("done"));

        controller.upload({ QUrl::fromLocalFile(file) }, remoteDir);
        QTRY_VERIFY_WITH_TIMEOUT(finished(), 15000);
        QCOMPARE(last().value("state").toString(), QStringLiteral("conflict"));
        controller.resolveConflict(last().value("id").toInt(), QStringLiteral("keepBoth"));
        QTRY_VERIFY_WITH_TIMEOUT(finished(), 15000);
        QCOMPARE(last().value("state").toString(), QStringLiteral("done"));
        QVERIFY(last().value("message").toString().contains(QStringLiteral("(2)")));

        const QString saved = local.filePath(QStringLiteral("downloaded.txt"));
        controller.download(remoteDir + "/" + QFileInfo(file).fileName(), QUrl::fromLocalFile(saved));
        QTRY_VERIFY_WITH_TIMEOUT(finished(), 15000);
        QCOMPARE(last().value("state").toString(), QStringLiteral("done"));
        { QFile f(saved); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), QByteArray("queue\n")); }

        controller.clearFinishedTransfers();
        QVERIFY(controller.transfers().isEmpty());
        controller.disconnectServer();
        settings.remove(pin);
    }

    void pinnedHostListsHomeWithUnicodeNames() {
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        const SshResult result = SshClient::list(request);
        QVERIFY2(result.status == SshResult::Status::Ok, qPrintable(result.message));
        const QStringList listed = names(result);
        QVERIFY(listed.contains(QStringLiteral("Mes documents")));
        QVERIFY(listed.contains(QStringLiteral("Notes été.txt")));
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    // Separate settings scope so the test never touches the user's pins.
    QCoreApplication::setOrganizationName(QStringLiteral("CYBOU-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("fortiq_ssh_it"));
    SshIntegrationTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "SshIntegrationTest.moc"
