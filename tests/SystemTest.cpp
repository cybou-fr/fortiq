#include <QtTest>
#include "system/SystemInfo.h"

#include <QFile>

namespace fortiq {
class SystemTest : public QObject {
    Q_OBJECT
    static QByteArray fixture(const QString &name) {
        QFile f(QStringLiteral(FORTIQ_SYS_FIXTURES "/") + name);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return f.readAll();
    }
    static QByteArray frames(bool systemd = true) {
        QByteArray o;
        o += "#section hostname\ntest-host\n#section os\n" + fixture("os.txt") + "#section kernel\n" + fixture("kernel.txt");
        o += "#section loadavg\n" + fixture("loadavg.txt") + "#section uptime\n" + fixture("uptime.txt");
        o += "#section meminfo\n" + fixture("meminfo.txt") + "#section nproc\n" + fixture("nproc.txt");
        o += "#section ps\n" + fixture("ps.txt");
        if (systemd) o += "#section systemd\nyes\n#section running\n" + fixture("running.txt") + "#section services\n" + fixture("services.txt");
        else o += "#section systemd\nno\n";
        o += "#section end\n";
        return o;
    }
private slots:
    void fixturesArePresent() {
        for (const char *n : { "ps.txt", "services.txt", "loadavg.txt", "uptime.txt", "meminfo.txt", "nproc.txt", "os.txt", "kernel.txt", "running.txt" })
            QVERIFY2(!fixture(n).isEmpty(), n);
    }
    void parsesRealProcesses() {
        const auto list = parsePs(fixture("ps.txt"));
        QVERIFY(list.size() >= 15);
        const ProcessInfo *init = nullptr, *fail2ban = nullptr;
        for (const ProcessInfo &p : list) { if (p.pid == 1) init = &p; if (p.command.contains("fail2ban-server")) fail2ban = &p; }
        QVERIFY(init && fail2ban);
        QCOMPARE(init->ppid, 0);
        QCOMPARE(init->user, QString("root"));
        QCOMPARE(init->name, QString("init"));
        QVERIFY(init->threads >= 1);
        QCOMPARE(fail2ban->name, QString("python3 fail2ban-server"));      // the script, not just the interpreter
        QCOMPARE(fail2ban->state, QString("Ssl"));
        QVERIFY(fail2ban->rssKb > 0 && fail2ban->elapsedSec > 0);
        QCOMPARE(fail2ban->command, QString("/usr/bin/python3 /usr/bin/fail2ban-server -xf start"));
        // ps --sort=-pcpu: the list is already in descending CPU order.
        for (qsizetype i = 1; i < list.size(); ++i) QVERIFY(list.at(i - 1).cpu >= list.at(i).cpu);
    }
    void processEdgeCases() {
        const QByteArray text =
            "    2       0 root                             S     0.0  0.0     0   9000    1 [kthreadd]\n"
            " 1234     900 www-data                         Sl    1.5  2.5 12345      42   12 /usr/sbin/nginx -g 'daemon on; master_process on;'\n"
            " 4321       1 alice                            Ss    0.0  0.1  2048     300    1 -bash\n"
            "  555     900 bob                              Z     0.0  0.0     0      10    1 [sh] <defunct>\n"
            "  777       1 carol                            S     0.2  0.3   999     100    2 /opt/My App/bin/my app --flag \"a b\" été\n"
            "  778       1 root                             R+    9.9  0.0   100       5    1 python3 -u -m http.server 8099\n"
            "garbage line\n"
            "abc 1 root S 0 0 0 0 1 x\n"
            "  900       1 root                             S     0.0  0.0     1       1\n"
            "\n";
        const auto l = parsePs(text);
        QCOMPARE(l.size(), 6);
        QVERIFY(l[0].kernelThread); QCOMPARE(l[0].name, QString("kthreadd"));
        QCOMPARE(l[1].name, QString("nginx")); QCOMPARE(l[1].threads, 12); QCOMPARE(l[1].mem, 2.5); QVERIFY(l[1].command.contains("daemon on; master_process on;"));
        QCOMPARE(l[2].name, QString("bash")); QCOMPARE(l[2].user, QString("alice"));
        QVERIFY(!l[3].kernelThread); QCOMPARE(l[3].state, QString("Z"));      // a zombie is not a kernel thread
        QVERIFY(l[4].command.contains(QString::fromUtf8("été"))); QCOMPARE(l[4].user, QString("carol"));
        QCOMPARE(l[5].name, QString("python3 http.server"));   // options are skipped, the module name is shown
    }
    void parsesRealServices() {
        const auto list = parseServices(fixture("services.txt"));
        QVERIFY(list.size() >= 50);
        const ServiceInfo *ssh = nullptr;
        bool anyActive = false;
        for (const ServiceInfo &s : list) { if (s.unit == "ssh.service") ssh = &s; anyActive |= s.active == "active"; }
        QVERIFY(ssh);
        QVERIFY(anyActive);
        QCOMPARE(ssh->load, QString("loaded"));
        QVERIFY(!ssh->description.isEmpty());
        for (const ServiceInfo &s : list) QVERIFY2(s.unit.endsWith(".service") && !s.load.isEmpty() && !s.active.isEmpty() && !s.sub.isEmpty(), qPrintable(s.unit));
    }
    void serviceEdgeCases() {
        const auto l = parseServices(
            "● broken.service loaded failed failed A broken thing with a long description\n"
            "ok.service loaded active running OpenBSD Secure Shell server\n"
            "gone.service not-found inactive dead gone.service\n"
            "no-description.service loaded active exited\n"
            "this is not a unit line\n"
            "short.service loaded\n");
        QCOMPARE(l.size(), 4);
        QCOMPARE(l[0].unit, QString("broken.service")); QCOMPARE(l[0].active, QString("failed")); QCOMPARE(l[0].description, QString("A broken thing with a long description"));
        QCOMPARE(l[1].description, QString("OpenBSD Secure Shell server"));
        QCOMPARE(l[2].load, QString("not-found"));
        QCOMPARE(l[3].description, QString());
        const QVariantList v = servicesToVariant(l);
        QVERIFY(v[0].toMap().value("failed").toBool());
        QVERIFY(!v[1].toMap().value("failed").toBool());
    }
    void overviewSections() {
        SystemOverview o;
        parseOverviewSection("loadavg", "0.52 1.50 2.25 1/200 12345\n", o);
        parseOverviewSection("uptime", "93784.77 88888.88\n", o);
        parseOverviewSection("meminfo", "MemTotal:        8000000 kB\nMemFree:          100000 kB\nMemAvailable:    2000000 kB\nBuffers: 5 kB\nCached: 7 kB\nSwapTotal: 1000 kB\nSwapFree: 250 kB\n", o);
        parseOverviewSection("nproc", "4\n", o);
        parseOverviewSection("os", "Debian GNU/Linux 13 (trixie)\n", o);
        parseOverviewSection("running", "degraded\n", o);
        QVERIFY(qFuzzyCompare(o.load1, 0.52)); QVERIFY(qFuzzyCompare(o.load15, 2.25));
        QCOMPARE(o.uptimeSec, 93784LL);
        QCOMPARE(o.memTotalKb, 8000000LL); QCOMPARE(o.memAvailableKb, 2000000LL); QCOMPARE(o.swapFreeKb, 250LL);
        QCOMPARE(o.cores, 4);
        QCOMPARE(o.systemdState, QString("degraded"));
        const QVariantMap m = overviewToVariant(o, 100, 2, 1, 3, 40, 50);
        QVERIFY(qFuzzyCompare(m.value("memFraction").toDouble(), 0.75));
        QVERIFY(qFuzzyCompare(m.value("swapFraction").toDouble(), 0.75));
        QVERIFY(qFuzzyCompare(m.value("loadRatio").toDouble(), 0.13));
        QCOMPARE(m.value("failedServices").toInt(), 3);
        QCOMPARE(m.value("uptime").toString(), QString("1 j 2 h"));
    }
    void assemblesRealSnapshot() {
        const SystemSnapshot s = parseSystemSnapshot(frames());
        QVERIFY2(s.ok, qPrintable(s.problem));
        QVERIFY(s.hasSystemd);
        QVERIFY(s.processes.size() >= 15);
        QVERIFY(s.services.size() >= 50);
        QCOMPARE(s.overview.hostname, QString("test-host"));
        QVERIFY(s.overview.memTotalKb > 0);
        QVERIFY(s.overview.cores >= 1);
        QVERIFY(!s.overview.os.isEmpty() && !s.overview.kernel.isEmpty());
        QVERIFY(!processesToVariant(s.processes).isEmpty());
        QVERIFY(processesToVariant(s.processes).first().toMap().contains("elapsed"));
        const SystemSnapshot plain = parseSystemSnapshot(frames(false));
        QVERIFY(plain.ok);
        QVERIFY(!plain.hasSystemd && plain.services.isEmpty());
        QVERIFY(plain.notes.join("\n").contains("systemd"));
    }
    void brokenSnapshotsAreExplicit() {
        QVERIFY(!parseSystemSnapshot("").ok);
        QVERIFY(!parseSystemSnapshot("#section hostname\nx\n").ok);                                 // no end frame
        const SystemSnapshot noPs = parseSystemSnapshot("#section hostname\nx\n#section ps\n\n#section end\n");
        QVERIFY(!noPs.ok);
        QVERIFY(noPs.problem.contains("ps"));
        QByteArray many = "#section ps\n";
        for (int i = 1; i <= kMaxProcesses + 1; ++i) many += QByteArray::number(i) + " 1 root S 0.0 0.0 1 1 1 /bin/x\n";
        many += "#section end\n";
        const SystemSnapshot big = parseSystemSnapshot(many);
        QVERIFY(big.ok);
        QVERIFY(big.processesTruncated);
        QVERIFY(big.notes.join("\n").contains("3000"));
    }
    void scriptIsReadOnly() {
        const QByteArray s = buildSystemSnapshotScript();
        for (const QByteArray &bad : { QByteArray("kill"), QByteArray("rm "), QByteArray("sudo"), QByteArray("systemctl restart"), QByteArray("systemctl stop"),
                                       QByteArray("systemctl start"), QByteArray("systemctl enable"), QByteArray("systemctl disable"), QByteArray("systemctl mask"),
                                       QByteArray("renice"), QByteArray("shutdown"), QByteArray("reboot") })
            QVERIFY2(!s.contains(bad), bad.constData());
        QVERIFY(s.contains("systemctl list-units"));
        QVERIFY(s.contains("systemctl is-system-running"));
        QVERIFY(s.contains("head -n 3001"));                  // bounded output
        QByteArray rest = s; rest.replace(">/dev/null", ""); rest.replace("2>&1", ""); rest.replace(">&1", "");
        QVERIFY(!rest.contains('>'));
    }
    void formatsNumbers() {
        QCOMPARE(humanKb(512), QString("512 Kio"));
        QCOMPARE(humanKb(2048), QString("2.0 Mio"));
        QCOMPARE(humanKb(300 * 1024), QString("300 Mio"));
        QCOMPARE(humanKb(2LL * 1024 * 1024), QString("2.00 Gio"));
        QCOMPARE(humanDuration(42), QString("42 s"));
        QCOMPARE(humanDuration(125), QString("2 min 5 s"));
        QCOMPARE(humanDuration(5 * 3600 + 12 * 60), QString("5 h 12 min"));
        QCOMPARE(humanDuration(3 * 86400 + 4 * 3600), QString("3 j 4 h"));
        QCOMPARE(humanDuration(-5), QString("0 s"));
    }
};
}

QTEST_GUILESS_MAIN(fortiq::SystemTest)
#include "SystemTest.moc"
