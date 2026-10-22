// Live SSH/SFTP check against a disposable test server (e.g. the WSL
// `fortiq-test` distro). Skipped unless FORTIQ_IT_HOST, FORTIQ_IT_PORT,
// FORTIQ_IT_USER, FORTIQ_IT_PASSWORD and FORTIQ_IT_FINGERPRINT (the host key
// read independently on the server) are set; FORTIQ_IT_KEY adds an unencrypted
// private-key login check. Secrets come only from the
// environment and are never printed.
#include "core/LogsController.h"
#include "core/RemoteController.h"
#include "core/TerminalController.h"
#include "core/DisksController.h"
#include "core/DocumentsController.h"
#include "core/MarkdownData.h"
#include "core/DiskInfo.h"
#include "ssh/SshClient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>
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

    void interactiveTerminalPreservesStateAndResizes() {
        auto req=base; req.expectedHostFingerprint=fingerprint;
        if(req.password.isEmpty())req.keyPath=qEnvironmentVariable("FORTIQ_IT_KEY");
        fortiq::SshSession session;
        const auto opened=session.open(req);QVERIFY2(opened.status==SshResult::Status::Ok,qPrintable(opened.message));
        QVERIFY(session.startTerminal(80,24).status==SshResult::Status::Ok);
        auto readUntil=[&](const QByteArray &input,const QByteArray &needle,int cols=80,int rows=24) {
            QByteArray output; QElapsedTimer timer;timer.start();bool first=true;
            while(timer.elapsed()<6000) {
                auto r=session.terminalExchange(first?input:QByteArray(),cols,rows);first=false;
                if(r.status!=SshResult::Status::Ok)return QByteArray("ERROR");
                output+=r.data;if(output.contains(needle))break;QThread::msleep(20);
            }
            return output;
        };
        QVERIFY(readUntil("stty -echo; printf '\\nREADY\\n'\r","\r\nREADY\r\n").contains("\r\nREADY\r\n"));
        const auto unicode=QString::fromUtf8("été Привет").toUtf8();
        QVERIFY(readUntil("cd /tmp; export FORTIQ_TERM_MARK='"+unicode+"'; printf '\\nSET:%s\\n' \"$FORTIQ_TERM_MARK\"\r","SET:"+unicode).contains("SET:"+unicode));
        QVERIFY(readUntil("printf '\\nSTATE:%s:%s\\n' \"$PWD\" \"$FORTIQ_TERM_MARK\"\r","STATE:/tmp:"+unicode).contains("STATE:/tmp:"+unicode));
        QVERIFY(readUntil("printf '\\nSIZE:'; stty size\r","SIZE:35 120",120,35).contains("SIZE:35 120"));
        session.terminalExchange("sleep 30\r",120,35);QThread::msleep(150);
        session.terminalExchange(QByteArray(1,char(3)),120,35);QThread::msleep(100);
        QVERIFY(readUntil("printf '\\nALIVE\\n'\r","\r\nALIVE\r\n",120,35).contains("\r\nALIVE\r\n"));
        auto r=session.terminalExchange("exit 7\r",120,35);
        QElapsedTimer end;end.start();while(r.code.isEmpty()&&end.elapsed()<6000){QThread::msleep(20);r=session.terminalExchange({},120,35);}
        QCOMPARE(r.code,QString("exit:7"));QVERIFY(!session.isOpen());
    }

    void terminalControllerStopsOnServerChange() {
        QTemporaryDir settings;QVERIFY(settings.isValid());
        const auto oldOrg=QCoreApplication::organizationName(),oldApp=QCoreApplication::applicationName();
        const auto oldFormat=QSettings::defaultFormat();
        QCoreApplication::setOrganizationName("CYBOU-tests");QCoreApplication::setApplicationName("terminal-live");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
        {
            fortiq::RemoteController remote;
            fortiq::TerminalController terminal(&remote);
            terminal.setWindowActive(true); // opening without SSH waits for server selection
            QVERIFY(!terminal.running());QVERIFY(!terminal.busy());
            QSignalSpy output(&terminal,&fortiq::TerminalController::output),reset(&terminal,&fortiq::TerminalController::resetScreen);
            remote.connectServer(base.host,base.port,base.user,base.password,qEnvironmentVariable("FORTIQ_IT_KEY"));
            QTRY_VERIFY_WITH_TIMEOUT(!remote.busy(),20000);
            QCOMPARE(remote.pendingFingerprint(),fingerprint);remote.trustHost();
            QTRY_VERIFY_WITH_TIMEOUT(remote.connected(),20000);
            QTRY_VERIFY_WITH_TIMEOUT(terminal.running(),20000); // auto-opens after SSH connects
            QVERIFY(terminal.historyStatus().startsWith("~/.bash_history"));
            QVERIFY(!terminal.history().isEmpty()); // fixture Bash has saved history
            terminal.send("stty -echo; printf '\\nFQ_CONTROLLER\\n'\r");
            auto received=[&]{QByteArray data;for(const auto &entry:output)data+=entry.first().toByteArray();return data;};
            QTRY_VERIFY_WITH_TIMEOUT(received().contains("\r\nFQ_CONTROLLER\r\n"),6000);
            terminal.setWindowActive(false);terminal.setWindowActive(true); // reuse the existing shell
            terminal.stop();remote.changed();QTest::qWait(150);QVERIFY(!terminal.running());
            terminal.setWindowActive(false);terminal.setWindowActive(true);
            QTRY_VERIFY_WITH_TIMEOUT(terminal.running(),20000); // window reopen starts once
            remote.disconnectServer();QVERIFY(!terminal.running());QVERIFY(reset.size()>=2);
            QVERIFY(terminal.history().isEmpty());
            const auto count=output.size();terminal.send("must-not-send\r");QTest::qWait(200);QCOMPARE(output.size(),count);
        }
        QSettings::setDefaultFormat(oldFormat);QCoreApplication::setOrganizationName(oldOrg);QCoreApplication::setApplicationName(oldApp);
    }

    void markdownDocumentsRoundTripConflictAndDisconnect() {
        auto req=base;req.expectedHostFingerprint=fingerprint;if(req.password.isEmpty())req.keyPath=qEnvironmentVariable("FORTIQ_IT_KEY");
        fortiq::SshSession session;const auto opened=session.open(req);QVERIFY2(opened.status==SshResult::Status::Ok,qPrintable(opened.message));
        const auto initial=fortiq::encodeMarkdown(QString::fromUtf8("# Été\nПривет\n"),true,true);
        const auto created=session.saveCopy(opened.home,QString::fromUtf8("mdok-test été espace.md"),initial);QVERIFY2(created.status==SshResult::Status::Ok,qPrintable(created.message));
        const QString original=created.directory;QString copy;
        QTemporaryDir settings;QVERIFY(settings.isValid());
        const auto oldOrg=QCoreApplication::organizationName(),oldApp=QCoreApplication::applicationName();const auto oldFormat=QSettings::defaultFormat();
        QCoreApplication::setOrganizationName("CYBOU-tests");QCoreApplication::setApplicationName("mdok-live");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
        {
            fortiq::RemoteController remote;fortiq::DocumentsController docs(&remote);
            remote.connectServer(base.host,base.port,base.user,base.password,qEnvironmentVariable("FORTIQ_IT_KEY"));
            QTRY_VERIFY_WITH_TIMEOUT(!remote.busy(),20000);if(!remote.connected()){QCOMPARE(remote.pendingFingerprint(),fingerprint);remote.trustHost();}
            QTRY_VERIFY_WITH_TIMEOUT(remote.connected(),20000);
            docs.open(original);QTRY_VERIFY_WITH_TIMEOUT(!docs.busy(),20000);QCOMPARE(docs.path(),original);QVERIFY(docs.canSave());
            QCOMPARE(docs.text(),QString::fromUtf8("# Été\nПривет\n"));
            docs.setText(QString::fromUtf8("# Modifié\nBonjour Привет\n"));QVERIFY(docs.dirty());docs.save();
            QTRY_VERIFY_WITH_TIMEOUT(!docs.busy(),20000);QVERIFY2(!docs.dirty(),qPrintable(docs.status()));
            const auto savedBytes=fortiq::encodeMarkdown(docs.text(),true,true);
            QCOMPARE(session.readFile(original,262144).data,savedBytes);
            const auto external=QByteArray("# External\r\n");
            QVERIFY(session.saveFile(original,external,QCryptographicHash::hash(savedBytes,QCryptographicHash::Sha256),false).status==SshResult::Status::Ok);
            docs.setText("# Draft must survive\n");docs.save();QTRY_VERIFY_WITH_TIMEOUT(!docs.busy(),20000);
            QVERIFY(docs.dirty());QVERIFY(docs.status().contains("Conflit"));QCOMPARE(session.readFile(original,262144).data,external);
            docs.saveAs(original);QTRY_VERIFY_WITH_TIMEOUT(!docs.busy(),20000);QVERIFY(!docs.dirty());copy=docs.path();QVERIFY(copy!=original);
            QCOMPARE(session.readFile(copy,262144).data,fortiq::encodeMarkdown(docs.text(),true,true));
            docs.setText("# Offline draft\n");docs.save();remote.disconnectServer();
            QVERIFY(!docs.canSave());QVERIFY(!docs.busy());QVERIFY(docs.dirty());QCOMPARE(docs.text(),QString("# Offline draft\n"));
            docs.saveAs(original);QVERIFY(!docs.busy());QCOMPARE(docs.path(),copy);
        }
        for(const auto &path:QStringList{original,copy})if(!path.isEmpty()) {
            const auto quoted=QByteArray("'")+path.toUtf8().replace("'","'\\''")+"'";
            const auto removed=session.runScript("rm -- "+quoted+"\n",1024);QCOMPARE(removed.code,QString("exit:0"));
        }
        QSettings::setDefaultFormat(oldFormat);QCoreApplication::setOrganizationName(oldOrg);QCoreApplication::setApplicationName(oldApp);
    }

    void disksReadOnlyAndDisconnect() {
        auto req=base;req.expectedHostFingerprint=fingerprint;
        if(req.password.isEmpty())req.keyPath=qEnvironmentVariable("FORTIQ_IT_KEY");
        fortiq::SshSession session;auto opened=session.open(req);QVERIFY2(opened.status==SshResult::Status::Ok,qPrintable(opened.message));
        for(bool devices:{true,false}) {
            const auto result=session.runScript(devices?fortiq::diskDevicesScript():fortiq::diskMountsScript(),1048576);
            QVERIFY2(result.code=="exit:0",qPrintable(result.message));QVERIFY(!result.truncated);
            const auto parsed=fortiq::parseDiskRows(result.data,devices);QVERIFY2(parsed.error.isEmpty(),qPrintable(parsed.error));
            if(!devices)QVERIFY(!parsed.rows.isEmpty());
        }
        QTemporaryDir settings;QVERIFY(settings.isValid());
        const auto oldOrg=QCoreApplication::organizationName(),oldApp=QCoreApplication::applicationName();
        const auto oldFormat=QSettings::defaultFormat();
        QCoreApplication::setOrganizationName("CYBOU-tests");QCoreApplication::setApplicationName("disks-live");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
        {
            fortiq::RemoteController remote;fortiq::DisksController disks(&remote);disks.setWindowActive(true);
            QVERIFY(!disks.busy());
            remote.connectServer(base.host,base.port,base.user,base.password,qEnvironmentVariable("FORTIQ_IT_KEY"));
            QTRY_VERIFY_WITH_TIMEOUT(!remote.busy(),20000);if(!remote.connected()){QCOMPARE(remote.pendingFingerprint(),fingerprint);remote.trustHost();}
            QTRY_VERIFY_WITH_TIMEOUT(remote.connected(),20000);
            QTRY_VERIFY_WITH_TIMEOUT(disks.state()=="ready",20000);QVERIFY(!disks.mounts().isEmpty());QVERIFY(!disks.versions().isEmpty());
            disks.reload();QVERIFY(disks.busy());remote.disconnectServer();
            QVERIFY(disks.mounts().isEmpty());QVERIFY(disks.devices().isEmpty());QVERIFY(!disks.busy());
            QTest::qWait(300);QVERIFY(disks.mounts().isEmpty());QCOMPARE(disks.state(),QString("disconnected"));
        }
        QSettings::setDefaultFormat(oldFormat);QCoreApplication::setOrganizationName(oldOrg);QCoreApplication::setApplicationName(oldApp);
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

    void folderRenameAndTrash() {
        const QString remoteDir = qEnvironmentVariable("FORTIQ_IT_UPLOAD_DIR");
        if (remoteDir.isEmpty()) QSKIP("FORTIQ_IT_UPLOAD_DIR not set");
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        if (request.password.isEmpty()) request.keyPath = qEnvironmentVariable("FORTIQ_IT_KEY");
        fortiq::SshSession session;
        const SshResult opened = session.open(request);
        QVERIFY(opened.status == SshResult::Status::Ok);
        const QString home = opened.home;
        const QString tag = QString::number(QDateTime::currentMSecsSinceEpoch());

        // Create, then refuse the same name again.
        const QString folder = QStringLiteral("Dossier été %1").arg(tag);
        SshResult r = session.makeDirectory(remoteDir, folder);
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QCOMPARE(session.makeDirectory(remoteDir, folder).code, QStringLiteral("conflict"));
        QVERIFY(session.makeDirectory(remoteDir, QStringLiteral("a/b")).status == SshResult::Status::Error);
        QVERIFY(session.makeDirectory(remoteDir, QStringLiteral("..")).status == SshResult::Status::Error);

        // Rename within the folder; refuse an existing target and path tricks.
        const QString renamed = QStringLiteral("Projet — %1").arg(tag);
        r = session.renameEntry(remoteDir + "/" + folder, renamed);
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QVERIFY(session.makeDirectory(remoteDir, folder).status == SshResult::Status::Ok);
        QCOMPARE(session.renameEntry(remoteDir + "/" + folder, renamed).code, QStringLiteral("conflict"));
        QVERIFY(session.renameEntry(remoteDir + "/" + folder, QStringLiteral("../escape")).status == SshResult::Status::Error);
        QVERIFY(names(session.list(remoteDir)).contains(renamed));

        // Trash: moved under ~/.local/share/Trash/files with a matching .trashinfo.
        r = session.moveToTrash(remoteDir + "/" + renamed);
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        const QString trashed = r.directory;
        QVERIFY(trashed.startsWith(home + "/.local/share/Trash/files/"));
        QVERIFY(!names(session.list(remoteDir)).contains(renamed));
        const QString infoPath = home + "/.local/share/Trash/info/" + trashed.section('/', -1) + ".trashinfo";
        const QByteArray info = session.readFile(infoPath, 4096).data;
        QVERIFY(info.startsWith("[Trash Info]\nPath="));
        QVERIFY(info.contains(QUrl::toPercentEncoding(remoteDir + "/" + renamed, "/")));
        QVERIFY(info.contains("DeletionDate="));
        // Same name again goes to a distinct Trash name.
        QVERIFY(session.makeDirectory(remoteDir, renamed).status == SshResult::Status::Ok);
        r = session.moveToTrash(remoteDir + "/" + renamed);
        QVERIFY(r.status == SshResult::Status::Ok);
        QVERIFY(r.directory != trashed);

        // Guard rails: home, /, the Trash and items already in it.
        QVERIFY(session.moveToTrash(home).status == SshResult::Status::Error);
        QVERIFY(session.moveToTrash(QStringLiteral("/")).status == SshResult::Status::Error);
        QVERIFY(session.moveToTrash(home + "/.local/share/Trash").status == SshResult::Status::Error);
        QVERIFY(session.moveToTrash(trashed).status == SshResult::Status::Error);
        QVERIFY(session.moveToTrash(remoteDir + "/missing-" + tag).status == SshResult::Status::Error);
        QVERIFY(session.isOpen());
    }

    // FORTIQ_IT_UPLOAD_DIR must contain keep/precious.txt and may contain
    // linktree/escape -> ../keep; FORTIQ_IT_CLI_TRASHED may name an item that
    // trash-put moved from FORTIQ_IT_UPLOAD_DIR to the Trash.
    void restoreDeleteAndEmptyTrash() {
        const QString remoteDir = qEnvironmentVariable("FORTIQ_IT_UPLOAD_DIR");
        if (remoteDir.isEmpty()) QSKIP("FORTIQ_IT_UPLOAD_DIR not set");
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        if (request.password.isEmpty()) request.keyPath = qEnvironmentVariable("FORTIQ_IT_KEY");
        fortiq::SshSession session;
        const SshResult opened = session.open(request);
        QVERIFY(opened.status == SshResult::Status::Ok);
        const QString home = opened.home;
        const QString files = home + "/.local/share/Trash/files";
        const QString tag = QString::number(QDateTime::currentMSecsSinceEpoch());
        QTemporaryDir local;
        const QString small = local.filePath("s.txt");
        { QFile f(small); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x\n"); }
        const auto ok = fortiq::TransferProgress([](qint64, qint64) { return true; });

        // Restore to the original place.
        const QString name = QStringLiteral("restore-é-%1.txt").arg(tag);
        QVERIFY(session.upload(small, remoteDir, name, fortiq::ConflictPolicy::Fail, ok).status == SshResult::Status::Ok);
        SshResult r = session.moveToTrash(remoteDir + "/" + name);
        QVERIFY(r.status == SshResult::Status::Ok);
        const QString trashed = r.directory;
        r = session.restoreFromTrash(trashed, fortiq::ConflictPolicy::Fail);
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QCOMPARE(r.directory, remoteDir + "/" + name);
        QVERIFY(!names(session.list(files)).contains(trashed.section('/', -1)));
        QVERIFY(session.readFile(home + "/.local/share/Trash/info/" + trashed.section('/', -1) + ".trashinfo", 10).status
                == SshResult::Status::Error);

        // Occupied name: conflict, then keep both.
        r = session.moveToTrash(remoteDir + "/" + name);
        const QString again = r.directory;
        QVERIFY(session.upload(small, remoteDir, name, fortiq::ConflictPolicy::Fail, ok).status == SshResult::Status::Ok);
        QCOMPARE(session.restoreFromTrash(again, fortiq::ConflictPolicy::Fail).code, QStringLiteral("conflict"));
        r = session.restoreFromTrash(again, fortiq::ConflictPolicy::KeepBoth);
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QVERIFY(r.directory.endsWith(QStringLiteral(" (2).txt")));

        // Original folder gone: refused, item stays in the Trash.
        const QString gone = QStringLiteral("gone-%1").arg(tag);
        QVERIFY(session.makeDirectory(remoteDir, gone).status == SshResult::Status::Ok);
        QVERIFY(session.upload(small, remoteDir + "/" + gone, "f.txt", fortiq::ConflictPolicy::Fail, ok).status == SshResult::Status::Ok);
        const QString orphan = session.moveToTrash(remoteDir + "/" + gone + "/f.txt").directory;
        QVERIFY(session.moveToTrash(remoteDir + "/" + gone).status == SshResult::Status::Ok);
        r = session.restoreFromTrash(orphan, fortiq::ConflictPolicy::Fail);
        QVERIFY(r.status == SshResult::Status::Error);
        QVERIFY(names(session.list(files)).contains(orphan.section('/', -1)));

        // Permanent deletion of a nested folder holding a symlink to keep/:
        // the link is removed, its target is untouched.
        const QString tree = QStringLiteral("tree-%1").arg(tag);
        QVERIFY(session.makeDirectory(remoteDir, tree).status == SshResult::Status::Ok);
        QVERIFY(session.makeDirectory(remoteDir + "/" + tree, "sub").status == SshResult::Status::Ok);
        QVERIFY(session.upload(small, remoteDir + "/" + tree + "/sub", "deep.txt", fortiq::ConflictPolicy::Fail, ok).status == SshResult::Status::Ok);
        const QString inTrash = session.moveToTrash(remoteDir + "/" + tree).directory;
        QVERIFY(!inTrash.isEmpty());
        // Outside the Trash permanent deletion is refused.
        QVERIFY(session.deleteFromTrash(remoteDir + "/" + name).status == SshResult::Status::Error);
        QVERIFY(session.deleteFromTrash(home).status == SshResult::Status::Error);
        r = session.deleteFromTrash(inTrash);
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QVERIFY(r.bytes >= 3);
        QVERIFY(!names(session.list(files)).contains(inTrash.section('/', -1)));
        // linktree/ (prepared on the server) holds "escape" -> ../keep.
        if (names(session.list(remoteDir)).contains(QStringLiteral("linktree"))) {
            const QString linked = session.moveToTrash(remoteDir + "/linktree").directory;
            QVERIFY(!linked.isEmpty());
            r = session.deleteFromTrash(linked);
            QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        }
        QVERIFY(names(session.list(remoteDir + "/keep")).contains(QStringLiteral("precious.txt")));

        // An item trashed by trash-put on the server restores as well.
        const QString cli = qEnvironmentVariable("FORTIQ_IT_CLI_TRASHED");
        if (!cli.isEmpty()) {
            r = session.restoreFromTrash(files + "/" + cli, fortiq::ConflictPolicy::Fail);
            QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
            QCOMPARE(r.directory, remoteDir + "/" + cli);
        }

        // Empty the Trash: files/ and info/ are empty afterwards.
        r = session.emptyTrash();
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QVERIFY(session.list(files).entries.isEmpty());
        QVERIFY(session.list(home + "/.local/share/Trash/info").entries.isEmpty());
        QVERIFY(names(session.list(remoteDir + "/keep")).contains(QStringLiteral("precious.txt")));
        QVERIFY(session.isOpen());
    }

    // Needs, in FORTIQ_IT_UPLOAD_DIR/edit: real.txt (mode 640), link.txt -> real.txt,
    // root.txt owned by another user but writable (666), crlf.txt with BOM + CRLF.
    void safeSave() {
        const QString remoteDir = qEnvironmentVariable("FORTIQ_IT_UPLOAD_DIR");
        if (remoteDir.isEmpty()) QSKIP("FORTIQ_IT_UPLOAD_DIR not set");
        const QString dir = remoteDir + "/edit";
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        if (request.password.isEmpty()) request.keyPath = qEnvironmentVariable("FORTIQ_IT_KEY");
        fortiq::SshSession session, other;
        QVERIFY(session.open(request).status == SshResult::Status::Ok);
        QVERIFY(other.open(request).status == SshResult::Status::Ok);
        if (!names(session.list(dir)).contains(QStringLiteral("real.txt"))) QSKIP("edit fixtures not present");
        auto hash = [](const QByteArray &b) { return QCryptographicHash::hash(b, QCryptographicHash::Sha256); };
        auto entry = [&](const QString &name) {
            for (const QVariant &v : session.list(dir).entries) if (v.toMap().value("name") == name) return v.toMap();
            return QVariantMap();
        };

        // Save keeps permissions, content reads back.
        const QByteArray v1 = session.readFile(dir + "/real.txt", 4096).data;
        SshResult r = session.saveFile(dir + "/real.txt", "version 2 — é\n", hash(v1), false);
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QCOMPARE(session.readFile(dir + "/real.txt", 4096).data, QByteArray("version 2 — é\n"));
        QCOMPARE(entry("real.txt").value("mode").toString(), QStringLiteral("-rw-r-----"));

        // Someone else saves meanwhile -> conflict, server version untouched.
        const QByteArray v2 = "version 2 — é\n";
        QVERIFY(other.saveFile(dir + "/real.txt", "theirs\n", hash(v2), false).status == SshResult::Status::Ok);
        r = session.saveFile(dir + "/real.txt", "mine\n", hash(v2), false);
        QCOMPARE(r.code, QStringLiteral("conflict"));
        QCOMPARE(session.readFile(dir + "/real.txt", 4096).data, QByteArray("theirs\n"));

        // Keep both: a copy next to it; then an explicit overwrite.
        r = session.saveCopy(dir, "real.txt", "mine\n");
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        // "real (2).txt", or a higher number if an earlier run left one.
        QVERIFY(QRegularExpression(QStringLiteral(R"(/real \(\d+\)\.txt$)")).match(r.directory).hasMatch());
        QCOMPARE(session.readFile(r.directory, 100).data, QByteArray("mine\n"));
        r = session.saveFile(dir + "/real.txt", "mine\n", hash(v2), true);
        QVERIFY(r.status == SshResult::Status::Ok);
        QCOMPARE(session.readFile(dir + "/real.txt", 4096).data, QByteArray("mine\n"));

        // Through a symlink: the target changes, the link stays a link.
        r = session.saveFile(dir + "/link.txt", "via link\n", hash("mine\n"), false);
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QCOMPARE(r.directory, dir + "/real.txt");
        QCOMPARE(session.readFile(dir + "/real.txt", 4096).data, QByteArray("via link\n"));
        QVERIFY(entry("link.txt").value("link").toBool());

        // Another user's file is refused and unchanged.
        const QByteArray rootBefore = session.readFile(dir + "/root.txt", 4096).data;
        r = session.saveFile(dir + "/root.txt", "hijack\n", hash(rootBefore), false);
        QVERIFY(r.status == SshResult::Status::Error);
        QCOMPARE(session.readFile(dir + "/root.txt", 4096).data, rootBefore);

        for (const QString &n : names(session.list(dir))) QVERIFY2(!n.contains(QStringLiteral(".fortiq-")), qPrintable(n));
    }

    void controllerSaveKeepsLineEndingsAndBom() {
        const QString remoteDir = qEnvironmentVariable("FORTIQ_IT_UPLOAD_DIR");
        if (remoteDir.isEmpty()) QSKIP("FORTIQ_IT_UPLOAD_DIR not set");
        const QString file = remoteDir + "/edit/crlf.txt";
        fortiq::RemoteController controller;
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        controller.openFile(file);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.previewBusy() && !controller.previewKind().isEmpty(), 10000);
        if (controller.previewKind() == "error") QSKIP("edit fixtures not present");
        QVERIFY(controller.previewEditable());
        QVERIFY(!controller.previewText().contains(QChar(0xFEFF)));

        QSignalSpy saved(&controller, &fortiq::RemoteController::saveFinished);
        controller.saveFile(controller.previewText().replace("\r\n", "\n") + "ajout\n");
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 15000);
        QVERIFY2(saved.at(0).at(0).toBool(), qPrintable(saved.at(0).at(1).toString()));

        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        if (request.password.isEmpty()) request.keyPath = qEnvironmentVariable("FORTIQ_IT_KEY");
        fortiq::SshSession session;
        QVERIFY(session.open(request).status == SshResult::Status::Ok);
        const QByteArray bytes = session.readFile(file, 4096).data;
        QVERIFY(bytes.startsWith("\xEF\xBB\xBF"));
        QVERIFY(bytes.endsWith("ajout\r\n"));
        QVERIFY(!bytes.contains("\n\n") && !bytes.contains("\r\r"));

        // Second save builds on the saved version (no false conflict).
        controller.saveFile(controller.previewText() + "encore\n");
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 2, 15000);
        QVERIFY2(saved.at(1).at(0).toBool(), qPrintable(saved.at(1).at(1).toString()));
        controller.disconnectServer();
        settings.remove(pin);
    }

    // Needs FORTIQ_IT_UPLOAD_DIR/search with: "Rapport été 2026.txt" (contains
    // "Bonjour le Monde"), "star*name.txt", "starXname.txt", sub/deep-rapport.md,
    // .hidden-rapport, 30 files many-N.txt and a binary blob.bin containing "Monde".
    void serverSearch() {
        const QString remoteDir = qEnvironmentVariable("FORTIQ_IT_UPLOAD_DIR");
        if (remoteDir.isEmpty()) QSKIP("FORTIQ_IT_UPLOAD_DIR not set");
        const QString dir = remoteDir + "/search";
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        if (request.password.isEmpty()) request.keyPath = qEnvironmentVariable("FORTIQ_IT_KEY");
        fortiq::SshSession session;
        QVERIFY(session.open(request).status == SshResult::Status::Ok);
        if (!names(session.list(dir)).contains(QStringLiteral("star*name.txt"))) QSKIP("search fixtures not present");
        const auto all = [](int) { return true; };
        auto found = [](const SshResult &r) {
            QStringList out;
            for (const QVariant &v : r.entries) out << v.toMap().value("name").toString();
            out.sort();
            return out;
        };

        // Name: case-insensitive substring, recursive, hidden files included, Unicode.
        SshResult r = session.search(dir, QStringLiteral("RAPPORT"), "name", 500, all);
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QCOMPARE(found(r), (QStringList{ ".hidden-rapport", "Rapport été 2026.txt", "deep-rapport.md" }));
        QVERIFY(r.code.startsWith(QStringLiteral("find")));
        QCOMPARE(found(session.search(dir, QStringLiteral("été 2026"), "name", 500, all)).size(), 1);
        // Glob characters are literal: "*" matches only the real asterisk.
        QCOMPARE(found(session.search(dir, QStringLiteral("star*name"), "name", 500, all)), QStringList{ "star*name.txt" });

        // Content: line number and text; binary files skipped.
        r = session.search(dir, QStringLiteral("monde"), "content", 500, all);
        QVERIFY2(r.status == SshResult::Status::Ok, qPrintable(r.message));
        QCOMPARE(found(r), QStringList{ "Rapport été 2026.txt" });
        QCOMPARE(r.entries.first().toMap().value("line").toInt(), 2);
        QVERIFY(r.entries.first().toMap().value("text").toString().contains(QStringLiteral("Bonjour le Monde")));
        qInfo("content search tool: %s", qPrintable(r.code));

        // Bounded and cancellable.
        r = session.search(dir, QStringLiteral("many-"), "name", 10, all);
        QVERIFY(r.status == SshResult::Status::Ok);
        QCOMPARE(r.entries.size(), 10);
        QVERIFY(r.truncated);
        r = session.search(QStringLiteral("/"), QStringLiteral("e"), "name", 500000, [](int) { return false; });
        QCOMPARE(r.code, QStringLiteral("cancelled"));
        QVERIFY(session.isOpen());

        // Hostile queries are data, never code.
        for (const QString &q : { QStringLiteral("'; touch /tmp/fortiq-pwned; '"), QStringLiteral("$(touch /tmp/fortiq-pwned)"),
                                  QStringLiteral("`touch /tmp/fortiq-pwned`"), QStringLiteral("\"; touch /tmp/fortiq-pwned; \"") }) {
            QVERIFY(session.search(dir, q, "name", 10, all).status == SshResult::Status::Ok);
            QVERIFY(session.search(dir, q, "content", 10, all).status == SshResult::Status::Ok);
        }
        QVERIFY(session.search(dir, QStringLiteral("a\nb"), "name", 10, all).status == SshResult::Status::Error);
        QVERIFY(session.search(QStringLiteral("/tmp"), QStringLiteral("fortiq-pwned"), "name", 10, all).entries.isEmpty());
        QVERIFY(session.list(dir).status == SshResult::Status::Ok); // SFTP still fine after exec channels
    }

    void controllerSearch() {
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
        controller.navigate(remoteDir + "/search");
        QTRY_VERIFY_WITH_TIMEOUT(controller.currentDirectory() == remoteDir + "/search" && !controller.busy(), 15000);

        controller.startSearch(QStringLiteral("monde"), QStringLiteral("content"));
        QVERIFY(controller.searchActive());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.searchBusy(), 15000);
        QCOMPARE(controller.searchResults().size(), 1);
        QCOMPARE(controller.searchScope(), remoteDir + "/search");
        // Browsing works while the search connection exists.
        controller.navigate(remoteDir);
        QTRY_VERIFY_WITH_TIMEOUT(controller.currentDirectory() == remoteDir && !controller.busy(), 15000);

        controller.startSearch(QStringLiteral("e"), QStringLiteral("name"));
        controller.cancelSearch();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.searchBusy(), 15000);
        controller.clearSearch();
        QVERIFY(!controller.searchActive());
        controller.disconnectServer();
        settings.remove(pin);
    }

    void logsGroupingIsDeterministic() {
        using fortiq::LogsController;
        QCOMPARE(LogsController::templateOf(QStringLiteral("worker 913 exited, client 10.0.0.12:443 id 0x1f2e")),
                 QStringLiteral("worker # exited, client ‹ip› id ‹hex›"));
        QCOMPARE(LogsController::templateOf(QStringLiteral("job 123e4567-e89b-12d3-a456-426614174000 done")),
                 QStringLiteral("job ‹id› done"));
        QVariantList events;
        for (int i = 0; i < 3; ++i)
            events << QVariantMap { { "unit", "a.service" }, { "message", QStringLiteral("timeout after %1 ms").arg(100 + i) },
                                    { "priority", 3 - (i == 2) }, { "time", 1000.0 * (i + 1) } };
        events << QVariantMap { { "unit", "b.service" }, { "message", "timeout after 5 ms" }, { "priority", 6 }, { "time", 9000.0 } };
        const QVariantList groups = LogsController::groupPatterns(events);
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups[0].toMap().value("count").toInt(), 3);
        QCOMPARE(groups[0].toMap().value("priority").toInt(), 2); // worst of the group
        const QVariantMap e = LogsController::parseJournalLine(
            R"({"__REALTIME_TIMESTAMP":"1760788800000000","PRIORITY":"3","_SYSTEMD_UNIT":"nginx.service","MESSAGE":[104,105],"__CURSOR":"s=1","_PID":"7"})");
        QCOMPARE(e.value("message").toString(), QStringLiteral("hi"));
        QCOMPARE(e.value("priority").toInt(), 3);
        QCOMPARE(e.value("unit").toString(), QStringLiteral("nginx.service"));
        QVERIFY(LogsController::parseJournalLine("not json").isEmpty());
    }

    // FORTIQ_IT_UPLOAD_DIR/app.log must exist; the user must NOT be in adm /
    // systemd-journal; FORTIQ_IT_LOG_TAG names three `logger -p user.err` lines
    // "<tag> failed after N ms" written just before the run.
    void logsJournalFilesFollow() {
        const QString remoteDir = qEnvironmentVariable("FORTIQ_IT_UPLOAD_DIR");
        const QString tag = qEnvironmentVariable("FORTIQ_IT_LOG_TAG");
        if (remoteDir.isEmpty() || tag.isEmpty()) QSKIP("FORTIQ_IT_UPLOAD_DIR / FORTIQ_IT_LOG_TAG not set");
        fortiq::RemoteController controller;
        fortiq::LogsController logs(&controller);
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);

        // Sources.
        QSignalSpy sources(&logs, &fortiq::LogsController::sourcesChanged);
        logs.loadSources();
        QTRY_VERIFY_WITH_TIMEOUT(sources.count() == 1, 15000);
        QVERIFY(!logs.units().isEmpty());
        bool lockedSeen = false;
        for (const QVariant &f : logs.files()) lockedSeen |= !f.toMap().value("readable").toBool();
        QVERIFY(lockedSeen); // e.g. /var/log/fail2ban.log (root:adm 640)

        // Journal, errors only, last 15 min: our three lines, one repeat group, limited access stated.
        logs.setRange(QStringLiteral("15min"));
        logs.setMaxPriority(3);
        QTRY_VERIFY_WITH_TIMEOUT(!logs.busy(), 20000);
        int ours = 0;
        for (const QVariant &v : logs.events()) {
            const QVariantMap e = v.toMap();
            QVERIFY(e.value("priority").toInt() <= 3);
            if (e.value("message").toString().startsWith(tag + QStringLiteral(" failed after"))) ++ours;
        }
        QCOMPARE(ours, 3);
        bool grouped = false;
        for (const QVariant &v : logs.patterns())
            grouped |= v.toMap().value("template").toString() == fortiq::LogsController::templateOf(tag + QStringLiteral(" failed after 1 ms"))
                       && v.toMap().value("count").toInt() >= 3; // earlier runs in the window share the template
        QVERIFY(grouped);
        QCOMPARE(logs.state(), QStringLiteral("limited"));

        // Plain file: last lines, then only appended ones, then rotation.
        const QString file = remoteDir + "/app.log";
        logs.select(QStringLiteral("file"), file);
        QTRY_VERIFY_WITH_TIMEOUT(!logs.busy(), 15000);
        QCOMPARE(logs.events().size(), 2);
        fortiq::SshSession writer;
        ConnectionRequest request = base;
        request.expectedHostFingerprint = fingerprint;
        if (request.password.isEmpty()) request.keyPath = key;
        QVERIFY(writer.open(request).status == SshResult::Status::Ok);
        QVERIFY(writer.runScript("printf 'third line\\nfourth line\\n' >> " + fortiq::SshSession::shellQuote(file) + "\n", 1024).code == "exit:0");
        QSignalSpy changed(&logs, &fortiq::LogsController::changed);
        logs.setFollowing(true);
        QTRY_VERIFY_WITH_TIMEOUT(logs.events().size() == 4, 15000);
        QCOMPARE(logs.events().last().toMap().value("message").toString(), QStringLiteral("fourth line"));
        QVERIFY(writer.runScript("printf 'after rotation\\n' > " + fortiq::SshSession::shellQuote(file) + "\n", 1024).code == "exit:0");
        QTRY_VERIFY_WITH_TIMEOUT(logs.state() == "rotated", 15000);
        QTRY_VERIFY_WITH_TIMEOUT(logs.events().size() == 1, 15000);
        logs.setFollowing(false);

        // Unreadable file: explicit denied state, no escalation.
        logs.select(QStringLiteral("file"), QStringLiteral("/var/log/fail2ban.log"));
        QTRY_VERIFY_WITH_TIMEOUT(!logs.busy(), 15000);
        QCOMPARE(logs.state(), QStringLiteral("denied"));
        QVERIFY(logs.events().isEmpty());

        // Disconnect freezes the view and stops following.
        logs.select(QStringLiteral("journal"));
        QTRY_VERIFY_WITH_TIMEOUT(!logs.busy(), 15000);
        logs.setFollowing(true);
        controller.disconnectServer();
        QCOMPARE(logs.state(), QStringLiteral("disconnected"));
        QVERIFY(!logs.following());
        settings.remove(pin);
    }

    void synopsisIsEvidenceBased() {
        using fortiq::LogsController;
        auto e = [](double s, int p, const QString &unit, const QString &msg, const QString &subject = {}) {
            return QVariant(QVariantMap { { "time", s * 1000.0 }, { "priority", p }, { "unit", unit }, { "message", msg }, { "subject", subject } });
        };
        const QVariantList events {
            e(100, 6, "nginx.service", "Reloading"),
            e(101, 3, "php.service", "pool reached max children (5)"),                      // 1
            e(102, 3, "nginx.service", "upstream timed out client 10.0.0.1"),               // 2
            e(150, 3, "nginx.service", "upstream timed out client 10.0.0.2"),               // 3 anchor
            e(151, 6, "systemd", "php.service: Main process exited, code=killed, status=9/KILL", "php.service"),
            e(152, 6, "systemd", "php.service: Scheduled restart job, restart counter is at 1.", "php.service"), // 5
            e(160, 3, "db.service", "disk quota exceeded on /var/lib/db"),                 // 6, only after anchor
        };
        const QVariantMap s = LogsController::synthesize(events, 3, 300, true, true, false);
        auto texts = [&](const QString &k) { QStringList out; for (const QVariant &v : s.value(k).toList()) out << v.toMap().value("text").toString(); return out; };
        auto refsOf = [&](const QString &k, const QString &needle) {
            for (const QVariant &v : s.value(k).toList())
                if (v.toMap().value("text").toString().contains(needle)) return v.toMap().value("refs").toList();
            return QVariantList();
        };
        const QStringList observed = texts("observed"), inferred = texts("inferred"), unknown = texts("unknown");
        QVERIFY(observed.first().contains("7 événement(s), 4 erreur(s)"));
        QCOMPARE(refsOf("observed", "Première erreur"), (QVariantList { 1 }));
        QCOMPARE(refsOf("observed", "redémarrage programmé n°1"), (QVariantList { 5 }));
        QCOMPARE(refsOf("observed", "Répété ×2"), (QVariantList { 2, 3 }));
        // Hypotheses are labelled and evidence-linked; no causal claim as fact.
        for (const QString &x : inferred) QVERIFY(x.startsWith("Hypothèse"));
        QCOMPARE(refsOf("inferred", "précèdent"), (QVariantList { 1, 2 }));
        QCOMPARE(refsOf("inferred", "le redémarrage de php.service"), (QVariantList { 1, 5 }));
        QVERIFY(!refsOf("inferred", "n'apparaît qu'après").isEmpty());
        for (const QString &x : observed) QVERIFY2(!x.contains("cause"), qPrintable(x));
        QVERIFY(unknown.first().contains("cause n'est pas déterminée"));
        QVERIFY(unknown.join('\n').contains("Accès limité"));
        QVERIFY(unknown.join('\n').contains("2000"));
        // A calm window says so instead of inventing a story.
        const QVariantMap calm = LogsController::synthesize({ e(1, 6, "a", "ok") }, 0, 60, false, false, false);
        QCOMPARE(calm.value("inferred").toList().size(), 0);
        QVERIFY(calm.value("unknown").toList().last().toMap().value("text").toString().contains("Aucune erreur"));
    }

    // FORTIQ_IT_CRASH_UNIT names a user service (systemd-run --user) that wrote
    // "<3><tag> boom" and failed with Restart=on-failure just before the run.
    void incidentInspectorOnRealRestart() {
        const QString unit = qEnvironmentVariable("FORTIQ_IT_CRASH_UNIT");
        const QString tag = qEnvironmentVariable("FORTIQ_IT_LOG_TAG");
        if (unit.isEmpty() || tag.isEmpty()) QSKIP("FORTIQ_IT_CRASH_UNIT / FORTIQ_IT_LOG_TAG not set");
        fortiq::RemoteController controller;
        fortiq::LogsController logs(&controller);
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        logs.select(QStringLiteral("user"));
        logs.setRange(QStringLiteral("15min"));
        logs.setMaxPriority(7);
        QTRY_VERIFY_WITH_TIMEOUT(!logs.busy(), 20000);
        QVariantMap boom;
        for (const QVariant &v : logs.events())
            if (v.toMap().value("message").toString() == tag + " boom") boom = v.toMap();
        QVERIFY2(!boom.isEmpty(), "crash line not found in the user journal");
        QCOMPARE(boom.value("unit").toString(), unit);
        QCOMPARE(boom.value("priority").toInt(), 3);

        logs.inspect(boom, 60);
        QVERIFY(logs.inspectorOpen());
        QTRY_VERIFY_WITH_TIMEOUT(!logs.inspectorBusy(), 20000);
        QVERIFY(logs.inspectorAnchor() >= 0);
        const QVariantMap s = logs.synopsis();
        QString all;
        for (const QString &k : { "observed", "inferred" })
            for (const QVariant &v : s.value(k).toList()) all += v.toMap().value("text").toString() + '\n';
        QVERIFY2(all.contains(unit + " : redémarrage programmé"), qPrintable(all));
        QVERIFY2(all.contains("Hypothèse : le redémarrage de " + unit), qPrintable(all));
        const QString report = logs.inspectorReport();
        QVERIFY(report.contains("sans AI") && report.contains(tag + " boom"));
        logs.setInspectorWindow(900);
        QTRY_VERIFY_WITH_TIMEOUT(!logs.inspectorBusy() && logs.inspectorWindow() == 900, 20000);
        logs.closeInspector();
        QVERIFY(!logs.inspectorOpen());
        controller.disconnectServer();
        settings.remove(pin);
    }

    void serviceParsing() {
        using fortiq::LogsController;
        const QVariantMap info = LogsController::parseServiceShow(
            "Id=php.service\nActiveState=active\nSubState=running\nUnitFileState=enabled\nResult=success\n"
            "MainPID=812\nNRestarts=3\nRestart=on-failure\nActiveEnterTimestamp=@1760788800\n"
            "MemoryCurrent=50540544\nCPUUsageNSec=312400000000\nTasksCurrent=18446744073709551615\n");
        QCOMPARE(info.value("id").toString(), QStringLiteral("php.service"));
        QCOMPARE(info.value("restarts").toInt(), 3);
        QCOMPARE(info.value("pid").toString(), QStringLiteral("812"));
        QCOMPARE(info.value("since").toDouble(), 1760788800000.0);
        QCOMPARE(info.value("cpu").toString(), QStringLiteral("312.4 s"));
        QVERIFY(!info.value("memory").toString().isEmpty());
        QVERIFY(info.value("tasks").toString().isEmpty()); // "unset" sentinel is not shown as a number
        QVERIFY(LogsController::parseServiceShow("MainPID=0\n").value("pid").toString().isEmpty());

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto e = [&](qint64 agoMs, const QString &msg) {
            return QVariant(QVariantMap { { "time", double(now - agoMs) }, { "message", msg }, { "subject", "php.service" }, { "unit", "systemd" } });
        };
        const QVariantList events { e(3 * 86400000LL, "Started php.service - PHP."), e(5000, "php.service: Failed with result 'exit-code'."),
                                    e(4000, "php.service: Scheduled restart job, restart counter is at 1."), e(3000, "Started php.service - PHP."),
                                    e(2000, "unrelated line") };
        const QVariantList history = LogsController::lifecycleHistory(events);
        QCOMPARE(history.size(), 4);
        QCOMPARE(history.first().toMap().value("kind").toString(), QStringLiteral("started")); // newest first
        QCOMPARE(history.at(1).toMap().value("kind").toString(), QStringLiteral("restart"));
        const QVariantList days = LogsController::dailyCounts(history, now);
        QCOMPARE(days.size(), 7);
        const QVariantMap today = days.last().toMap();
        QVERIFY(today.value("starts").toInt() >= 1 && today.value("failures").toInt() >= 1 && today.value("restarts").toInt() >= 1);
    }

    // Uses FORTIQ_IT_CRASH_UNIT (failing user service, see incidentInspectorOnRealRestart).
    void serviceView() {
        const QString unit = qEnvironmentVariable("FORTIQ_IT_CRASH_UNIT");
        if (unit.isEmpty()) QSKIP("FORTIQ_IT_CRASH_UNIT not set");
        fortiq::RemoteController controller;
        fortiq::LogsController logs(&controller);
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);

        QSignalSpy sources(&logs, &fortiq::LogsController::sourcesChanged);
        logs.loadSources();
        QTRY_VERIFY_WITH_TIMEOUT(sources.count() == 1, 15000);
        bool userListed = false;
        for (const QVariant &u : logs.units())
            userListed |= u.toMap().value("user").toBool() && u.toMap().value("name").toString() == unit;
        QVERIFY2(userListed, "the failing user service is not listed");

        // The failing user service: failed state, restarts and give-up in its history.
        logs.select(QStringLiteral("userunit"), unit);
        logs.loadService();
        QTRY_VERIFY_WITH_TIMEOUT(!logs.serviceBusy() && !logs.serviceInfo().isEmpty(), 20000);
        const QVariantMap info = logs.serviceInfo();
        QCOMPARE(info.value("id").toString(), unit);
        QCOMPARE(info.value("active").toString(), QStringLiteral("failed"));
        QCOMPARE(info.value("result").toString(), QStringLiteral("exit-code"));
        QVERIFY(!info.value("historyLimited").toBool());
        QStringList kinds;
        for (const QVariant &h : logs.serviceHistory()) kinds << h.toMap().value("kind").toString();
        QVERIFY2(kinds.contains("restart") && kinds.contains("failed") && kinds.contains("gaveup") && kinds.contains("started"),
                 qPrintable(kinds.join(',')));
        QVERIFY(logs.serviceDays().last().toMap().value("failures").toInt() >= 1);

        // A system service as an unprivileged user: status readable, history marked limited.
        logs.select(QStringLiteral("unit"), QStringLiteral("ssh.service"));
        logs.loadService();
        QTRY_VERIFY_WITH_TIMEOUT(!logs.serviceBusy() && !logs.serviceInfo().isEmpty(), 20000);
        QCOMPARE(logs.serviceInfo().value("active").toString(), QStringLiteral("active"));
        QVERIFY(logs.serviceInfo().value("historyLimited").toBool());

        logs.select(QStringLiteral("unit"), QStringLiteral("no-such-fortiq.service"));
        logs.loadService();
        QTRY_VERIFY_WITH_TIMEOUT(!logs.serviceBusy() && !logs.serviceInfo().isEmpty(), 20000);
        QVERIFY(!logs.serviceError().isEmpty());
        controller.disconnectServer();
        settings.remove(pin);
    }

    void savedServers() {
        fortiq::RemoteController controller;
        QSettings settings;
        settings.remove("servers/profiles");
        settings.remove("bootstrap"); // no migration of a previous single server here
        // Validation: no host / user / bad port / spaces -> refused.
        QVERIFY(controller.saveServer({ { "host", "" }, { "port", 22 }, { "user", "u" } }).isEmpty());
        QVERIFY(controller.saveServer({ { "host", "h" }, { "port", 70000 }, { "user", "u" } }).isEmpty());
        QVERIFY(controller.saveServer({ { "host", "a b" }, { "port", 22 }, { "user", "u" } }).isEmpty());

        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        const QString id = controller.saveServer({ { "name", "Test WSL" }, { "host", base.host }, { "port", base.port },
                                                   { "user", base.user }, { "keyPath", key }, { "password", "must-not-be-stored" } });
        QVERIFY(!id.isEmpty());
        QCOMPARE(controller.servers().size(), 1);
        QVariantMap p = controller.servers().first().toMap();
        QCOMPARE(p.value("name").toString(), QStringLiteral("Test WSL"));
        QVERIFY(!p.contains("password"));
        QVERIFY(!settings.value("servers/profiles").toByteArray().contains("must-not-be-stored"));
        // Edit keeps the id; a second server is added.
        QCOMPARE(controller.saveServer({ { "id", id }, { "name", "Renamed" }, { "host", base.host }, { "port", base.port },
                                         { "user", base.user }, { "keyPath", key } }), id);
        const QString other = controller.saveServer({ { "host", "example.invalid" }, { "port", 2200 }, { "user", "x" } });
        QCOMPARE(controller.servers().size(), 2);
        QCOMPARE(controller.servers().first().toMap().value("name").toString(), QStringLiteral("Renamed"));

        // Connect through the profile; the trust step stays mandatory.
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.remove(pin);
        controller.connectTo(id, key.isEmpty() ? base.password : QString());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pendingFingerprint().isEmpty(), 15000);
        QCOMPARE(controller.pendingFingerprint(), fingerprint);
        QVERIFY(!controller.connected());
        controller.trustHost();
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        QCOMPARE(controller.activeServerId(), id);
        bool activeFlag = false, pinnedFlag = false;
        for (const QVariant &v : controller.servers())
            if (v.toMap().value("id") == id) { activeFlag = v.toMap().value("active").toBool(); pinnedFlag = v.toMap().value("pinned").toBool(); }
        QVERIFY(activeFlag && pinnedFlag);

        // Removing the active server disconnects; removing the other keeps one.
        controller.removeServer(id);
        QVERIFY(!controller.connected());
        QCOMPARE(controller.servers().size(), 1);
        controller.removeServer(other);
        QVERIFY(controller.servers().isEmpty());
        settings.remove(pin);
        settings.remove("servers/profiles");
    }

    void controllerOperations() {
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
        controller.navigate(remoteDir);
        QTRY_VERIFY_WITH_TIMEOUT(controller.currentDirectory() == remoteDir && !controller.busy(), 15000);

        QSignalSpy finished(&controller, &fortiq::RemoteController::operationFinished);
        const QString name = QStringLiteral("ctl-%1").arg(QDateTime::currentMSecsSinceEpoch());
        auto listed = [&](const QString &n) { SshResult l; l.entries = controller.entries(); return names(l).contains(n); };

        controller.createFolder(name);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        QVERIFY(finished.at(0).at(0).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(listed(name) && !controller.busy(), 15000);

        controller.renameEntry(remoteDir + "/" + name, name + "-b");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 15000);
        QVERIFY(finished.at(1).at(0).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(listed(name + "-b") && !listed(name), 15000);

        controller.trashEntry(remoteDir + "/" + name + "-b");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 15000);
        QVERIFY(finished.at(2).at(0).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!listed(name + "-b") && !controller.busy(), 15000);
        QVERIFY(controller.trashDirectory().endsWith(QStringLiteral("/.local/share/Trash/files")));

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
