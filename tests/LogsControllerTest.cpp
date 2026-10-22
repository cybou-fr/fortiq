#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include "core/LogsController.h"
#include "core/RemoteController.h"
#include "ssh/SshClient.h"

namespace fortiq {
class LogsControllerTest : public QObject {
    Q_OBJECT
    static SshResult page(qint64 size, qint64 start, bool reset, const QByteArray &data) {
        SshResult r;
        r.status = SshResult::Status::Ok;
        r.code = "exit:0";
        r.data = "FORTIQ:stat 12 " + QByteArray::number(size) + " " + QByteArray::number(start)
            + (reset ? " 1 " : " 0 ") + QByteArray::number(data.size()) + "\n" + data;
        return r;
    }
private slots:
    void documentDownloadDisconnectIsUncertain() {
        RemoteController remote;
        remote.m_connected = true;
        for (const QString &path : {QString("content://provider/document/1"), QString("C:/native.txt")}) {
            RemoteController::Transfer job;
            job.local = path; job.state = "running";
            remote.m_transfers << job;
        }
        remote.disconnectServer();
        QCOMPARE(remote.m_transfers[0].state, QString("unknown"));
        QVERIFY(remote.m_transfers[0].message.contains("partiel"));
        QCOMPARE(remote.m_transfers[1].state, QString("cancelled"));
    }
    void linuxReaderScript() {
#ifdef Q_OS_WIN
        const QString distro = qEnvironmentVariable("FORTIQ_TEST_WSL");
        if (distro.isEmpty()) QSKIP("Set FORTIQ_TEST_WSL to exercise the actual Linux reader script");
#endif
        QTemporaryDir dir(QDir::currentPath() + "/logs-script-XXXXXX");
        QVERIFY(dir.isValid());
        QFile file(dir.path() + QString::fromUtf8("/été ' log.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QString path = file.fileName();
#ifdef Q_OS_WIN
        path = "/mnt/" + path.left(1).toLower() + path.mid(2);
#endif
        RemoteController remote;
        LogsController logs(&remote);
        logs.m_source = "file";
        logs.m_target = path;
        auto read = [&](bool append) {
            QProcess shell;
#ifdef Q_OS_WIN
            shell.start("wsl", {"-d", distro, "--", "sh", "-s"});
#else
            shell.start("sh", {"-s"});
#endif
            if (!shell.waitForStarted()) return false;
            shell.write(logs.fileScript(append));
            shell.closeWriteChannel();
            if (!shell.waitForFinished(15000)) { shell.kill(); shell.waitForFinished(); return false; }
            SshResult r;
            r.status = SshResult::Status::Ok;
            r.code = QString("exit:%1").arg(shell.exitCode());
            r.data = shell.readAllStandardOutput();
            r.message = QString::fromUtf8(shell.readAllStandardError());
            logs.apply(r, append, 0);
            return logs.state() != "error";
        };
        QVERIFY(read(false));
        const QByteArray line = QString::fromUtf8("2026-10-22 00:00:00 événement\n").toUtf8();
        QVERIFY(file.open(QIODevice::Append));
        for (int i = 0; i < 30000; ++i) QCOMPARE(file.write(line), line.size());
        file.close();
        int pages = 0;
        while (logs.m_fileOffset < file.size() && pages < 10) { QVERIFY(read(true)); ++pages; }
        QVERIFY(pages > 1);
        QCOMPARE(logs.m_fileOffset, file.size());
        QCOMPARE(logs.events().size(), 2000);
        QCOMPARE(logs.events().last().toMap()["message"].toString(), QString::fromUtf8(line.chopped(1)));
        // A fresh view is a bounded tail; a rename/recreate resets its inode.
        QVERIFY(read(false));
        QCOMPARE(logs.m_fileOffset, file.size());
        QCOMPARE(logs.state(), QString("limited"));
        QVERIFY(file.rename(file.fileName() + ".old"));
        QFile replacement(dir.path() + QString::fromUtf8("/été ' log.txt"));
        QVERIFY(replacement.open(QIODevice::WriteOnly));
        QCOMPARE(replacement.write("rotated\n"), 8);
        replacement.close();
        QVERIFY(read(true));
        QCOMPARE(logs.events().size(), 1);
        QCOMPARE(logs.events().last().toMap()["message"].toString(), QString("rotated"));
    }
    void backlogAndSplitUtf8() {
        RemoteController remote;
        LogsController logs(&remote);
        logs.m_source = "file";
        logs.apply(page(0, 0, true, {}), false, 0);
        const QByteArray text = QString::fromUtf8("été\n").toUtf8();
        logs.apply(page(9000000, 0, false, text.left(1)), true, 0);
        QCOMPARE(logs.m_fileOffset, 1);
        QVERIFY(logs.events().isEmpty());
        QCOMPARE(logs.state(), QString("limited"));
        logs.apply(page(text.size(), 1, false, text.mid(1)), true, 0);
        QCOMPARE(logs.events().size(), 1);
        QCOMPARE(logs.events()[0].toMap()["message"].toString(), QString::fromUtf8("été"));
        QCOMPARE(logs.m_fileOffset, text.size());
    }
    void journalResumesAfterCompleteEvent() {
        RemoteController remote;
        LogsController logs(&remote);
        SshResult r;
        r.status = SshResult::Status::Ok;
        r.code = "exit:-1";
        r.truncated = true;
        r.data = "{\"MESSAGE\":\"first\",\"__CURSOR\":\"c1\",\"PRIORITY\":\"3\"}\n{\"MESSAGE\":\"cut";
        logs.apply(r, false, 0);
        QCOMPARE(logs.m_cursor, QString("c1"));
        QCOMPARE(logs.events().size(), 1);
        QCOMPARE(logs.state(), QString("limited"));
        const QByteArray script = logs.journalScript(true);
        QVERIFY(script.contains("--after-cursor='c1'"));
        QVERIFY(script.contains("--no-tail"));
        QVERIFY(!script.contains(" -n "));
        r.truncated = false;
        r.code = "exit:0";
        r.data = "{\"MESSAGE\":\"cut completed\",\"__CURSOR\":\"c2\",\"PRIORITY\":\"3\"}\n";
        logs.apply(r, true, 0);
        QCOMPARE(logs.m_cursor, QString("c2"));
        QCOMPARE(logs.events().size(), 2);
        QVERIFY(logs.state().isEmpty());
    }
    void incompleteNeverAdvances() {
        RemoteController remote;
        LogsController logs(&remote);
        logs.m_source = "file";
        logs.apply(page(2, 0, true, "a\n"), false, 0);
        for (int kind = 0; kind < 3; ++kind) {
            auto r = page(10, 2, false, "b\n");
            if (kind == 0) r.truncated = true;
            if (kind == 1) r.code = "exit:5";
            if (kind == 2) r.data.chop(1);
            logs.apply(r, true, 0);
            QCOMPARE(logs.m_fileOffset, 2);
            QCOMPARE(logs.events().size(), 1);
            QCOMPARE(logs.state(), QString("error"));
        }
        logs.apply(page(4, 2, false, "b\n"), true, 0);
        QCOMPARE(logs.m_fileOffset, 4);
        QCOMPARE(logs.events().size(), 2);
    }
    void longLineAndRotation() {
        RemoteController remote;
        LogsController logs(&remote);
        logs.m_source = "file";
        logs.apply(page(0, 0, true, {}), false, 0);
        const QByteArray chunk(262144, 'x');
        for (int i = 0; i < 40; ++i)
            logs.apply(page(40 * chunk.size() + 1, i * chunk.size(), false, chunk), true, 0);
        QVERIFY(logs.m_filePending.size() <= 16000);
        QVERIFY(logs.events().isEmpty());
        logs.apply(page(logs.m_fileOffset + 1, logs.m_fileOffset, false, "\n"), true, 0);
        QCOMPARE(logs.events().size(), 1);
        QCOMPARE(logs.events()[0].toMap()["message"].toString().size(), 4000);
        logs.apply(page(4, 0, true, "new\n"), true, 0);
        QCOMPARE(logs.events().size(), 1);
        QCOMPARE(logs.events()[0].toMap()["message"].toString(), QString("new"));
        QCOMPARE(logs.m_fileOffset, 4);
    }
    void initialTailSkipsPartialLine() {
        RemoteController remote;
        LogsController logs(&remote);
        logs.m_source = "file";
        logs.apply(page(20, 10, true, "cut\nwhole\n"), false, 0);
        QCOMPARE(logs.events().size(), 1);
        QCOMPARE(logs.events()[0].toMap()["message"].toString(), QString("whole"));
    }
    void connectionAndSelectionInvalidate() {
        RemoteController remote;
        LogsController logs(&remote);
        remote.m_connected = true;
        remote.m_generation = 42;
        QVERIFY(logs.accepts(42));
        QVERIFY(!logs.accepts(41));
        logs.m_serviceBusy = true;
        logs.m_units = {QString("old server")};
        logs.m_serviceInfo.insert("id", "old.service");
        const int sources = logs.m_sourcesSeq, service = logs.m_serviceSeq, inspector = logs.m_inspectorSeq;
        remote.m_connected = false;
        emit remote.changed();
        QVERIFY(!logs.accepts(42));
        QVERIFY(logs.m_sourcesSeq > sources);
        QVERIFY(logs.m_serviceSeq > service);
        QVERIFY(logs.m_inspectorSeq > inspector);
        QVERIFY(logs.units().isEmpty());
        QVERIFY(logs.serviceInfo().isEmpty());
        QVERIFY(!logs.serviceBusy());
        const int seq = logs.m_serviceSeq;
        logs.select("journal");
        QVERIFY(logs.m_serviceSeq > seq);
        logs.m_source = "file";
        logs.apply(page(2, 0, true, "a\n"), false, logs.m_seq - 1);
        QVERIFY(logs.events().isEmpty());
    }
    void lateResultsAndRemoteFailures() {
        RemoteController remote;
        remote.m_connected = true;
        remote.m_generation = 42;
        LogsController logs(&remote);
        SshResult sources;
        sources.status = SshResult::Status::Ok;
        sources.code = "exit:0";
        sources.data = "U\talpha.service\tactive\trunning\n";
        logs.applySources(sources, 41, 0);
        QVERIFY(logs.units().isEmpty());
        logs.applySources(sources, 42, -1);
        QVERIFY(logs.units().isEmpty());
        logs.applySources(sources, 42, 0);
        QCOMPARE(logs.units().size(), 1);
        SshResult service = sources;
        service.data = "#show\nId=alpha.service\nLoadState=loaded\nActiveState=active\n#journal\n";
        logs.applyService(service, 41, 0, false);
        QVERIFY(logs.serviceInfo().isEmpty());
        logs.applyService(service, 42, -1, false);
        QVERIFY(logs.serviceInfo().isEmpty());
        logs.applyService(service, 42, 0, false);
        QVERIFY(!logs.serviceInfo().isEmpty());
        logs.m_serviceInfo.clear();
        service.code = "exit:1"; // transport success, remote failure without stderr
        logs.applyService(service, 42, 0, false);
        QVERIFY(logs.serviceInfo().isEmpty());
        QVERIFY(!logs.serviceError().isEmpty());
        logs.apply(service, false, 0);
        QCOMPARE(logs.state(), QString("error"));
        QVERIFY(logs.events().isEmpty());
        logs.m_units.clear();
        sources.truncated = true;
        logs.applySources(sources, 42, 0);
        QVERIFY(logs.units().isEmpty());
    }
};
}
QTEST_GUILESS_MAIN(fortiq::LogsControllerTest)
#include "LogsControllerTest.moc"
