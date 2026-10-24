// Live SSH/SFTP check against a disposable test server (e.g. the WSL
// `fortiq-test` distro). Skipped unless FORTIQ_IT_HOST, FORTIQ_IT_PORT,
// FORTIQ_IT_USER, FORTIQ_IT_PASSWORD and FORTIQ_IT_FINGERPRINT (the host key
// read independently on the server) are set; FORTIQ_IT_KEY adds an unencrypted
// private-key login check. Secrets come only from the
// environment and are never printed.
#include "core/LogsController.h"
#include "core/RemoteController.h"
#include "core/TaskManager.h"
#include "browser/BrowserController.h"
#include "network/NetworkController.h"
#include "system/SystemController.h"
#include "db/DbController.h"
#include "mail/MailController.h"
#include "core/SecretStore.h"
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
#include <QJsonDocument>
#include <QJsonObject>
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
        auto last = [&] { return controller.tasks()->tasks().constLast().toMap(); };
        // Settled for this test: finished, or waiting for the user's decision.
        auto finished = [&] {
            const auto t = last();
            return !t.value("active").toBool() || !t.value("decision").toString().isEmpty();
        };

        controller.upload({ QUrl::fromLocalFile(file) }, remoteDir);
        QTRY_VERIFY_WITH_TIMEOUT(finished(), 15000);
        QCOMPARE(last().value("state").toString(), QStringLiteral("succeeded"));

        controller.upload({ QUrl::fromLocalFile(file) }, remoteDir);
        QTRY_VERIFY_WITH_TIMEOUT(finished(), 15000);
        QCOMPARE(last().value("state").toString(), QStringLiteral("queued"));
        QCOMPARE(last().value("decision").toString(), QStringLiteral("conflict"));
        controller.resolveConflict(last().value("id").toInt(), QStringLiteral("keepBoth"));
        QTRY_VERIFY_WITH_TIMEOUT(finished(), 15000);
        QCOMPARE(last().value("state").toString(), QStringLiteral("succeeded"));
        QVERIFY(last().value("message").toString().contains(QStringLiteral("(2)")));

        const QString saved = local.filePath(QStringLiteral("downloaded.txt"));
        controller.download(remoteDir + "/" + QFileInfo(file).fileName(), QUrl::fromLocalFile(saved));
        QTRY_VERIFY_WITH_TIMEOUT(finished(), 15000);
        QCOMPARE(last().value("state").toString(), QStringLiteral("succeeded"));
        { QFile f(saved); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), QByteArray("queue\n")); }

        controller.clearFinishedTransfers();
        QVERIFY(controller.tasks()->tasks().isEmpty());
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
        {
            const QVariantMap t = controller.tasks()->tasks().constLast().toMap();
            QCOMPARE(t.value("kind").toString(), QStringLiteral("search"));
            QCOMPARE(t.value("title").toString(), QStringLiteral("monde"));
            QCOMPARE(t.value("state").toString(), QStringLiteral("succeeded"));
            QVERIFY(t.value("cancellable").toBool());
        }
        // Browsing works while the search connection exists.
        controller.navigate(remoteDir);
        QTRY_VERIFY_WITH_TIMEOUT(controller.currentDirectory() == remoteDir && !controller.busy(), 15000);

        controller.startSearch(QStringLiteral("e"), QStringLiteral("name"));
        controller.cancelSearch();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.searchBusy(), 15000);
        controller.clearSearch();
        QVERIFY(!controller.searchActive());
        // Whatever the race between the cancel and the result, no search task stays active.
        QCOMPARE(controller.tasks()->activeCount(), 0);
        const QString lastState = controller.tasks()->tasks().constLast().toMap().value("state").toString();
        QVERIFY2(lastState == "cancelled" || lastState == "succeeded", qPrintable(lastState));
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

        // Each operation is a task of the shared model: succeeded, in order, not cancellable.
        const QVariantList taskList = controller.tasks()->tasks();
        QCOMPARE(taskList.size(), 3);
        const QStringList kinds { "createFolder", "rename", "trash" };
        for (int i = 0; i < 3; ++i) {
            const QVariantMap t = taskList.at(i).toMap();
            QCOMPARE(t.value("kind").toString(), kinds.at(i));
            QCOMPARE(t.value("state").toString(), QStringLiteral("succeeded"));
            QVERIFY(!t.value("cancellable").toBool());
            QCOMPARE(t.value("source").toString(), controller.serverLabel());
        }
        QCOMPARE(controller.tasks()->activeCount(), 0);

        controller.disconnectServer();
        settings.remove(pin);
    }

    // The System app reads processes, services and the overview of the test server (read-only).
    void systemAppReadsTheServer() {
        fortiq::RemoteController controller;
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        fortiq::SystemController sys(&controller);
        sys.setWindowActive(true);
        QTRY_COMPARE_WITH_TIMEOUT(sys.state(), QStringLiteral("ready"), 20000);
        const QVariantMap o = sys.overview();
        QVERIFY(!o.value("hostname").toString().isEmpty());
        QVERIFY(o.value("cores").toInt() >= 1);
        QVERIFY(o.value("memFraction").toDouble() > 0 && o.value("memFraction").toDouble() < 1.0);
        QVERIFY(!o.value("uptime").toString().isEmpty());
        QVERIFY(sys.processes().size() >= 5);
        bool hasInit = false, hasSshd = false;
        double previousCpu = 1e9;
        for (const QVariant &v : sys.processes()) {
            const QVariantMap p = v.toMap();
            hasInit |= p.value("pid").toInt() == 1;
            hasSshd |= p.value("name").toString() == "sshd" || p.value("command").toString().contains("sshd");
            QVERIFY(p.value("cpu").toDouble() <= previousCpu);        // ps --sort=-pcpu
            previousCpu = p.value("cpu").toDouble();
        }
        QVERIFY(hasInit);
        QVERIFY(hasSshd);
        QVERIFY(sys.hasSystemd());
        bool sshService = false;
        for (const QVariant &v : sys.services()) sshService |= v.toMap().value("unit").toString().startsWith("ssh");
        QVERIFY(sshService);
        // Refreshing reuses the connection and keeps the data.
        const QString firstUpdate = sys.updated();
        QTest::qWait(1100);
        sys.reload();
        QTRY_VERIFY_WITH_TIMEOUT(!sys.busy() && sys.updated() != firstUpdate, 15000);
        QCOMPARE(sys.state(), QStringLiteral("ready"));
        // Read-only: no task, and a new session clears everything.
        QCOMPARE(controller.tasks()->count(), 0);
        controller.disconnectServer();
        QCOMPARE(sys.state(), QStringLiteral("disconnected"));
        QVERIFY(sys.processes().isEmpty() && sys.services().isEmpty() && sys.overview().isEmpty());
        settings.remove(pin);
    }

    // The Network app reads interfaces, routes, ports and DNS of the test server and runs diagnostics there.
    void networkAppReadsTheServer() {
        fortiq::RemoteController controller;
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        fortiq::NetworkController net(&controller);
        net.setWindowActive(true);
        QTRY_COMPARE_WITH_TIMEOUT(net.state(), QStringLiteral("ready"), 20000);
        QVERIFY2(net.interfaces().size() >= 2, "lo and at least one more interface");
        bool hasLoopback = false;
        for (const QVariant &v : net.interfaces()) hasLoopback |= v.toMap().value("name").toString() == "lo";
        QVERIFY(hasLoopback);
        bool defaultRoute = false;
        for (const QVariant &v : net.routes()) defaultRoute |= v.toMap().value("isDefault").toBool();
        QVERIFY(defaultRoute);
        // sshd listens on the test port on all interfaces: reported as reachable from outside.
        bool sshListener = false;
        for (const QVariant &v : net.listeners()) sshListener |= v.toMap().value("port").toInt() == base.port && v.toMap().value("exposure").toString() == "all";
        QVERIFY2(sshListener, "the SSH port is listed as listening on all interfaces");
        QVERIFY(net.exposedCount() >= 1);
        QVERIFY(!net.dnsServers().isEmpty());
        QVERIFY(!net.hostname().isEmpty());

        // Diagnostics run on the server; invalid targets never reach it.
        const int tasksBefore = controller.tasks()->count();
        net.runProbe("ping", "a;b");
        QVERIFY(net.probeStatus().contains("Saisissez"));
        QCOMPARE(controller.tasks()->count(), tasksBefore);
        net.runProbe("resolve", "localhost");
        QTRY_VERIFY_WITH_TIMEOUT(!net.probeBusy(), 20000);
        QVERIFY2(net.probeOutput().contains("127.0.0.1") || net.probeOutput().contains("::1"), qPrintable(net.probeOutput()));
        if (net.tools().contains("ping")) {
            net.runProbe("ping", "127.0.0.1");
            QTRY_VERIFY_WITH_TIMEOUT(!net.probeBusy(), 30000);
            QVERIFY2(net.probeOutput().contains("packets transmitted"), qPrintable(net.probeOutput()));
            // A silent address takes seconds; stopping returns at once and the task ends as cancelled.
            QElapsedTimer timer;
            timer.start();
            net.runProbe("ping", "203.0.113.1");
            QTRY_VERIFY_WITH_TIMEOUT(net.probeBusy(), 5000);
            QTest::qWait(300);
            net.cancelProbe();
            QTRY_VERIFY_WITH_TIMEOUT(!net.probeBusy(), 6000);
            QVERIFY(timer.elapsed() < 6000);
            QCOMPARE(net.probeStatus(), QStringLiteral("Annulé."));
            QCOMPARE(controller.tasks()->tasks().constLast().toMap().value("state").toString(), QStringLiteral("cancelled"));
        }
        QCOMPARE(controller.tasks()->activeCount(), 0);
        QVERIFY(controller.tasks()->tasks().constLast().toMap().value("kind").toString() == "networkProbe");

        // A new server session clears everything.
        controller.disconnectServer();
        QCOMPARE(net.state(), QStringLiteral("disconnected"));
        QVERIFY(net.interfaces().isEmpty() && net.listeners().isEmpty() && net.probeOutput().isEmpty());
        settings.remove(pin);
    }

    // The Courrier app against the test server: a disposable notmuch database built by tool/make_mail_fixture.sh.
    // Needs FORTIQ_IT_MAIL=1 and `notmuch` installed on the test server.
    void mailAppReadsAndTags() {
        if (qEnvironmentVariable("FORTIQ_IT_MAIL").isEmpty()) QSKIP("FORTIQ_IT_MAIL not set");
        fortiq::RemoteController controller;
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);

        auto shell = [&](const QByteArray &script) {
            fortiq::SshSession session;
            const SshResult opened = session.open(controller.connectionRequest());
            if (opened.status != SshResult::Status::Ok) return opened;
            return session.runScript(script, 262144, {});
        };
        QFile fixtureScript(QString::fromUtf8(QT_TESTCASE_SOURCEDIR) + "/tool/make_mail_fixture.sh");
        QVERIFY(fixtureScript.open(QIODevice::ReadOnly));
        const SshResult made = shell("rm -f /tmp/fq_mail_pwned\nbash -s -- /tmp/fq_mail_out <<'FQ_FIXTURE_END'\n" + fixtureScript.readAll().replace("\r\n", "\n") + "\nFQ_FIXTURE_END\n");
        QVERIFY2(made.status == SshResult::Status::Ok && made.code == "exit:0", qPrintable(made.message + QString::fromUtf8(made.data)));
        const QString config = QStringLiteral("/home/") + base.user + QStringLiteral("/mailfix/nm.conf");

        fortiq::MailController mail(&controller);
        mail.setWindowActive(true);
        mail.setConfigPath(config);
        QTRY_VERIFY_WITH_TIMEOUT(mail.state() == "ready" && !mail.threads().isEmpty() && !mail.folders().isEmpty(), 25000);
        QCOMPARE(mail.threads().size(), 5);
        auto folderCount = [&](const QString &k) { for (const QVariant &f : mail.folders()) if (f.toMap().value("key").toString() == k) return f.toMap().value("count").toInt(); return -2; };
        QCOMPARE(folderCount("inbox"), 6);
        QCOMPARE(folderCount("unread"), 5);
        QCOMPARE(folderCount("flagged"), 1);
        QVERIFY(mail.version().startsWith("notmuch"));
        auto threadBySubject = [&](const QString &subject) { for (const QVariant &t : mail.threads()) if (t.toMap().value("subject").toString() == subject) return t.toMap(); return QVariantMap(); };

        // Search: the text is data. A shell-looking query matches nothing and runs nothing.
        mail.search("from:alice");
        QTRY_COMPARE_WITH_TIMEOUT(mail.threads().size(), 1, 15000);
        QCOMPARE(mail.threads().first().toMap().value("subject").toString(), QStringLiteral("Rapport"));
        mail.search("$(touch /tmp/fq_mail_pwned) ; `touch /tmp/fq_mail_pwned` \"");
        QTRY_VERIFY_WITH_TIMEOUT(!mail.busy(), 15000);
        QCOMPARE(QString::fromUtf8(shell("[ -e /tmp/fq_mail_pwned ] && echo YES || echo NO\n").data).trimmed(), QStringLiteral("NO"));
        mail.search(QString());
        QTRY_COMPARE_WITH_TIMEOUT(mail.threads().size(), 5, 15000);

        // Reading a conversation: decoded headers, attachments listed, replies nested; opening marks it read.
        const QString zoeId = threadBySubject(QString::fromUtf8("Caf\xC3\xA9 au lait \xE2\x9C\x93")).value("id").toString();
        QVERIFY(!zoeId.isEmpty());
        mail.openThread(zoeId);
        QTRY_COMPARE_WITH_TIMEOUT(mail.messages().size(), 2, 15000);
        QVERIFY(mail.messages().at(0).toMap().value("from").toString().contains(QString::fromUtf8("Zo\xC3\xAB")));
        QCOMPARE(mail.messages().at(1).toMap().value("depth").toInt(), 1);
        QVERIFY(mail.messages().at(0).toMap().value("hasHtml").toBool());
        QVERIFY(!mail.messages().at(0).toMap().value("html").toString().contains("script"));
        QTRY_VERIFY_WITH_TIMEOUT(!mail.messages().at(0).toMap().value("unread").toBool(), 15000);      // marked read
        QTRY_COMPARE_WITH_TIMEOUT(folderCount("unread"), 4, 15000);
        QVERIFY(!threadBySubject(QString::fromUtf8("Caf\xC3\xA9 au lait \xE2\x9C\x93")).value("unread").toBool());

        const QString reportId = threadBySubject("Rapport").value("id").toString();
        mail.openThread(reportId);
        QTRY_COMPARE_WITH_TIMEOUT(mail.messages().size(), 1, 15000);
        const QVariantMap report = mail.messages().first().toMap();
        QCOMPARE(report.value("attachments").toList().size(), 1);
        QCOMPARE(report.value("attachments").toList().first().toMap().value("name").toString(), QString::fromUtf8("Rapport \xC3\xA9t\xC3\xA9.pdf"));
        QCOMPARE(report.value("text").toString(), QString::fromUtf8("Voir le rapport en pi\xC3\xA8""ce jointe."));

        // HTML-only mail: inert rich text, subject shown literally.
        const QString promoId = threadBySubject("Offre <b>speciale</b>").value("id").toString();
        QVERIFY(!promoId.isEmpty());
        mail.openThread(promoId);
        QTRY_COMPARE_WITH_TIMEOUT(mail.messages().size(), 1, 15000);
        const QString html = mail.messages().first().toMap().value("html").toString();
        QVERIFY(!html.isEmpty() && !html.contains("<script") && !html.contains("onload") && !html.contains("track.example") && !html.contains("javascript"));

        // Tags: flag, unflag; archive removes the row from the inbox and only that tag changes.
        mail.setTag(reportId, "flagged", false);
        QTRY_COMPARE_WITH_TIMEOUT(folderCount("flagged"), 0, 15000);
        mail.setTag(reportId, "flagged", true);
        QTRY_COMPARE_WITH_TIMEOUT(folderCount("flagged"), 1, 15000);
        mail.setTag(reportId, "inbox", false);
        QTRY_COMPARE_WITH_TIMEOUT(folderCount("inbox"), 5, 15000);
        QCOMPARE(mail.threads().size(), 4);
        mail.setTag(reportId, "bogus", true);                 // tags outside the editable set never reach the server
        mail.setTag("zz;rm", "unread", true);                 // nor do invalid ids
        QTRY_VERIFY_WITH_TIMEOUT(!mail.busy(), 15000);
        const SshResult counts = shell("export NOTMUCH_CONFIG='" + config.toUtf8() + "'\nnotmuch count -- 'tag:bogus'; notmuch count -- 'tag:flagged'\n");
        QCOMPARE(QString::fromUtf8(counts.data).split('\n', Qt::SkipEmptyParts), QStringList({ "0", "1" }));
        mail.openFolder("all");
        QTRY_COMPARE_WITH_TIMEOUT(mail.threads().size(), 5, 15000);

        // A wrong database path gives a clear message and no stale data.
        mail.setConfigPath("/nonexistent/notmuch-config");
        QTRY_COMPARE_WITH_TIMEOUT(mail.state(), QStringLiteral("error"), 15000);
        QVERIFY2(mail.status().contains("notmuch"), qPrintable(mail.status()));
        mail.setConfigPath(config);
        QTRY_COMPARE_WITH_TIMEOUT(mail.state(), QStringLiteral("ready"), 15000);

        // Disconnect clears everything.
        controller.disconnectServer();
        QCOMPARE(mail.state(), QStringLiteral("disconnected"));
        QVERIFY(mail.threads().isEmpty() && mail.messages().isEmpty() && mail.folders().isEmpty());
        settings.remove(pin);
        settings.remove("mail/notmuchConfig");
    }

    // Mail slice 2 against the test server: the account wizard, msmtp sending and mbsync + notmuch.
    // Needs FORTIQ_IT_MAILSEND=1 and tool/setup_mail_live_fixture.sh run as root on the DISPOSABLE test Debian
    // (dovecot on 127.0.0.1:1143, SMTP sink on 127.0.0.1:2525). It deletes ~/.mbsyncrc, ~/.msmtprc, ~/.notmuch-config,
    // ~/Mail and ~/sinkout of the test user: never run it against a real mailbox.
    void mailSendAndWizard() {
        if (qEnvironmentVariable("FORTIQ_IT_MAILSEND").isEmpty()) QSKIP("FORTIQ_IT_MAILSEND not set");
        const QString password = QStringLiteral("Zq\"7 #x\\y$z;w");     // throwaway password of the fixture user
        fortiq::RemoteController controller;
        QSettings settings;
        settings.remove("mail/notmuchConfig"); settings.remove("mail/displayName");
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);

        auto shell = [&](const QByteArray &script) {
            fortiq::SshSession session;
            const SshResult opened = session.open(controller.connectionRequest());
            if (opened.status != SshResult::Status::Ok) return opened;
            return session.runScript(script, 1048576, {});
        };
        auto text = [&](const QByteArray &script) { return QString::fromUtf8(shell(script).data).trimmed(); };
        QVERIFY2(text("cd ~ && rm -rf .mbsyncrc* .msmtprc* .notmuch-config* Mail sinkout/*.eml sinkout/*.env sinkout/auth_failed .msmtprc.d; echo cleaned\n") == "cleaned", "cleanup");
        // A pre-existing msmtp configuration of the user, group-readable, must survive untouched (except its mode).
        QVERIFY(shell("printf 'defaults\\ntls on\\n\\naccount other\\nhost smtp.other.example\\nfrom other@example.org\\n' > ~/.msmtprc; chmod 644 ~/.msmtprc\n").code == "exit:0");

        fortiq::MailController mail(&controller);
        auto waitIdle = [&] { QTRY_VERIFY_WITH_TIMEOUT(!mail.actionBusy(), 60000); };

        // ---- probe: what exists on the server, no secrets ----
        mail.probeSetup("live");
        QTest::qWait(100); waitIdle();
        QVERIFY2(mail.setupState().value("ok").toBool(), qPrintable(mail.setupState().value("problem").toString()));
        QVERIFY(mail.setupState().value("hasMsmtprc").toBool());
        QVERIFY(!mail.setupState().value("hasMbsyncrc").toBool());
        QVERIFY(!mail.setupState().value("hasNotmuchConfig").toBool());
        QVERIFY(mail.setupState().value("msmtp").toString().startsWith("msmtp"));
        QVERIFY(mail.setupState().value("mbsync").toString().contains("1."));
        const QString home = mail.setupState().value("home").toString();
        QVERIFY(home.startsWith("/home/"));

        // ---- preview: the exact files, nothing written ----
        QVariantMap spec { { "name", "live" }, { "displayName", QString::fromUtf8("Zo\xC3\xAB Test") }, { "email", "me@example.net" },
                           { "imapHost", "127.0.0.1" }, { "imapPort", 1143 }, { "imapSecurity", "none" }, { "imapUser", "fqmail" },
                           { "smtpHost", "127.0.0.1" }, { "smtpPort", 2525 }, { "smtpSecurity", "none" }, { "smtpUser", "fqmail" }, { "password", password } };
        QVERIFY(!mail.previewSetup({ { "name", "bad name" } }).isEmpty());
        QVERIFY(mail.setupFiles().isEmpty());
        QVERIFY2(mail.previewSetup(spec).isEmpty(), "preview");
        QCOMPARE(mail.setupFiles().size(), 3);
        for (const QVariant &f : mail.setupFiles()) QVERIFY(!f.toMap().value("shown").toString().contains("Zq\"7"));   // masked
        QCOMPARE(text("[ -e ~/.mbsyncrc ] && echo Y || echo N\n"), QStringLiteral("N"));                           // nothing written without consent
        QCOMPARE(text("[ -e ~/.notmuch-config ] && echo Y || echo N\n"), QStringLiteral("N"));

        // ---- consent for two files only ----
        mail.applySetup({ "mbsyncrc", "msmtprc" });
        waitIdle();
        QVERIFY2(mail.actionStatus().startsWith("Écrit"), qPrintable(mail.actionStatus()));
        QVERIFY(mail.setupFiles().isEmpty());                                                                       // the password left the preview
        QCOMPARE(text("stat -c %a ~/.mbsyncrc ~/.msmtprc | tr '\\n' ' '\n"), QStringLiteral("600 600"));
        QCOMPARE(text("[ -e ~/.notmuch-config ] && echo Y || echo N\n"), QStringLiteral("N"));                      // not ticked: not written
        const QString msmtprc = text("cat ~/.msmtprc\n");
        QVERIFY(msmtprc.contains("account other\nhost smtp.other.example\nfrom other@example.org"));              // foreign content preserved
        QVERIFY(msmtprc.contains("# >>> FORTIQ account live") && msmtprc.contains("password \"Zq\"7 #x\\y$z;w\""));
        QCOMPARE(text("ls ~/.msmtprc.fortiq-bak-* | wc -l\n"), QStringLiteral("1"));                                  // backup of the previous file
        QCOMPARE(text("stat -c %a ~/.msmtprc.fortiq-bak-*\n"), QStringLiteral("600"));                               // a backup of a 0644 file is not left readable (audit)
        QCOMPARE(text("grep -c 'password' ~/.msmtprc.fortiq-bak-* | tr -d '\\n'\n"), QStringLiteral("0"));
        QCOMPARE(text("ls ~/.mbsyncrc.fortiq-bak-* 2>/dev/null | wc -l\n"), QStringLiteral("0"));                    // no previous .mbsyncrc: no backup
        // The same wizard run twice keeps one block (idempotent) and the other account.
        QTRY_VERIFY_WITH_TIMEOUT(mail.setupState().value("blockMsmtp").toBool(), 20000);
        QVERIFY(mail.previewSetup(spec).isEmpty());
        mail.applySetup({ "mbsyncrc", "msmtprc", "notmuch" });
        waitIdle();
        QCOMPARE(text("grep -c '# >>> FORTIQ account live' ~/.msmtprc ~/.mbsyncrc | tr '\\n' ' '\n").replace(home, ""), QStringLiteral("/.msmtprc:1 /.mbsyncrc:1"));
        QCOMPARE(text("grep -c 'account other' ~/.msmtprc\n"), QStringLiteral("1"));
        QVERIFY(text("cat ~/.notmuch-config\n").contains("path=" + home + "/Mail"));
        // An existing notmuch configuration is never overwritten.
        QTRY_VERIFY_WITH_TIMEOUT(mail.setupState().value("hasNotmuchConfig").toBool(), 20000);
        QVERIFY(mail.previewSetup(spec).isEmpty());
        QCOMPARE(mail.setupFiles().size(), 2);
        mail.clearSetup();
        QVERIFY(mail.setupFiles().isEmpty());

        // ---- tests of the connections ----
        mail.testSmtp("live"); waitIdle();
        QVERIFY2(mail.actionStatus().contains("SMTP répond"), qPrintable(mail.actionStatus()));
        QVERIFY(mail.actionOutput().contains("127.0.0.1"));
        mail.testImap("live"); waitIdle();
        QVERIFY2(mail.actionStatus().contains("IMAP réussie"), qPrintable(mail.actionStatus()));
        QVERIFY2(mail.actionOutput().contains("INBOX"), qPrintable(mail.actionOutput()));

        // ---- sync: mbsync (pull only) then notmuch new ----
        mail.setWindowActive(true);
        mail.syncNow("live"); waitIdle();
        QVERIFY2(mail.actionStatus().contains("relevé"), qPrintable(mail.actionStatus() + mail.actionOutput()));
        QVERIFY(mail.actionOutput().contains("notmuch new"));
        QTRY_VERIFY_WITH_TIMEOUT(mail.state() == "ready" && !mail.accounts().isEmpty(), 30000);
        QCOMPARE(mail.accounts().size(), 2);                                                                          // other + live
        QCOMPARE(text("find ~/Mail/live/INBOX/new ~/Mail/live/INBOX/cur -type f | wc -l\n"), QStringLiteral("1"));
        mail.search(QString());
        QTRY_VERIFY_WITH_TIMEOUT(!mail.threads().isEmpty(), 20000);
        QCOMPARE(mail.threads().first().toMap().value("subject").toString(), QStringLiteral("Message from IMAP"));
        const QString threadId = mail.threads().first().toMap().value("id").toString();
        mail.openThread(threadId);
        QTRY_VERIFY_WITH_TIMEOUT(!mail.messages().isEmpty(), 20000);
        const QVariantMap original = mail.messages().first().toMap();
        QTRY_VERIFY_WITH_TIMEOUT(!mail.busy(), 20000);

        // ---- reply draft ----
        const QVariantMap draft = mail.replyDraft(original);
        QCOMPARE(draft.value("subject").toString(), QStringLiteral("Re: Message from IMAP"));
        QCOMPARE(draft.value("to").toString(), QStringLiteral("Alice <alice@example.org>"));
        QVERIFY(draft.value("body").toString().contains("> hello from the imap server"));
        QCOMPARE(draft.value("inReplyTo").toString(), QStringLiteral("imap1@example.org"));

        // ---- sending: invalid drafts never reach the server ----
        const int tasksBefore = controller.tasks()->count();
        QVERIFY(!mail.send({ { "account", "live" }, { "to", "not an address" }, { "subject", "x" } }).isEmpty());
        QVERIFY(!mail.send({ { "account", "ghost" }, { "to", "a@b.org" } }).isEmpty());
        QVERIFY(!mail.send({ { "account", "live" }, { "to", "a@b.org\nBcc: evil@x.org" } }).isEmpty());
        QCOMPARE(controller.tasks()->count(), tasksBefore);
        QCOMPARE(text("ls ~/sinkout 2>/dev/null | grep -c eml\n"), QStringLiteral("0"));

        // ---- a real send through msmtp to the sink ----
        QVERIFY2(mail.send({ { "account", "live" }, { "to", "you@example.org, o'brien@example.org" }, { "cc", "cc@example.org" }, { "bcc", "hidden@example.org" },
                             { "subject", QString::fromUtf8("R\xC3\xA9sum\xC3\xA9 \xE2\x9C\x93") }, { "body", QString::fromUtf8("Bonjour\nligne 2 \xC3\xA9") },
                             { "inReplyTo", "imap1@example.org" } }).isEmpty(), "send");
        QTRY_VERIFY_WITH_TIMEOUT(mail.sendState() != "sending", 60000);
        QVERIFY2(mail.sendState() == "sent", qPrintable(mail.sendState() + " " + mail.sendStatus()));
        QCOMPARE(controller.tasks()->tasks().constLast().toMap().value("kind").toString(), QStringLiteral("mailSend"));
        QCOMPARE(controller.tasks()->tasks().constLast().toMap().value("state").toString(), QStringLiteral("succeeded"));
        QCOMPARE(text("ls ~/sinkout | grep -c eml\n"), QStringLiteral("1"));
        const QString env = text("cat $(ls -t ~/sinkout/*.env | head -1)\n");
        QVERIFY2(env.contains("from me@example.net"), qPrintable(env));
        for (const char *rcpt : { "rcpt you@example.org", "rcpt o'brien@example.org", "rcpt cc@example.org", "rcpt hidden@example.org" }) QVERIFY2(env.contains(rcpt), rcpt);
        const QString eml = text("cat $(ls -t ~/sinkout/*.eml | head -1)\n");
        QVERIFY(eml.contains("From: =?UTF-8?B?") || eml.contains("From: "));
        QVERIFY(!eml.contains("hidden@example.org"));                                                                 // Bcc stays in the envelope
        QVERIFY(eml.contains("In-Reply-To: <imap1@example.org>"));
        QVERIFY(eml.contains("Subject: =?UTF-8?B?"));
        // The stored copy and the replied tag, checked with notmuch on the server.
        const QString nm = "export NOTMUCH_CONFIG=" + home + "/.notmuch-config\n";
        QCOMPARE(text(nm.toUtf8() + "notmuch count -- 'tag:sent'\n"), QStringLiteral("1"));
        QCOMPARE(text(nm.toUtf8() + "notmuch count -- 'tag:replied and id:imap1@example.org'\n"), QStringLiteral("1"));
        QCOMPARE(text(nm.toUtf8() + "notmuch show --format=text --body=true -- tag:sent | grep -c 'Résumé' \n"), QStringLiteral("1"));
        const QString subjectLine = text(nm.toUtf8() + "notmuch search --output=summary --format=text -- tag:sent | head -1\n");
        QVERIFY2(subjectLine.contains(QString::fromUtf8("R\xC3\xA9sum\xC3\xA9 \xE2\x9C\x93")), qPrintable(subjectLine));       // headers and body decode correctly
        QCOMPARE(text(nm.toUtf8() + "notmuch show --format=text --body=true -- tag:sent | grep -c 'ligne 2 é'\n"), QStringLiteral("1"));

        // ---- wrong password: a clear error and no claim of delivery ----
        QVariantMap bad = spec;
        bad.insert("name", "badpw"); bad.insert("password", "wrong-password");
        mail.probeSetup("badpw"); QTest::qWait(100); waitIdle();
        QVERIFY(mail.previewSetup(bad).isEmpty());
        mail.applySetup({ "mbsyncrc", "msmtprc" }); waitIdle();
        QTRY_VERIFY_WITH_TIMEOUT(mail.accounts().size() == 3, 20000);
        QVERIFY(mail.send({ { "account", "badpw" }, { "to", "x@example.org" }, { "subject", "nope" }, { "body", "x" } }).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(mail.sendState() != "sending", 60000);
        QCOMPARE(mail.sendState(), QStringLiteral("failed"));
        QVERIFY2(mail.sendStatus().contains("Authentification"), qPrintable(mail.sendStatus()));
        QVERIFY(!mail.sendStatus().contains("wrong-password"));
        QCOMPARE(controller.tasks()->tasks().constLast().toMap().value("state").toString(), QStringLiteral("failed"));
        QCOMPARE(text("ls ~/sinkout | grep -c eml\n"), QStringLiteral("1"));                                         // still only the first message
        QCOMPARE(text(nm.toUtf8() + "notmuch count -- 'tag:sent'\n"), QStringLiteral("1"));                           // no copy of an unsent message
        mail.resetSend();

        // ---- a new session clears everything, including any preview ----
        QVERIFY(mail.previewSetup(spec).isEmpty());
        controller.disconnectServer();
        QVERIFY(mail.setupFiles().isEmpty() && mail.accounts().isEmpty() && mail.threads().isEmpty());
        QCOMPARE(mail.sendState(), QStringLiteral("idle"));
        QVERIFY(!mail.previewSetup(spec).isEmpty());                                                                  // no server: refused

        // cleanup of the test user's mail configuration
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        shell("cd ~ && rm -rf .mbsyncrc* .msmtprc* .notmuch-config* Mail\n");
        settings.remove(pin); settings.remove("mail/displayName");
    }

    // Attachments stay on the server: saved into ~/Downloads there, or fetched to this computer as a download task.
    // Needs FORTIQ_IT_MAIL=1 (uses the disposable notmuch database of tool/make_mail_fixture.sh).
    void mailAttachmentsStayOnTheServer() {
        if (qEnvironmentVariable("FORTIQ_IT_MAIL").isEmpty()) QSKIP("FORTIQ_IT_MAIL not set");
        fortiq::RemoteController controller;
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        auto shell = [&](const QByteArray &script) {
            fortiq::SshSession session;
            const SshResult opened = session.open(controller.connectionRequest());
            if (opened.status != SshResult::Status::Ok) return opened;
            return session.runScript(script, 262144, {});
        };
        auto text = [&](const QByteArray &script) { return QString::fromUtf8(shell(script).data).trimmed(); };
        QFile fixtureScript(QString::fromUtf8(QT_TESTCASE_SOURCEDIR) + "/tool/make_mail_fixture.sh");
        QVERIFY(fixtureScript.open(QIODevice::ReadOnly));
        const SshResult made = shell("bash -s -- /tmp/fq_mail_out <<'FQ_FIXTURE_END'\n" + fixtureScript.readAll().replace("\r\n", "\n") + "\nFQ_FIXTURE_END\n");
        QVERIFY2(made.status == SshResult::Status::Ok && made.code == "exit:0", qPrintable(made.message));
        QCOMPARE(text("cd ~ && rm -rf Downloads .cache/fortiq-mail; echo cleaned\n"), QStringLiteral("cleaned"));
        const QString home = text("printf '%s' \"$HOME\"\n");

        fortiq::MailController mail(&controller);
        mail.setWindowActive(true);
        mail.setConfigPath(home + "/mailfix/nm.conf");
        QTRY_VERIFY_WITH_TIMEOUT(mail.state() == "ready" && !mail.threads().isEmpty(), 25000);
        QString threadId;
        for (const QVariant &t : mail.threads()) if (t.toMap().value("subject").toString() == "Rapport") threadId = t.toMap().value("id").toString();
        QVERIFY(!threadId.isEmpty());
        mail.openThread(threadId);
        QTRY_VERIFY_WITH_TIMEOUT(!mail.messages().isEmpty(), 20000);
        const QVariantMap message = mail.messages().first().toMap();
        const QVariantMap attachment = message.value("attachments").toList().first().toMap();
        const QString name = attachment.value("name").toString();
        QCOMPARE(name, QString::fromUtf8("Rapport \xC3\xA9t\xC3\xA9.pdf"));
        const int part = attachment.value("part").toInt();
        QVERIFY(part >= 2);
        const QString messageId = message.value("id").toString();
        auto waitIdle = [&] { QTRY_VERIFY_WITH_TIMEOUT(!mail.actionBusy(), 30000); };

        // ---- on the server: decoded content, never overwriting, safe names ----
        mail.saveAttachmentToServer(messageId, part, name); waitIdle();
        QVERIFY2(mail.attachStatus().startsWith("Enregistr"), qPrintable(mail.attachStatus()));
        QVERIFY(mail.attachStatus().contains(home + "/Downloads/"));
        QCOMPARE(text("head -c 8 ~/Downloads/*.pdf\n"), QStringLiteral("%PDF-1.4"));                                          // base64 decoded
        QCOMPARE(text("stat -c %a ~/Downloads/*.pdf\n"), QStringLiteral("600"));
        mail.saveAttachmentToServer(messageId, part, name); waitIdle();
        QCOMPARE(text("ls ~/Downloads | wc -l\n"), QStringLiteral("2"));
        QVERIFY(text("ls ~/Downloads\n").contains(QString::fromUtf8("Rapport \xC3\xA9t\xC3\xA9 (1).pdf")));
        mail.saveAttachmentToServer(messageId, part, "../../etc/evil\\name"); waitIdle();                           // a hostile name stays inside the folder
        QVERIFY(text("ls ~/Downloads\n").contains("_.._etc_evil_name"));
        QCOMPARE(text("ls ~/Downloads | wc -l\n"), QStringLiteral("3"));
        QCOMPARE(text("[ -e ~/etc ] && echo Y || echo N\n"), QStringLiteral("N"));
        // A wrong part or an unknown message saves nothing and says so.
        mail.saveAttachmentToServer(messageId, 99, "x.bin"); waitIdle();
        QVERIFY2(mail.attachStatus().contains("introuvable"), qPrintable(mail.attachStatus()));
        mail.saveAttachmentToServer("nope@example.org", part, "x.bin"); waitIdle();
        QVERIFY2(mail.attachStatus().contains("notmuch"), qPrintable(mail.attachStatus()));
        QCOMPARE(text("ls ~/Downloads | wc -l\n"), QStringLiteral("3"));
        QCOMPARE(text("ls -A ~/Downloads | grep -c fortiq-part\n"), QStringLiteral("0"));                          // no temporary leftovers

        // ---- to this computer: an ordinary download task, then the temporary copy is removed ----
        QTemporaryDir local;
        QVERIFY(local.isValid());
        const QString target = local.filePath("received.pdf");
        const int tasksBefore = controller.tasks()->count();
        mail.downloadAttachment(messageId, part, name, QUrl::fromLocalFile(target));
        QTRY_VERIFY_WITH_TIMEOUT(controller.tasks()->count() >= tasksBefore + 2, 30000);     // the extraction task and the download task
        QTRY_VERIFY_WITH_TIMEOUT(controller.tasks()->activeCount() == 0, 30000);
        QCOMPARE(controller.tasks()->tasks().constLast().toMap().value("kind").toString(), QStringLiteral("download"));
        QCOMPARE(controller.tasks()->tasks().constLast().toMap().value("state").toString(), QStringLiteral("succeeded"));
        QFile received(target);
        QVERIFY(received.open(QIODevice::ReadOnly));
        QCOMPARE(received.readAll(), QByteArray("%PDF-1.4\n% fake\n"));
        QTRY_COMPARE_WITH_TIMEOUT(text("ls ~/.cache/fortiq-mail 2>/dev/null | grep -c att.\n"), QStringLiteral("0"), 15000);   // the temporary copy is gone
        // Downloading onto an existing file is a conflict, not an overwrite; the temporary copy is still removed.
        mail.downloadAttachment(messageId, part, name, QUrl::fromLocalFile(target));
        QTRY_VERIFY_WITH_TIMEOUT(controller.tasks()->tasks().constLast().toMap().value("kind").toString() == "download", 30000);
        QTRY_VERIFY_WITH_TIMEOUT(controller.tasks()->tasks().constLast().toMap().value("decision").toString() == "conflict"
                                 || controller.tasks()->tasks().constLast().toMap().value("active").toBool() == false, 30000);
        const int conflictId = controller.tasks()->tasks().constLast().toMap().value("id").toInt();
        if (controller.tasks()->task(conflictId).value("decision").toString() == "conflict") controller.resolveConflict(conflictId, "skip");
        QTRY_VERIFY_WITH_TIMEOUT(controller.tasks()->activeCount() == 0, 30000);
        QTRY_COMPARE_WITH_TIMEOUT(text("ls ~/.cache/fortiq-mail 2>/dev/null | grep -c att.\n"), QStringLiteral("0"), 15000);
        received.close();
        QVERIFY(received.open(QIODevice::ReadOnly));
        QCOMPARE(received.readAll(), QByteArray("%PDF-1.4\n% fake\n"));                                           // unchanged

        controller.disconnectServer();
        QVERIFY(mail.attachStatus().isEmpty());
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        shell("cd ~ && rm -rf Downloads .cache/fortiq-mail\n");
        settings.remove(pin); settings.remove("mail/notmuchConfig");
    }

    // ---- Audit 23.10.2026 (docs/AUDIT_2026-10-23.md) ----
    // Adversarial corpus against the real clients: whatever the scanner lets through must not change anything, even though the test role OWNS the tables.
    // Needs FORTIQ_IT_DB=1 and the PostgreSQL cluster of the test Debian.
    void databaseAdversarialCorpus() {
        if (qEnvironmentVariable("FORTIQ_IT_DB").isEmpty()) QSKIP("FORTIQ_IT_DB not set");
        fortiq::RemoteController controller;
        QSettings settings;
        settings.remove("servers/profiles"); settings.remove("db/profiles");
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        const QString serverId = controller.saveServer({ { "name", "Adversarial" }, { "host", base.host }, { "port", base.port }, { "user", base.user }, { "keyPath", key } });
        controller.connectTo(serverId, key.isEmpty() ? base.password : QString());
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        auto shell = [&](const QByteArray &script) {
            fortiq::SshSession session;
            const SshResult opened = session.open(controller.connectionRequest());
            if (opened.status != SshResult::Status::Ok) return opened;
            return session.runScript(script, 262144, {});
        };
        auto text = [&](const QByteArray &script) { return QString::fromUtf8(shell(script).data).trimmed(); };
        QCOMPARE(text("rm -f /tmp/fq_adv*; sqlite3 /tmp/fq_adv.db \"create table t(a); insert into t values (1),(2);\"; echo ready\n"), QStringLiteral("ready"));
        const QByteArray pgSnapshot = "PGPASSWORD=fqdb-test-pw psql -h 127.0.0.1 -U fqdb -d fqtest -tAc \"select (select md5(string_agg(p::text, '|' order by id)) from people p) || '/' || "
                                      "(select count(*) from pg_tables where schemaname='public') || '/' || (select last_value from people_id_seq) || '/' || (select count(*) from pg_roles)\"\n";
        const QByteArray sqliteSnapshot = "sha256sum /tmp/fq_adv.db | cut -d' ' -f1; ls /tmp | grep -c '^fq_adv'\n";
        const QString pgBefore = text(pgSnapshot), sqliteBefore = text(sqliteSnapshot);
        QVERIFY2(pgBefore.contains('/') && pgBefore.size() > 10, qPrintable(pgBefore));

        fortiq::DbController db(&controller);
        struct Attack { const char *sql; const char *expect; };   // refuse: never sent | fail: sent and rejected | ok: harmless | any: only the end state matters
        auto runCorpus = [&](const QList<Attack> &corpus) {
            for (const Attack &a : corpus) {
                const int tasksBefore = controller.tasks()->count();
                db.runQuery(QString::fromUtf8(a.sql));
                if (db.busy()) QTRY_VERIFY_WITH_TIMEOUT(!db.busy(), 30000);
                const QString expect = QString::fromLatin1(a.expect);
                const bool sent = controller.tasks()->count() > tasksBefore;
                if (expect == "refuse") { QVERIFY2(!sent && !db.error().isEmpty(), a.sql); }
                else if (expect == "fail") { QVERIFY2(sent && !db.error().isEmpty(), qPrintable(QString::fromUtf8(a.sql) + " -> " + db.error())); }
                else if (expect == "ok") { QVERIFY2(sent && db.error().isEmpty(), qPrintable(QString::fromUtf8(a.sql) + " -> " + db.error())); }
            }
        };

        QVERIFY(db.saveProfile({ { "name", "PG" }, { "engine", "postgres" }, { "host", "127.0.0.1" }, { "port", 5432 }, { "user", "fqdb" }, { "database", "fqtest" } },
                               QStringLiteral("fqdb-test-pw"), false).isEmpty());
        runCorpus({
            { "insert into people(name) values ('x')", "refuse" }, { "update people set name='x'", "refuse" }, { "delete from people", "refuse" }, { "drop table people", "refuse" },
            { "truncate people", "refuse" }, { "create table adv(a int)", "refuse" }, { "alter table people add column z int", "refuse" }, { "begin; delete from people; commit", "refuse" },
            { "select 1; delete from people", "refuse" }, { "\\! touch /tmp/fq_adv_pg", "refuse" }, { "select 1 \\gexec", "refuse" }, { "copy people to program 'touch /tmp/fq_adv_pg'", "refuse" },
            { "do $$ begin delete from people; end $$", "refuse" }, { "call nosuch()", "refuse" }, { "set default_transaction_read_only = off", "refuse" }, { "reset all", "refuse" },
            { "vacuum people", "refuse" }, { "grant all on people to public", "refuse" }, { "lock table people", "refuse" }, { "select pg_read_file('/etc/hostname')", "refuse" },
            { "select lo_import('/etc/hostname')", "refuse" }, { "select pg_terminate_backend(pg_backend_pid())", "refuse" }, { "select set_config('default_transaction_read_only','off',false)", "refuse" },
            { "values (1); drop table people", "refuse" }, { "/**/delete from people", "refuse" }, { "(delete from people)", "refuse" }, { "  \n--c\n insert into people(name) values('x')", "refuse" },
            { "listen x", "refuse" }, { "notify x", "refuse" }, { "savepoint a", "refuse" }, { "prepare p as delete from people", "refuse" }, { "execute p", "refuse" },
            { "declare c cursor for select 1", "refuse" }, { "create role adv_role", "refuse" }, { "select 1 \\g /tmp/fq_adv_pg", "refuse" }, { "select 'unterminated", "refuse" },
            { "with x as (delete from people returning *) select * from x", "fail" }, { "with x as (update people set name='z' returning *) select * from x", "fail" },
            { "with x as (insert into people(name) values ('h') returning *) select * from x", "fail" }, { "select * into adv_new from people", "fail" },
            { "explain analyze insert into people(name) values ('x')", "fail" }, { "explain analyze delete from people", "fail" }, { "select nextval('people_id_seq')", "fail" },
            { "select setval('people_id_seq', 999)", "fail" }, { "select * from people for update", "fail" }, { "select txid_current()", "any" },
            { "select count(*) from people", "ok" }, { "show server_version", "ok" }, { "table people", "ok" }, { "values (1),(2)", "ok" }, { "explain select * from people", "ok" },
            { "select pg_sleep(0)", "ok" }, { "select 'insert into people' as s", "ok" }, { "select 1 -- ; delete from people", "ok" }, { "select $$ ; delete from people $$", "ok" },
            { "select E'\\'; delete from people; --' as s", "ok" }, { "select '\\'; delete from people; --'", "any" }, { "select \"a\" from (select 1 as \"a\") q", "ok" } });
        QCOMPARE(text(pgSnapshot), pgBefore);                                              // not a row, table, role or sequence value changed
        QCOMPARE(text("ls /tmp | grep -c '^fq_adv_pg'\n"), QStringLiteral("0"));         // no command ran on the server

        QVERIFY(db.saveProfile({ { "name", "SQLite" }, { "engine", "sqlite" }, { "path", "/tmp/fq_adv.db" } }, QString(), false).isEmpty());
        runCorpus({
            { "insert into t values (9)", "refuse" }, { "delete from t", "refuse" }, { "drop table t", "refuse" }, { "create table n(a)", "refuse" }, { "vacuum", "refuse" }, { "reindex", "refuse" },
            { "replace into t values (1)", "refuse" }, { "attach database '/tmp/fq_adv_x.db' as x", "refuse" }, { ".shell touch /tmp/fq_adv_sq", "refuse" },
            { "select 1\n.shell touch /tmp/fq_adv_sq", "refuse" }, { ".read /etc/passwd", "refuse" }, { "select writefile('/tmp/fq_adv_sq', 'x')", "refuse" },
            { "select edit('x', 'touch /tmp/fq_adv_sq')", "refuse" }, { "select readfile('/etc/hostname')", "refuse" }, { "select load_extension('/lib/x86_64-linux-gnu/libm.so.6')", "refuse" },
            { "with c as (select 1) select readfile('/etc/passwd') from c", "refuse" }, { "select name, data from fsdir('/etc')", "refuse" }, { "begin", "refuse" },
            { "with x as (select 1) delete from t", "fail" }, { "with x as (select 1) insert into t select * from x", "fail" }, { "pragma user_version = 7", "any" },
            { "pragma journal_mode = wal", "fail" }, { "pragma writable_schema = 1", "any" }, { "pragma temp_store_directory = '/tmp'", "any" },
            { "select * from t", "ok" }, { "pragma table_info(t)", "ok" }, { "explain select 1", "ok" }, { "select 'a;b' as s", "ok" }, { "select '.shell touch /tmp/fq_adv_sq' as s", "ok" },
            { "select 1 -- .shell touch /tmp/fq_adv_sq", "ok" } });
        QCOMPARE(text(sqliteSnapshot), sqliteBefore);                                       // same bytes, no new file next to it
        QCOMPARE(text("ls /tmp | grep -c '^fq_adv_sq\\|^fq_adv_x'\n"), QStringLiteral("0"));

        // The shell without safe mode is refused, not trusted: simulate a sqlite3 that rejects -safe.
        QCOMPARE(text("mkdir -p /tmp/fq_adv_bin && printf '#!/bin/sh\\nfor a in \"$@\"; do [ \"$a\" = -safe ] && exit 1; done; exec /usr/bin/sqlite3 \"$@\"\\n' > /tmp/fq_adv_bin/sqlite3 && chmod +x /tmp/fq_adv_bin/sqlite3 && echo done\n"), QStringLiteral("done"));
        {
            const QByteArray script = fortiq::buildSqliteScript(fortiq::profileFromVariant({ { "engine", "sqlite" }, { "path", "/tmp/fq_adv.db" } }), "select 1", { 5, 10 }, "FQDELIMX");
            const SshResult r = shell("PATH=/tmp/fq_adv_bin:$PATH\nexport PATH\n" + script);
            QVERIFY2(r.status == SshResult::Status::Ok && r.code == "exit:11" && r.message.contains("old-sqlite") && r.data.isEmpty(), qPrintable(r.code + r.message));
        }
        shell("rm -rf /tmp/fq_adv_bin /tmp/fq_adv.db\n");
        controller.removeServer(serverId);
        settings.remove(pin); settings.remove("servers/profiles"); settings.remove("db/profiles");
    }

    // Reliability invariants under session changes and failures (disposable test environment only).
    // Needs FORTIQ_IT_DB, FORTIQ_IT_MAIL and FORTIQ_IT_MAILSEND (PostgreSQL, notmuch, SMTP sink of tool/setup_mail_live_fixture.sh).
    void reliabilityInvariants() {
        if (qEnvironmentVariable("FORTIQ_IT_DB").isEmpty() || qEnvironmentVariable("FORTIQ_IT_MAIL").isEmpty() || qEnvironmentVariable("FORTIQ_IT_MAILSEND").isEmpty())
            QSKIP("FORTIQ_IT_DB, FORTIQ_IT_MAIL and FORTIQ_IT_MAILSEND are all needed");
        fortiq::RemoteController controller;
        QSettings settings;
        settings.remove("servers/profiles"); settings.remove("db/profiles"); settings.remove("mail/notmuchConfig"); settings.remove("mail/displayName");
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        const QString serverA = controller.saveServer({ { "name", "A" }, { "host", base.host }, { "port", base.port }, { "user", base.user }, { "keyPath", key } });
        const QString serverB = controller.saveServer({ { "name", "B" }, { "host", base.host }, { "port", base.port }, { "user", base.user }, { "keyPath", key } });
        QVERIFY(!serverA.isEmpty() && !serverB.isEmpty() && serverA != serverB);
        auto connectTo = [&](const QString &id) {
            controller.connectTo(id, key.isEmpty() ? base.password : QString());
            QTRY_VERIFY_WITH_TIMEOUT(controller.connected() && controller.activeServerId() == id, 15000);
        };
        auto shell = [&](const QByteArray &script) {
            fortiq::SshSession session;
            const SshResult opened = session.open(controller.connectionRequest());
            if (opened.status != SshResult::Status::Ok) return opened;
            return session.runScript(script, 262144, {});
        };
        auto text = [&](const QByteArray &script) { return QString::fromUtf8(shell(script).data).trimmed(); };
        auto lastTaskState = [&] { return controller.tasks()->tasks().constLast().toMap().value("state").toString(); };
        connectTo(serverA);
        QFile fixtureScript(QString::fromUtf8(QT_TESTCASE_SOURCEDIR) + "/tool/make_mail_fixture.sh");
        QVERIFY(fixtureScript.open(QIODevice::ReadOnly));
        QVERIFY(shell("bash -s -- /tmp/fq_mail_out <<'FQ_FIXTURE_END'\n" + fixtureScript.readAll().replace("\r\n", "\n") + "\nFQ_FIXTURE_END\n").code == "exit:0");
        const QString home = text("printf '%s' \"$HOME\"\n");
        const QString mailConfig = home + "/mailfix/nm.conf";
        // A msmtp account for the SMTP sink (the wizard has its own test); CRAM-MD5 because msmtp refuses plain methods without TLS.
        QVERIFY(shell("rm -rf ~/sinkout/*.eml ~/sinkout/*.env ~/sinkout/delay; umask 077; printf 'account live\\nhost 127.0.0.1\\nport 2525\\nprotocol smtp\\nauth cram-md5\\nuser fqmail\\npassword \"Zq\"7 #x\\\\y$z;w\"\\nfrom me@example.net\\ntls off\\n' > ~/.msmtprc\n").code == "exit:0");
        const QString dbPassword = QStringLiteral("fqdb-test-pw");

        // ===== 1. A late reply never lands in a newer session (database) =====
        fortiq::DbController db(&controller);
        QVERIFY(db.saveProfile({ { "name", "PG" }, { "engine", "postgres" }, { "host", "127.0.0.1" }, { "port", 5432 }, { "user", "fqdb" }, { "database", "fqtest" } }, dbPassword, false).isEmpty());
        const QString profileId = db.selectedId();
        db.runQuery("select pg_sleep(3), 'LATE' as marker");
        QTRY_VERIFY_WITH_TIMEOUT(db.busy(), 5000);
        controller.disconnectServer();                                  // the session ends while the query runs
        QVERIFY(!db.busy());
        QVERIFY2(lastTaskState() == "cancelled", qPrintable(lastTaskState()));   // never "succeeded": nobody saw the result
        connectTo(serverB);                                             // another server profile, a newer generation
        QTest::qWait(4500);                                             // the stale reply arrives now
        QVERIFY(db.rows().isEmpty() && db.columns().isEmpty() && db.error().isEmpty());
        QVERIFY(db.profiles().isEmpty());                               // the profile belongs to server A only
        QVERIFY(db.selectedId().isEmpty());
        QVERIFY2(lastTaskState() == "cancelled", qPrintable(lastTaskState()));
        // ... and server A's state comes back intact after reconnecting.
        controller.disconnectServer();
        connectTo(serverA);
        QCOMPARE(db.profiles().size(), 1);
        db.select(profileId);
        QTRY_VERIFY_WITH_TIMEOUT(!db.busy(), 20000);                    // the schema read of the selection
        db.runQuery("select 'AFTER' as marker");
        QTRY_VERIFY_WITH_TIMEOUT(!db.busy() && !db.rows().isEmpty(), 20000);
        QCOMPARE(db.rows().first().toList().first().toString(), QStringLiteral("AFTER"));

        // ===== 2. The same for System, Network and Mail reads =====
        {
            fortiq::SystemController sys(&controller);
            fortiq::NetworkController net(&controller);
            fortiq::MailController mail(&controller);
            sys.setWindowActive(true); net.setWindowActive(true); mail.setConfigPath(mailConfig); mail.setWindowActive(true);
            QTRY_VERIFY_WITH_TIMEOUT(sys.busy() || sys.state() == "ready", 5000);
            controller.disconnectServer();
            connectTo(serverB);
            QTest::qWait(3500);
            // The windows are active, so they reload for server B; only that read may fill them.
            QTRY_VERIFY_WITH_TIMEOUT(sys.state() == "ready" && net.state() == "ready" && mail.state() == "ready", 30000);   // the windows are active: they reload for B
            controller.disconnectServer();
            QCOMPARE(sys.state(), QStringLiteral("disconnected")); QCOMPARE(net.state(), QStringLiteral("disconnected")); QCOMPARE(mail.state(), QStringLiteral("disconnected"));
            QVERIFY(sys.processes().isEmpty() && sys.services().isEmpty() && net.interfaces().isEmpty() && mail.threads().isEmpty() && mail.folders().isEmpty() && mail.messages().isEmpty());
            QTest::qWait(1500);
            QVERIFY(sys.processes().isEmpty() && net.interfaces().isEmpty() && mail.threads().isEmpty());           // nothing arrives late into a closed session
            connectTo(serverA);
            QTRY_VERIFY_WITH_TIMEOUT(mail.state() == "ready" && !mail.threads().isEmpty() && !mail.accounts().isEmpty(), 30000);
            sys.setWindowActive(false); net.setWindowActive(false);

            // ===== 3. An interrupted send is "unknown", never "sent" and never "failed", and nothing is retried =====
            QVERIFY(shell("echo 4 > ~/sinkout/delay\n").code == "exit:0");   // the sink stores the message but acknowledges 4 s late
            QVERIFY2(mail.send({ { "account", "live" }, { "to", "you@example.org" }, { "subject", "Reliability" }, { "body", "x" } }).isEmpty(), "send");
            QTRY_VERIFY_WITH_TIMEOUT(mail.sendState() == "sending", 5000);
            QTest::qWait(1200);                                              // the connection to msmtp is up and the message is in flight
            controller.disconnectServer();
            QCOMPARE(mail.sendState(), QStringLiteral("unknown"));
            QVERIFY2(mail.sendStatus().contains("ne sait pas"), qPrintable(mail.sendStatus()));
            QCOMPARE(controller.tasks()->tasks().constLast().toMap().value("kind").toString(), QStringLiteral("mailSend"));
            QCOMPARE(lastTaskState(), QStringLiteral("unknown"));
            connectTo(serverA);
            QTest::qWait(6500);
            QCOMPARE(mail.sendState(), QStringLiteral("unknown"));         // a later reply or a reconnection does not rewrite history
            QCOMPARE(lastTaskState(), QStringLiteral("unknown"));
            const QString delivered = text("ls ~/sinkout | grep -c eml\n");
            qInfo() << "interrupted send: the sink received" << delivered << "message(s); the client reported unknown";
            QVERIFY2(delivered == "0" || delivered == "1", qPrintable(delivered));                                    // never more than once
            QTest::qWait(3000);
            QCOMPARE(text("ls ~/sinkout | grep -c eml\n"), delivered);                                               // no automatic resend
            mail.resetSend();
            QCOMPARE(mail.sendState(), QStringLiteral("idle"));
            shell("rm -f ~/sinkout/delay\n");

            // ===== 4. A failed mutation changes nothing locally and says so =====
            QTRY_VERIFY_WITH_TIMEOUT(mail.state() == "ready" && !mail.threads().isEmpty(), 30000);
            const QVariantMap thread = mail.threads().first().toMap();
            const QString threadId = thread.value("id").toString();
            const QStringList tagsBefore = thread.value("tags").toStringList();
            auto folderCount = [&](const QString &k) { for (const QVariant &f : mail.folders()) if (f.toMap().value("key").toString() == k) return f.toMap().value("count").toInt(); return -2; };
            const int flaggedBefore = folderCount("flagged");
            QVERIFY(shell("chmod -R a-w ~/mailfix/.notmuch\n").code == "exit:0");   // the index becomes read-only: tagging must fail
            mail.setTag(threadId, "flagged", !tagsBefore.contains("flagged"));
            QTRY_VERIFY_WITH_TIMEOUT(!mail.busy(), 20000);
            QVERIFY2(mail.status().startsWith(QString::fromUtf8("\xC3\x89tiquette non modifi\xC3\xA9""e")), qPrintable(mail.status()));
            QCOMPARE(mail.threads().first().toMap().value("tags").toStringList(), tagsBefore);
            QCOMPARE(folderCount("flagged"), flaggedBefore);
            QVERIFY(shell("chmod -R u+w ~/mailfix/.notmuch\n").code == "exit:0");
            mail.setTag(threadId, "flagged", !tagsBefore.contains("flagged"));
            QTRY_VERIFY_WITH_TIMEOUT(mail.threads().first().toMap().value("tags").toStringList().contains("flagged") != tagsBefore.contains("flagged"), 20000);   // works again
            mail.setTag(threadId, "flagged", tagsBefore.contains("flagged"));
            QTRY_VERIFY_WITH_TIMEOUT(!mail.busy(), 20000);
        }

        // ===== 5. Secrets never reach the client settings in clear =====
        for (const QString &k : settings.allKeys()) {
            const QString value = settings.value(k).toString() + QString::fromUtf8(settings.value(k).toByteArray());
            QVERIFY2(!value.contains(dbPassword) && !value.contains(base.password) && !value.contains("Zq\"7"), qPrintable(k));
        }
        QVERIFY(fortiq::SecretStore::load("db-" + profileId).ok);   // it is in the protected store...
        shell("rm -f ~/.msmtprc\n");
        controller.removeServer(serverA);                           // ...and leaves with the server
        QVERIFY(!fortiq::SecretStore::load("db-" + profileId).ok);
        controller.disconnectServer();
        settings.remove(pin); settings.remove("servers/profiles"); settings.remove("db/profiles"); settings.remove("mail/notmuchConfig"); settings.remove("mail/displayName");
    }

    // Connection state at a drop: every app notices a dead transport, the active server is marked lost with a French
    // sentence, and the next refresh opens a new connection by itself and clears the mark.
    void aDroppedConnectionIsNoticedAndRecovered() {
        fortiq::RemoteController controller;
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        QVERIFY(!controller.linkLost());
        QVERIFY(controller.linkText().isEmpty());

        fortiq::SystemController system(&controller);
        system.reload();
        QTRY_VERIFY_WITH_TIMEOUT(system.state() == "ready", 30000);
        QVERIFY(!controller.linkLost());

        // The server ends every login session of this user except the one that gives the order.
        fortiq::SshSession shellSession;
        QVERIFY(shellSession.open(controller.connectionRequest()).status == SshResult::Status::Ok);
        const SshResult killed = shellSession.runScript(
            "me=$PPID\nfor p in $(pgrep -u \"$(id -u)\" -f 'sshd-session: '); do [ \"$p\" != \"$me\" ] && kill -9 \"$p\"; done\nsleep 1\necho done\n", 65536, {});
        QVERIFY2(killed.status == SshResult::Status::Ok && killed.data.contains("done"), qPrintable(killed.message));

        QSignalSpy linkSpy(&controller, &fortiq::RemoteController::linkChanged);
        system.reload();
        QTRY_VERIFY_WITH_TIMEOUT(controller.linkLost(), 30000);
        QVERIFY(linkSpy.count() >= 1);
        QVERIFY2(controller.linkText().contains("Connexion perdue"), qPrintable(controller.linkText()));
        QVERIFY(QRegularExpression("^\\d\\d:\\d\\d$").match(controller.linkLostAt()).hasMatch());
        QTRY_COMPARE_WITH_TIMEOUT(system.state(), QStringLiteral("error"), 30000);   // the failed refresh is reported, not hidden
        QVERIFY2(system.status().contains("perdue"), qPrintable(system.status()));        // in French
        QVERIFY(controller.connected());                                    // the server stays selected: this is a link problem, not a logout

        // The next refresh reconnects on its own (host pin checked again) and the mark disappears.
        system.reload();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.linkLost(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(system.state() == "ready", 30000);
        QVERIFY(controller.linkText().isEmpty());

        // Leaving on purpose is not a loss.
        controller.disconnectServer();
        QTest::qWait(100);
        QVERIFY(!controller.linkLost());
    }

    // Audit backlog 1: Logs, MDok, Disks and Terminal report their work to the TaskManager, with the right guarding and settling rules.
    void everyAppReportsItsWorkToTheTaskManager() {
        fortiq::RemoteController controller;
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        fortiq::TaskManager *tm = controller.tasks();
        QStringList seenKinds;                                   // every kind that was ever listed, even if it vanished (transient reads)
        QObject::connect(tm, &fortiq::TaskManager::tasksChanged, [&] { for (const QVariant &t : tm->tasks()) if (!seenKinds.contains(t.toMap().value("kind").toString())) seenKinds << t.toMap().value("kind").toString(); });
        auto kindState = [&](const QString &kind) { QString s; for (const QVariant &t : tm->tasks()) if (t.toMap().value("kind").toString() == kind) s = t.toMap().value("state").toString(); return s; };
        auto kindId = [&](const QString &kind) { int id = 0; for (const QVariant &t : tm->tasks()) if (t.toMap().value("kind").toString() == kind) id = t.toMap().value("id").toInt(); return id; };
        fortiq::SshSession shellSession;
        QVERIFY(shellSession.open(controller.connectionRequest()).status == SshResult::Status::Ok);
        auto shell = [&](const QByteArray &script) { return shellSession.runScript(script, 65536, {}); };
        QString dir = QString::fromUtf8(shell("printf '%s' \"$HOME\"\n").data) + "/.fortiq-task-test";
        QVERIFY(shell("rm -rf '" + dir.toUtf8() + "'; mkdir -p '" + dir.toUtf8() + "'; printf '# Titre\\ntexte\\n' > '" + dir.toUtf8() + "/note.md'\n").code == "exit:0");

        // ---- Disks: a transient read, cancellable, silent once done ----
        {
            fortiq::DisksController disks(&controller);
            disks.setWindowActive(true);
            QTRY_VERIFY_WITH_TIMEOUT(disks.state() == "ready" || disks.state() == "partial" || disks.state() == "error", 30000);
            QVERIFY(seenKinds.contains("disksRead"));
            QCOMPARE(kindState("disksRead"), QString());         // success leaves no line behind
            QCOMPARE(tm->activeCount(), 0);
            disks.setWindowActive(false);
        }
        // ---- Logs: transient reads and the live follow as one cancellable task ----
        {
            fortiq::LogsController logs(&controller);
            logs.loadSources();
            logs.select("system");
            QTRY_VERIFY_WITH_TIMEOUT(!logs.busy(), 40000);
            QTRY_COMPARE_WITH_TIMEOUT(tm->activeCount(), 0, 20000);
            QVERIFY(seenKinds.contains("logsLoad") && seenKinds.contains("logsSources"));
            logs.setFollowing(true);
            QVERIFY(logs.following());
            QCOMPARE(kindState("logsFollow"), QStringLiteral("running"));
            QCOMPARE(tm->guardingCount(), 0);                    // following does not guard the session
            QVERIFY(tm->requestCancel(kindId("logsFollow")));    // the task panel's stop button stops the follow
            QTRY_VERIFY(!logs.following());
            QCOMPARE(kindState("logsFollow"), QStringLiteral("cancelled"));
            logs.setFollowing(true);
            QCOMPARE(kindState("logsFollow"), QStringLiteral("running"));
            logs.setFollowing(false);                            // switching it off in the window ends the task too
            QTRY_COMPARE(tm->activeCount(), 0);
        }
        tm->clearFinished();

        // ---- MDok: open is a transient read, save is a guarding write with honest outcomes ----
        {
            fortiq::DocumentsController docs(&controller);
            docs.open(dir + "/note.md");
            QTRY_VERIFY_WITH_TIMEOUT(docs.hasDocument() && !docs.busy(), 20000);
            QVERIFY(seenKinds.contains("docOpen"));
            QCOMPARE(kindState("docOpen"), QString());
            docs.setText(QStringLiteral("# Titre\nmodifi\xC3\xA9\n"));
            docs.save();
            QCOMPARE(kindState("docSave"), QStringLiteral("running"));
            QCOMPARE(tm->guardingCount(), 1);                    // a save running when the user logs out must be confirmed
            QTRY_VERIFY_WITH_TIMEOUT(!docs.busy(), 20000);
            QCOMPARE(kindState("docSave"), QStringLiteral("succeeded"));
            QCOMPARE(tm->guardingCount(), 0);
            QCOMPARE(QString::fromUtf8(shell("cat '" + dir.toUtf8() + "/note.md'\n").data), QString::fromUtf8("# Titre\nmodifi\xC3\xA9\n"));
            // A concurrent change on the server makes the next save a conflict: failed, with a code, never "succeeded".
            QVERIFY(shell("printf '# Autre\\n' > '" + dir.toUtf8() + "/note.md'\n").code == "exit:0");
            docs.setText(QStringLiteral("# Titre\nencore\n"));
            docs.save();
            QTRY_VERIFY_WITH_TIMEOUT(!docs.busy(), 20000);
            const QVariantMap conflict = tm->task(kindId("docSave")).isEmpty() ? QVariantMap() : tm->tasks().constLast().toMap();
            QCOMPARE(conflict.value("state").toString(), QStringLiteral("failed"));
            QCOMPARE(conflict.value("code").toString(), QStringLiteral("conflict"));
            QCOMPARE(QString::fromUtf8(shell("cat '" + dir.toUtf8() + "/note.md'\n").data), QStringLiteral("# Autre\n"));   // the other version is untouched
            // Save-as copy is its own kind.
            docs.saveAs(dir + "/copie.md");
            QTRY_VERIFY_WITH_TIMEOUT(!docs.busy(), 20000);
            QVERIFY(seenKinds.contains("docSaveCopy"));
        }
        tm->clearFinished();

        // ---- Terminal: the shell session is a task that guards the session; closing it is not a failure ----
        {
            fortiq::TerminalController term(&controller);
            term.setWindowActive(true);
            QTRY_VERIFY_WITH_TIMEOUT(term.running(), 20000);
            QCOMPARE(kindState("terminal"), QStringLiteral("running"));
            QCOMPARE(tm->guardingCount(), 1);                    // logging out would end the shell: ask first
            const QVariantMap running = tm->tasks().constLast().toMap();
            QVERIFY(running.value("guards").toBool() && running.value("session").toBool() && running.value("cancellable").toBool());
            term.stop();
            QVERIFY(!term.running());
            QCOMPARE(kindState("terminal"), QStringLiteral("succeeded"));
            QCOMPARE(tm->guardingCount(), 0);
            // Cancelling from the task panel closes the shell and is recorded as cancelled.
            term.start();
            QTRY_VERIFY_WITH_TIMEOUT(term.running(), 20000);
            QVERIFY(tm->requestCancel(kindId("terminal") == 0 ? 0 : tm->tasks().constLast().toMap().value("id").toInt()));
            QTRY_VERIFY(!term.running());
            QCOMPARE(tm->tasks().constLast().toMap().value("state").toString(), QStringLiteral("cancelled"));
            // A shell that exits on its own is a success of the task.
            term.start();
            QTRY_VERIFY_WITH_TIMEOUT(term.running(), 20000);
            term.send("exit\n");
            QTRY_VERIFY_WITH_TIMEOUT(!term.running(), 20000);
            QCOMPARE(tm->tasks().constLast().toMap().value("state").toString(), QStringLiteral("succeeded"));
            // A dropped connection is a cancel (a view has nothing to verify), never "unknown".
            term.start();
            QTRY_VERIFY_WITH_TIMEOUT(term.running(), 20000);
            controller.disconnectServer();
            QCOMPARE(tm->tasks().constLast().toMap().value("state").toString(), QStringLiteral("cancelled"));
            QCOMPARE(tm->activeCount(), 0);
        }
        // Everything ended: no task is active and the guard has nothing to ask about.
        QCOMPARE(tm->activeCount(), 0);
        QCOMPARE(tm->guardingCount(), 0);
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        fortiq::SshSession cleanup;
        if (cleanup.open(controller.connectionRequest()).status == SshResult::Status::Ok) cleanup.runScript("rm -rf '" + dir.toUtf8() + "'\n", 4096, {});
        settings.remove(pin);
    }

    // Audit R4: what the connected database role may do is checked when a profile is opened and said plainly.
    // Needs FORTIQ_IT_DB=1 and tool/setup_db_roles_fixture.sh run as root on the DISPOSABLE Debian (roles fqro, fqfile, fqwfile, fqsu).
    void databaseRoleCheck() {
        if (qEnvironmentVariable("FORTIQ_IT_DB").isEmpty()) QSKIP("FORTIQ_IT_DB not set");
        fortiq::RemoteController controller;
        QSettings settings;
        settings.remove("servers/profiles"); settings.remove("db/profiles");
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        const QString serverId = controller.saveServer({ { "name", "Roles" }, { "host", base.host }, { "port", base.port }, { "user", base.user }, { "keyPath", key } });
        controller.connectTo(serverId, key.isEmpty() ? base.password : QString());
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        fortiq::SshSession shellSession;
        QVERIFY(shellSession.open(controller.connectionRequest()).status == SshResult::Status::Ok);
        auto shell = [&](const QByteArray &script) { return shellSession.runScript(script, 65536, {}); };
        fortiq::DbController db(&controller);

        // Opening a profile (the window does saveProfile + loadSchema; select() does the same) runs the check by itself.
        auto open = [&](const QString &user, const QString &password) -> QVariantMap {
            const QString saved = db.saveProfile({ { "name", user }, { "engine", "postgres" }, { "host", "127.0.0.1" }, { "port", 5432 }, { "user", user }, { "database", "fqtest" } }, password, false);
            if (!saved.isEmpty()) return QVariantMap { { "level", "SAVE-ERROR: " + saved } };
            db.loadSchema();
            for (int i = 0; i < 300 && (db.busy() || db.roleCheck().isEmpty()); ++i) QTest::qWait(100);
            return db.roleCheck();
        };
        auto findings = [](const QVariantMap &r) { return r.value("findings").toStringList().join("\n"); };

        // The role that owns the tables can write to all of them.
        QVariantMap owner = open("fqdb", "fqdb-test-pw");
        QCOMPARE(owner.value("level").toString(), QStringLiteral("warning"));
        QVERIFY2(findings(owner).contains("Droit d'écriture"), qPrintable(findings(owner)));
        QVERIFY(owner.value("recommendation").toString().contains("CREATE ROLE fortiq_ro"));
        QVERIFY(owner.value("recommendation").toString().contains("GRANT CONNECT ON DATABASE \"fqtest\""));
        // The recommended role: nothing to warn about.
        QVariantMap ro = open("fqro", "fqro-test-pw");
        QCOMPARE(ro.value("level").toString(), QStringLiteral("ok"));
        QVERIFY(ro.value("findings").toStringList().isEmpty() && ro.value("recommendation").toString().isEmpty());
        // File and program access are named.
        QVariantMap fileRole = open("fqfile", "fqfile-test-pw");
        QCOMPARE(fileRole.value("level").toString(), QStringLiteral("warning"));
        QVERIFY2(findings(fileRole).contains("pg_read_server_files"), qPrintable(findings(fileRole)));
        QVariantMap writeFile = open("fqwfile", "fqwfile-test-pw");
        QCOMPARE(writeFile.value("level").toString(), QStringLiteral("danger"));
        QVERIFY2(findings(writeFile).contains("pg_write_server_files"), qPrintable(findings(writeFile)));
        QVariantMap superuser = open("fqsu", "fqsu-test-pw");
        QCOMPARE(superuser.value("level").toString(), QStringLiteral("danger"));
        QVERIFY2(findings(superuser).contains("Superutilisateur"), qPrintable(findings(superuser)));
        QVERIFY(superuser.value("summary").toString().contains("très étendus"));

        // The check never gets in the way: the profile stays usable, and the check itself changed nothing.
        db.runQuery("select current_user");
        QTRY_VERIFY_WITH_TIMEOUT(!db.busy(), 20000);
        QVERIFY2(db.error().isEmpty(), qPrintable(db.error()));
        QCOMPARE(db.rows().first().toList().first().toString(), QStringLiteral("fqsu"));
        // Role-level protection, no FORTIQ involved: the read-only role cannot write even with a plain, writable session.
        const QString denied = QString::fromUtf8(shell("PGPASSWORD=fqro-test-pw psql -h 127.0.0.1 -U fqro -d fqtest -c \"insert into people(name) values ('x')\" 2>&1\n").data);
        QVERIFY2(denied.contains("permission denied"), qPrintable(denied));
        // A wrong password is not a "check failed" banner: the schema read fails first and says why.
        QVERIFY(db.saveProfile({ { "name", "bad" }, { "engine", "postgres" }, { "host", "127.0.0.1" }, { "port", 5432 }, { "user", "fqro" }, { "database", "fqtest" } }, "wrong", false).isEmpty());
        db.loadSchema();
        QTRY_VERIFY_WITH_TIMEOUT(!db.busy(), 30000);
        QVERIFY2(db.schemaStatus().contains("Authentification"), qPrintable(db.schemaStatus()));
        QVERIFY(db.roleCheck().isEmpty());                                                       // no check without a successful connection, nothing alarming

        // SQLite: the file rights of the SSH user.
        QCOMPARE(QString::fromUtf8(shell("rm -f /tmp/fq_role.db; sqlite3 /tmp/fq_role.db 'create table t(a);'; echo ready\n").data).trimmed(), QStringLiteral("ready"));
        QVERIFY(db.saveProfile({ { "name", "file" }, { "engine", "sqlite" }, { "path", "/tmp/fq_role.db" } }, QString(), false).isEmpty());
        db.loadSchema();
        QTRY_VERIFY_WITH_TIMEOUT(!db.busy() && !db.roleCheck().isEmpty(), 30000);
        QCOMPARE(db.roleCheck().value("level").toString(), QStringLiteral("note"));
        QVERIFY(findings(db.roleCheck()).contains("peut écrire"));
        QVERIFY(shell("chmod 444 /tmp/fq_role.db\n").code == "exit:0");
        db.loadSchema();
        QTRY_VERIFY_WITH_TIMEOUT(!db.busy() && db.roleCheck().value("level").toString() == "ok", 30000);
        // Leaving the session clears it with everything else.
        controller.disconnectServer();
        QVERIFY(db.roleCheck().isEmpty());
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        fortiq::SshSession cleanup;
        if (cleanup.open(controller.connectionRequest()).status == SshResult::Status::Ok) cleanup.runScript("rm -f /tmp/fq_role.db\n", 4096, {});
        controller.removeServer(serverId);
        settings.remove(pin); settings.remove("servers/profiles"); settings.remove("db/profiles");
    }

    // The Bases de données app against the test server: SQLite file and PostgreSQL 17 (role fqdb / base fqtest).
    // Needs FORTIQ_IT_DB=1 and the PostgreSQL cluster running (`pg_ctlcluster 17 main start`).
    void databaseAppIsReadOnlyAndBounded() {
        if (qEnvironmentVariable("FORTIQ_IT_DB").isEmpty()) QSKIP("FORTIQ_IT_DB not set");
        fortiq::RemoteController controller;
        QSettings settings;
        settings.remove("servers/profiles"); settings.remove("db/profiles");
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        const QString serverId = controller.saveServer({ { "name", "DB test" }, { "host", base.host }, { "port", base.port }, { "user", base.user }, { "keyPath", key } });
        QVERIFY(!serverId.isEmpty());
        controller.connectTo(serverId, key.isEmpty() ? base.password : QString());
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);

        auto shell = [&](const QString &script) {
            fortiq::SshSession session;
            const SshResult opened = session.open(controller.connectionRequest());
            if (opened.status != SshResult::Status::Ok) return opened;
            return session.runScript(script.toUtf8(), 65536, {});
        };
        const SshResult made = shell(QStringLiteral("rm -f /tmp/fq_it.db /tmp/fq_pwned\n"
            "sqlite3 /tmp/fq_it.db \"create table people(id integer primary key, name text, note text, score real); "
            "insert into people values (1,'Zoë','it''s',1.5),(2,'Иван',NULL,NULL),(3,'NULL','',0);\"\n"));
        QVERIFY2(made.status == SshResult::Status::Ok && made.code == "exit:0", qPrintable(made.message));

        fortiq::DbController db(&controller);
        auto run = [&](const QString &sql) {
            db.runQuery(sql);
            if (db.busy()) QTRY_VERIFY_WITH_TIMEOUT(!db.busy(), 30000);
        };
        auto cellOf = [&](int row, int col) { return db.rows().at(row).toList().at(col); };

        // ---- SQLite ----
        QVERIFY(!db.saveProfile({ { "name", "Fichier" }, { "engine", "sqlite" }, { "path", "relative.db" } }, QString(), false).isEmpty());
        QVERIFY(db.saveProfile({ { "name", "Fichier" }, { "engine", "sqlite" }, { "path", "/tmp/fq_it.db" } }, QString(), false).isEmpty());
        QCOMPARE(db.profiles().size(), 1);
        QVERIFY(!db.selectedId().isEmpty());
        db.loadSchema();
        QTRY_VERIFY_WITH_TIMEOUT(!db.busy() && !db.schema().isEmpty(), 20000);
        QCOMPARE(db.schema().first().toMap().value("name").toString(), QStringLiteral("people"));
        QCOMPARE(db.schema().first().toMap().value("columns").toList().size(), 4);

        const int tasksBefore = controller.tasks()->count();
        run("select * from people order by id");
        QVERIFY2(db.error().isEmpty(), qPrintable(db.error()));
        QCOMPARE(db.rows().size(), 3);
        QCOMPARE(db.columns(), QStringList({ "id", "name", "note", "score" }));
        QCOMPARE(cellOf(0, 1).toString(), QString::fromUtf8("Zo\xC3\xAB"));
        QCOMPARE(cellOf(0, 2).toString(), QStringLiteral("it's"));
        QVERIFY(cellOf(1, 2).isNull());                         // NULL
        QVERIFY(!cellOf(2, 1).isNull());                        // the text 'NULL'
        QCOMPARE(cellOf(2, 1).toString(), QStringLiteral("NULL"));
        QVERIFY(!cellOf(2, 2).isNull());
        QCOMPARE(cellOf(2, 2).toString(), QString());           // empty string
        QCOMPARE(controller.tasks()->count(), tasksBefore + 1);
        QCOMPARE(controller.tasks()->tasks().constLast().toMap().value("kind").toString(), QStringLiteral("dbQuery"));
        QVERIFY(!controller.tasks()->tasks().constLast().toMap().value("title").toString().contains("people"));   // the SQL never reaches the task list

        // Refused before sending: no task, no change, nothing executed on the server.
        const int tasksNow = controller.tasks()->count();
        for (const QString &bad : { QStringLiteral("delete from people"), QStringLiteral("select 1; delete from people"),
                                    QStringLiteral(".shell touch /tmp/fq_pwned"), QStringLiteral("select 1\n.shell touch /tmp/fq_pwned"),
                                    QStringLiteral("drop table people"), QStringLiteral("attach database '/tmp/x' as x") }) {
            db.runQuery(bad);
            QVERIFY2(!db.busy() && !db.error().isEmpty(), qPrintable(bad));
        }
        QCOMPARE(controller.tasks()->count(), tasksNow);
        // Second layer: a WITH ... DELETE passes the keyword check but sqlite3 -readonly refuses it.
        run("with x as (select 1) delete from people");
        QVERIFY(!db.error().isEmpty());
        run("select count(*) from people");
        QCOMPARE(cellOf(0, 0).toString(), QStringLiteral("3"));
        QCOMPARE(QString::fromUtf8(shell(QStringLiteral("[ -e /tmp/fq_pwned ] && echo YES || echo NO\n")).data).trimmed(), QStringLiteral("NO"));

        // Row limit and truncation flag.
        db.setLimits(15, 5);
        run("with recursive c(x) as (select 1 union all select x+1 from c limit 100) select x from c");
        QCOMPARE(db.rows().size(), 5);
        QVERIFY(db.truncated());
        QVERIFY(db.resultInfo().contains("tronqué"));
        // Time limit: an endless query ends with a clear message, the connection stays usable.
        db.setLimits(2, 500);
        QElapsedTimer timer; timer.start();
        run("with recursive c(x) as (select 1 union all select x+1 from c) select count(*) from c");
        QVERIFY2(db.error().contains("Délai"), qPrintable(db.error()));
        QVERIFY(timer.elapsed() < 12000);
        run("select 1 as one");
        QVERIFY(db.error().isEmpty()); QCOMPARE(cellOf(0, 0).toString(), QStringLiteral("1"));
        // Cancel
        db.setLimits(60, 500);
        db.runQuery("with recursive c(x) as (select 1 union all select x+1 from c) select count(*) from c");
        QTRY_VERIFY_WITH_TIMEOUT(db.busy(), 5000);
        QTest::qWait(500);
        timer.restart();
        db.cancelQuery();
        QTRY_VERIFY_WITH_TIMEOUT(!db.busy(), 8000);
        QVERIFY(timer.elapsed() < 8000);
        QCOMPARE(controller.tasks()->tasks().constLast().toMap().value("state").toString(), QStringLiteral("cancelled"));
        db.setLimits(15, 500);
        // Missing file
        QVERIFY(db.saveProfile({ { "name", "Absent" }, { "engine", "sqlite" }, { "path", "/tmp/fq_absent.db" } }, QString(), false).isEmpty());
        run("select 1");
        QVERIFY2(db.error().contains("n'existe pas"), qPrintable(db.error()));

        // ---- PostgreSQL ----
        QVERIFY(db.saveProfile({ { "name", "PG" }, { "engine", "postgres" }, { "host", "127.0.0.1" }, { "port", 5432 }, { "user", "fqdb" }, { "database", "fqtest" } },
                               QStringLiteral("fqdb-test-pw"), false).isEmpty());
        const QString pgId = db.selectedId();
        QVERIFY(db.selected().value("hasPassword").toBool());
        QVERIFY(!settings.value("db/profiles").toByteArray().contains("fqdb-test-pw"));   // the password is never in the settings
        QVERIFY(fortiq::SecretStore::load("db-" + pgId).ok);
        db.loadSchema();
        QTRY_VERIFY_WITH_TIMEOUT(!db.busy() && !db.schema().isEmpty(), 20000);
        bool hasPeople = false;
        for (const QVariant &o : db.schema()) hasPeople |= o.toMap().value("name").toString() == "people";
        QVERIFY(hasPeople);
        run("select id, name, note, score from people order by id");
        QVERIFY2(db.error().isEmpty(), qPrintable(db.error()));
        QCOMPARE(db.rows().size(), 3);
        QCOMPARE(cellOf(0, 1).toString(), QString::fromUtf8("Zo\xC3\xAB M\xC3\xBCller"));
        QVERIFY(cellOf(1, 3).isNull());                        // numeric NULL
        QVERIFY(cellOf(1, 2).toString().contains("line1"));
        QVERIFY(db.numeric().first().toBool());
        // The password is never on a command line of the server.
        db.runQuery("select pg_sleep(4)");
        QTRY_VERIFY_WITH_TIMEOUT(db.busy(), 5000);
        QTest::qWait(1500);
        const SshResult ps = shell(QStringLiteral("ps -eo args | grep -v grep | grep -c 'fqdb-test-pw'; true\n"));
        QCOMPARE(QString::fromUtf8(ps.data).trimmed(), QStringLiteral("0"));
        QTRY_VERIFY_WITH_TIMEOUT(!db.busy(), 15000);
        // Layered guard: WITH ... INSERT passes the keyword check but the read-only session refuses it.
        run("with x as (insert into people(name) values ('hack') returning *) select * from x");
        QVERIFY2(db.error().contains("lecture seule"), qPrintable(db.error()));
        run("select count(*) from people");
        QCOMPARE(cellOf(0, 0).toString(), QStringLiteral("3"));
        for (const QString &bad : { QStringLiteral("insert into people(name) values ('x')"), QStringLiteral("set default_transaction_read_only=off"),
                                    QStringLiteral("select 1; select 2"), QStringLiteral("\\! touch /tmp/fq_pwned"), QStringLiteral("select 1 \\g") }) {
            db.runQuery(bad);
            QVERIFY2(!db.busy() && !db.error().isEmpty(), qPrintable(bad));
        }
        QCOMPARE(QString::fromUtf8(shell(QStringLiteral("[ -e /tmp/fq_pwned ] && echo YES || echo NO\n")).data).trimmed(), QStringLiteral("NO"));
        // Server-side statement timeout.
        db.setLimits(2, 500);
        timer.restart();
        run("select pg_sleep(30)");
        QVERIFY2(db.error().contains("Délai"), qPrintable(db.error()));
        QVERIFY(timer.elapsed() < 12000);
        // Large output is bounded.
        db.setLimits(30, 100);
        run("select * from big");
        QVERIFY2(db.error().isEmpty(), qPrintable(db.error()));
        QVERIFY(db.rows().size() <= 100);
        db.setLimits(15, 500);
        // Wrong password: a clear message, no secret in it.
        QVERIFY(db.saveProfile({ { "id", pgId }, { "name", "PG" }, { "engine", "postgres" }, { "host", "127.0.0.1" }, { "port", 5432 }, { "user", "fqdb" }, { "database", "fqtest" } },
                               QStringLiteral("wrong-pw"), false).isEmpty());
        run("select 1");
        QVERIFY2(db.error().contains("Authentification"), qPrintable(db.error()));
        QVERIFY(!db.error().contains("wrong-pw"));
        // Changing the target drops the stored password (it must not be sent to a new host).
        QVERIFY(db.saveProfile({ { "id", pgId }, { "name", "PG" }, { "engine", "postgres" }, { "host", "localhost" }, { "port", 5432 }, { "user", "fqdb" }, { "database", "fqtest" } },
                               QString(), false).isEmpty());
        QVERIFY(!db.selected().value("hasPassword").toBool());
        QVERIFY(!fortiq::SecretStore::load("db-" + pgId).ok);

        // ---- cleanup: disconnect clears results; removing the server removes its profiles ----
        shell(QStringLiteral("rm -f /tmp/fq_it.db"));
        controller.disconnectServer();
        QVERIFY(db.rows().isEmpty() && db.schema().isEmpty() && db.selectedId().isEmpty());
        controller.removeServer(serverId);
        QVERIFY(db.profiles().isEmpty());
        shell(QStringLiteral("rm -f /tmp/fq_it.db\n"));
        settings.remove(pin); settings.remove("servers/profiles"); settings.remove("db/profiles");
    }

    // Needs FORTIQ_IT_WEB_BASE (for example http://127.0.0.1:8099): tests/fixtures/web_fixture.py running on the test server.
    void browserFetchesThroughTheServer() {
        const QString web = qEnvironmentVariable("FORTIQ_IT_WEB_BASE");
        if (web.isEmpty()) QSKIP("FORTIQ_IT_WEB_BASE not set");
        fortiq::RemoteController controller;
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        fortiq::BrowserController browser(&controller);
        // The fixture is on the server's loopback: an INTERNAL address, so every navigation asks once (audit R1). The test answers yes.
        auto settle = [&](int ms) {
            QTRY_VERIFY_WITH_TIMEOUT(browser.state() == "confirm" || !browser.busy(), ms);
            if (browser.state() == "confirm") { browser.confirmInternal(); QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(), ms); }
        };

        // Not connected / refused schemes never reach the server.
        browser.open(QStringLiteral("file:///etc/passwd"));
        QCOMPARE(browser.state(), QStringLiteral("error"));
        QVERIFY(browser.status().contains("http"));
        QCOMPARE(controller.tasks()->count(), 0);

        // A page: sanitised, reader mode keeps <main>, nothing active survives.
        browser.open(web + "/page");
        QTRY_VERIFY_WITH_TIMEOUT(browser.state() == "confirm", 20000);       // asked before anything is fetched
        QCOMPARE(browser.pendingConfirm().value("host").toString(), QStringLiteral("127.0.0.1"));
        browser.confirmInternal();
        QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(), 20000);
        QCOMPARE(browser.state(), QStringLiteral("ready"));
        QCOMPARE(browser.kind(), QStringLiteral("html"));
        QCOMPARE(browser.title(), QStringLiteral("Fixture page"));
        QCOMPARE(browser.info().value("status").toInt(), 200);
        QVERIFY(browser.contentHtml().contains("Hello fixture"));
        QVERIFY2(browser.contentHtml().contains(QString("<a href=\"%1/page2\">link</a>").arg(web)), qPrintable(browser.contentHtml()));
        QVERIFY(!browser.contentHtml().contains("executed"));
        QVERIFY(!browser.contentHtml().contains("<script") && !browser.contentHtml().contains("<img") && !browser.contentHtml().contains("javascript:"));
        QVERIFY(!browser.contentHtml().contains("Footer text"));           // reader mode
        QVERIFY(browser.contentHtml().contains("[Tracking pixel]"));
        QCOMPARE(browser.refreshUrl(), web + "/page2");                       // reported, not followed
        QVERIFY(browser.notes().join("\n").contains("redirection"));
        QCOMPARE(browser.url(), web + "/page");
        browser.setReader(false);
        QVERIFY(browser.contentHtml().contains("Footer text"));
        browser.setReader(true);
        QVERIFY(!browser.contentHtml().contains("Footer text"));
        QVERIFY(!browser.headers().isEmpty());

        // Redirects are followed by the server and reported.
        browser.open(web + "/redir");
        settle(20000);
        QCOMPARE(browser.url(), web + "/page");
        QCOMPARE(browser.info().value("redirects").toInt(), 1);

        // Links go through the same rules; history works.
        browser.openLink("/page2");
        settle(20000);
        QCOMPARE(browser.url(), web + "/page2");
        QCOMPARE(browser.title(), QStringLiteral("Second"));
        browser.openLink("javascript:alert(1)");
        QCOMPARE(browser.url(), web + "/page2");                              // ignored
        QVERIFY(browser.canGoBack());
        browser.goBack();
        settle(20000);
        QCOMPARE(browser.url(), web + "/page");
        QVERIFY(browser.canGoForward());

        // Errors are shown as pages with a note; failures are explicit.
        browser.open(web + "/missing");
        settle(20000);
        QCOMPARE(browser.state(), QStringLiteral("ready"));
        QCOMPARE(browser.info().value("status").toInt(), 404);
        QVERIFY(browser.notes().join("\n").contains("404"));
        browser.open(QStringLiteral("http://nonexistent-host.invalid/"));
        settle(40000);
        QCOMPARE(browser.state(), QStringLiteral("error"));
        QVERIFY2(browser.status().contains("DNS"), qPrintable(browser.status()));
        QVERIFY(browser.contentHtml().isEmpty());
        browser.open(web + "/loop");
        settle(20000);
        QCOMPARE(browser.state(), QStringLiteral("error"));
        QVERIFY2(browser.status().contains("redirections"), qPrintable(browser.status()));
        browser.open(web + "/redir-file");                                    // a redirect to file:// is refused by curl
        settle(20000);
        QCOMPARE(browser.state(), QStringLiteral("error"));
        QVERIFY(!browser.contentText().contains("root:"));
        QVERIFY(!browser.contentHtml().contains("root:"));
        browser.open(web + "/big");
        settle(30000);
        QCOMPARE(browser.state(), QStringLiteral("error"));
        QVERIFY2(browser.status().contains("5 Mio"), qPrintable(browser.status()));

        // Other content types.
        browser.open(web + "/json");
        settle(20000);
        QCOMPARE(browser.kind(), QStringLiteral("json"));
        QVERIFY(browser.contentText().contains("\n    \"name\": \"fortiq\""));
        browser.open(web + "/latin1");
        settle(20000);
        QCOMPARE(browser.kind(), QStringLiteral("text"));
        QCOMPARE(browser.contentText(), QString::fromUtf8("café crème"));
        browser.open(web + "/binary");
        settle(20000);
        QCOMPARE(browser.kind(), QStringLiteral("binary"));
        QVERIFY(browser.contentText().isEmpty());
        browser.open(web + "/nojs");
        settle(20000);
        QVERIFY(browser.notes().join("\n").contains("JavaScript"));

        // Stopping a slow page returns quickly and ends its task as cancelled.
        QElapsedTimer timer;
        timer.start();
        browser.open(web + "/slow");
        QTRY_VERIFY_WITH_TIMEOUT(browser.state() == "confirm", 10000);
        browser.confirmInternal();
        QTRY_VERIFY_WITH_TIMEOUT(browser.busy(), 5000);
        QTest::qWait(300);
        browser.stop();
        settle(6000);
        QVERIFY(timer.elapsed() < 6000);
        QCOMPARE(browser.status(), QStringLiteral("Chargement annulé."));
        const QVariantMap last = controller.tasks()->tasks().constLast().toMap();
        QCOMPARE(last.value("kind").toString(), QStringLiteral("browse"));
        QCOMPARE(last.value("state").toString(), QStringLiteral("cancelled"));

        // Every fetch was a task: none left active, the successful ones succeeded.
        QCOMPARE(controller.tasks()->activeCount(), 0);
        int succeeded = 0;
        for (const QVariant &v : controller.tasks()->tasks()) succeeded += v.toMap().value("state").toString() == "succeeded";
        QVERIFY(succeeded >= 8);

        // A new server session clears the page: nothing shown may look like it came from another server.
        controller.disconnectServer();
        QCOMPARE(browser.state(), QStringLiteral("disconnected"));
        QVERIFY(browser.contentHtml().isEmpty() && browser.url().isEmpty() && !browser.canGoBack());
        settings.remove(pin);
    }

    // Audit R1: the Browser's address policy against a real server. Needs FORTIQ_IT_WEB_BASE and FORTIQ_IT_POLICY=1 with
    // tool/setup_browser_policy_fixture.sh run as root on the DISPOSABLE Debian (public-looking loopback aliases, host names, fixture on 0.0.0.0).
    void browserAddressPolicy() {
        if (qEnvironmentVariable("FORTIQ_IT_WEB_BASE").isEmpty() || qEnvironmentVariable("FORTIQ_IT_POLICY").isEmpty()) QSKIP("FORTIQ_IT_WEB_BASE and FORTIQ_IT_POLICY are needed");
        fortiq::RemoteController controller;
        QSettings settings;
        const auto pin = QStringLiteral("ssh/hostPins/") + QString::fromLatin1(QCryptographicHash::hash(
            (base.host.toLower() + ':' + QString::number(base.port)).toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(pin, fingerprint);
        const QString key = base.password.isEmpty() ? qEnvironmentVariable("FORTIQ_IT_KEY") : QString();
        controller.connectServer(base.host, base.port, base.user, base.password, key);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 15000);
        fortiq::BrowserController browser(&controller);
        fortiq::SshSession shellSession;
        QVERIFY(shellSession.open(controller.connectionRequest()).status == SshResult::Status::Ok);
        auto shell = [&](const QByteArray &script) { return QString::fromUtf8(shellSession.runScript(script, 65536, {}).data).trimmed(); };
        auto hits = [&](const QString &path) {   // how many requests the fixture actually received for a path
            const QJsonObject o = QJsonDocument::fromJson(shell("curl -s http://127.0.0.1:8099/_hits\n").toUtf8()).object();
            return o.value(path).toInt(0);
        };
        auto waitIdleOrAsk = [&] { QTRY_VERIFY_WITH_TIMEOUT(browser.state() == "confirm" || !browser.busy(), 30000); };
        const QString pub = "http://public-fixture.test:8099";
        const int tasksBefore = controller.tasks()->count();

        // ---- a public address is fetched without a question, and curl is pinned to the address that was checked ----
        browser.open(pub + "/page");
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("ready"));
        QCOMPARE(browser.info().value("pinnedIp").toString(), QStringLiteral("93.184.216.34"));
        QCOMPARE(browser.info().value("remoteIp").toString(), QStringLiteral("93.184.216.34"));      // curl really connected to the pinned address
        QVERIFY(browser.contentHtml().contains("Hello fixture"));
        // two public addresses: the first is pinned and used
        browser.open("http://multi-fixture.test:8099/echo");
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("ready"));
        QCOMPARE(browser.info().value("remoteIp").toString(), QStringLiteral("93.184.216.34"));
        QVERIFY2(browser.contentText().contains("local=93.184.216.34"), qPrintable(browser.contentText()));

        // ---- an internal address asks BEFORE anything is sent ----
        const int pageHitsBefore = hits("/page");
        browser.open("http://127.0.0.1:8099/page");
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("confirm"));
        QVERIFY(!browser.busy());
        QCOMPARE(browser.pendingConfirm().value("host").toString(), QStringLiteral("127.0.0.1"));
        QVERIFY(browser.pendingConfirm().value("reason").toString().contains("boucle locale"));
        QCOMPARE(hits("/page"), pageHitsBefore);                                                    // not a single request reached the service
        QCOMPARE(controller.tasks()->tasks().constLast().toMap().value("state").toString(), QStringLiteral("running"));
        browser.cancelConfirm();
        QCOMPARE(browser.state() == "confirm", false);
        QCOMPARE(controller.tasks()->tasks().constLast().toMap().value("state").toString(), QStringLiteral("cancelled"));
        QCOMPARE(hits("/page"), pageHitsBefore);
        browser.open("http://127.0.0.1:8099/page");
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("confirm"));
        browser.confirmInternal();
        QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(), 20000);
        QCOMPARE(browser.state(), QStringLiteral("ready"));
        QCOMPARE(hits("/page"), pageHitsBefore + 1);
        QVERIFY(browser.info().value("addressClass").toString().contains("boucle locale"));
        // the answer is for one navigation only
        browser.open("http://127.0.0.1:8099/page");
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("confirm"));
        browser.cancelConfirm();
        // names that resolve to something internal ask too; a mixed set is internal and the internal address is the one shown
        browser.open("http://internal-fixture.test:8099/page");
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("confirm"));
        browser.cancelConfirm();
        browser.open("http://mixed-fixture.test:8099/page");
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("confirm"));
        const QVariantList mixed = browser.pendingConfirm().value("details").toList();
        QCOMPARE(mixed.size(), 2);
        QStringList classes;                                                                          // getent orders the addresses as it likes
        for (const QVariant &d : mixed) classes << d.toMap().value("cls").toString();
        classes.sort();
        QCOMPARE(classes, QStringList({ "internal", "public" }));
        browser.cancelConfirm();
        browser.open("http://private-fixture.test/");                                               // 10.255.255.1: asks, and would not even connect
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("confirm"));
        QVERIFY(browser.pendingConfirm().value("reason").toString().contains("réseau privé"));
        browser.cancelConfirm();

        // ---- redirects are judged hop by hop ----
        const int toLoopbackBefore = hits("/to-loopback"), pageBefore = hits("/page");
        browser.open(pub + "/to-loopback");                                                         // public host -> loopback
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("confirm"));
        QCOMPARE(browser.pendingConfirm().value("hop").toInt(), 1);
        QVERIFY(browser.pendingConfirm().value("from").toString().endsWith("/to-loopback"));
        QCOMPARE(hits("/to-loopback"), toLoopbackBefore + 1);                                       // the public hop was fetched...
        QCOMPARE(hits("/page"), pageBefore);                                                        // ...the internal target was not
        browser.cancelConfirm();
        QCOMPARE(hits("/page"), pageBefore);
        browser.open(pub + "/to-loopback");
        waitIdleOrAsk();
        browser.confirmInternal();
        QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(), 20000);
        QCOMPARE(browser.state(), QStringLiteral("ready"));
        QCOMPARE(browser.info().value("redirects").toInt(), 1);
        QCOMPARE(browser.url(), QStringLiteral("http://127.0.0.1:8099/page"));
        QCOMPARE(hits("/page"), pageBefore + 1);
        // a decimal spelling of 127.0.0.1 in a redirect is read as what it is, not as a way around the question
        browser.open(pub + "/to-odd");
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("confirm"));
        QCOMPARE(browser.pendingConfirm().value("host").toString(), QStringLiteral("127.0.0.1"));
        browser.cancelConfirm();
        // internal (confirmed) -> public: no second question; internal -> another internal host:port: a new question
        browser.open("http://127.0.0.1:8099/to-public");
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("confirm"));
        browser.confirmInternal();
        QTRY_VERIFY_WITH_TIMEOUT(!browser.busy(), 20000);
        QCOMPARE(browser.state(), QStringLiteral("ready"));
        QCOMPARE(browser.info().value("remoteIp").toString(), QStringLiteral("93.184.216.34"));
        browser.open("http://127.0.0.1:8099/to-other-port");
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("confirm"));
        browser.confirmInternal();
        QTRY_VERIFY_WITH_TIMEOUT(browser.state() == "confirm" || !browser.busy(), 20000);
        QCOMPARE(browser.state(), QStringLiteral("confirm"));                                       // 127.0.0.1:8098 is a different host:port
        QCOMPARE(browser.pendingConfirm().value("port").toInt(), 8098);
        browser.cancelConfirm();

        // ---- cloud metadata is never opened, and there is nothing to confirm ----
        const int metaBefore = controller.tasks()->count();
        for (const QString &url : { QStringLiteral("http://169.254.169.254/latest/meta-data/"), QStringLiteral("http://[fd00:ec2::254]/"), QStringLiteral("http://[::ffff:169.254.169.254]/"),
                                    QStringLiteral("http://2852039166/"), QStringLiteral("http://metadata.google.internal/"), QStringLiteral("http://meta-fixture.test/"), pub + "/to-metadata" }) {
            browser.open(url);
            waitIdleOrAsk();
            QVERIFY2(browser.state() == "error", qPrintable(url + " -> " + browser.state() + " " + browser.status()));
            QVERIFY2(browser.state() != "confirm", qPrintable(url));
            QVERIFY2(browser.status().contains("bloqu"), qPrintable(url + " -> " + browser.status()));
            QVERIFY(browser.pendingConfirm().isEmpty());
            QCOMPARE(controller.tasks()->tasks().constLast().toMap().value("state").toString(), QStringLiteral("failed"));
        }
        QCOMPARE(controller.tasks()->count(), metaBefore + 7);

        // ---- the user's own curl settings cannot get around the pin ----
        QCOMPARE(shell("printf 'proxy = \"http://127.0.0.1:1\"\\nlocation\\n' > ~/.curlrc; echo ok\n"), QStringLiteral("ok"));
        browser.open(pub + "/page");
        waitIdleOrAsk();
        QCOMPARE(browser.state(), QStringLiteral("ready"));                                         // ~/.curlrc is ignored (curl -q), so is a proxy
        shell("rm -f ~/.curlrc\n");

        QCOMPARE(controller.tasks()->activeCount(), 0);
        QVERIFY(controller.tasks()->count() > tasksBefore);
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
