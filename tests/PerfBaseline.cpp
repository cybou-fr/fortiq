// Performance baseline (docs/PERFORMANCE_BASELINE.md, docs/STABILIZATION_PLAN.md §4). NOT a pass/fail test: it measures and prints
// markdown table rows. Run through tool/run_perf_baseline.py, which prepares the disposable server (a directory of 50 000 files).
//
//   fortiq_perf <section>...   sections: terminal ssh files parsers logs soak=<minutes>
// Environment (same as the live tests): FORTIQ_IT_HOST/PORT/USER/FINGERPRINT/KEY, FORTIQ_PERF_DIR (the 50 000-entry directory).
#include "core/RemotePath.h"
#include "browser/HtmlSanitizer.h"
#include "core/LogsController.h"
#include "db/DbInfo.h"
#include "mail/MailInfo.h"
#include "ssh/SshClient.h"
#include "terminal/TerminalView.h"

extern "C" {
#include "vterm.h"
}

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSettings>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <numeric>
#include <vector>

#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

using namespace fortiq;

namespace {
struct ProcessStats { double workingSetMiB = 0, peakMiB = 0; int threads = 0, handles = 0; };

ProcessStats stats() {
    ProcessStats s;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc {};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) { s.workingSetMiB = pmc.WorkingSetSize / 1048576.0; s.peakMiB = pmc.PeakWorkingSetSize / 1048576.0; }
    DWORD handles = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handles)) s.handles = int(handles);
    if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te { sizeof te };
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) if (te.th32OwnerProcessID == GetCurrentProcessId()) ++s.threads;
        CloseHandle(snap);
    }
#endif
    return s;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(p * double(v.size())))];
}

void row(const char *scenario, const QString &result, const QString &note = {}) {
    std::printf("| %s | %s | %s |\n", scenario, qPrintable(result), qPrintable(note));
    std::fflush(stdout);
}

ConnectionRequest request() {
    ConnectionRequest r;
    r.host = qEnvironmentVariable("FORTIQ_IT_HOST", QStringLiteral("127.0.0.1"));
    r.port = qEnvironmentVariableIntValue("FORTIQ_IT_PORT") ? qEnvironmentVariableIntValue("FORTIQ_IT_PORT") : 22;
    r.user = qEnvironmentVariable("FORTIQ_IT_USER");
    r.keyPath = qEnvironmentVariable("FORTIQ_IT_KEY");
    r.password = qEnvironmentVariable("FORTIQ_IT_PASSWORD");
    r.expectedHostFingerprint = qEnvironmentVariable("FORTIQ_IT_FINGERPRINT");
    return r;
}

// ---------------------------------------------------------------------------------------------------------------- terminal
void terminal() {
    auto view = std::make_unique<TerminalView>();
    view->setProperty("inputEnabled", true);
    view->setSize(QSizeF(1200, 700));
    struct Case { const char *name; QByteArray chunk; qint64 total; };
    QByteArray colored;
    for (int i = 0; i < 64; ++i) colored += "\x1b[3" + QByteArray::number(1 + i % 6) + "m" + QByteArray("line of coloured output ") + QByteArray::number(i) + "\x1b[0m the quick brown fox jumps\r\n";
    QByteArray random;
    for (int i = 0; i < 65536; ++i) random += char(QRandomGenerator::global()->bounded(32, 126));
    for (int i = 64; i < random.size(); i += 80) random[i] = '\n';
    const Case cases[] = { { "`yes` (2-byte lines)", QByteArray(65536, 'y').replace(1, 0, "").replace("yy", "y\n"), 64 << 20 },
                           { "coloured lines (SGR)", colored, 64 << 20 }, { "random printable text", random, 32 << 20 } };
    for (const Case &c : cases) {
        const ProcessStats before = stats();
        QElapsedTimer t; t.start();
        qint64 fed = 0;
        while (fed < c.total) { view->feed(c.chunk); fed += c.chunk.size(); }
        const double seconds = t.elapsed() / 1000.0;
        const ProcessStats after = stats();
        row(c.name, QStringLiteral("%1 MiB/s").arg(fed / 1048576.0 / seconds, 0, 'f', 1),
            QStringLiteral("%1 MiB fed in %2 s; working set %3 → %4 MiB").arg(fed / 1048576.0, 0, 'f', 0).arg(seconds, 0, 'f', 2).arg(before.workingSetMiB, 0, 'f', 0).arg(after.workingSetMiB, 0, 'f', 0));
    }
}


// ------------------------------------------------------------------------------------------------ raw libvterm (where does the time go?)
void vtermRaw() {
    QByteArray yes;
    for (int i = 0; i < 32768; ++i) yes += "y\n";
    const qint64 total = 8 << 20;
    struct Variant { const char *name; bool screen; bool pushline; bool flush; };
    const Variant variants[] = { { "libvterm only (no screen layer)", false, false, false }, { "screen layer, scrolling dropped", true, false, true },
                                 { "screen layer + scrollback push (as TerminalView)", true, true, true }, { "same, flush once per 64 KiB chunk", true, true, false } };
    for (const Variant &v : variants) {
        VTerm *vt = vterm_new(40, 150);
        vterm_set_utf8(vt, 1);
        VTermScreen *screen = nullptr;
        std::vector<std::vector<VTermScreenCell>> scrollback;
        static const VTermScreenCallbacks cbPush = [] {
            VTermScreenCallbacks cb{};
            cb.sb_pushline = [](int cols, const VTermScreenCell *cells, void *user) {
                auto *sb = static_cast<std::vector<std::vector<VTermScreenCell>> *>(user);
                sb->emplace_back(cells, cells + cols);
                if (sb->size() > 1000) sb->erase(sb->begin());      // not a deque here: the measurement is of libvterm, the container is a stand-in
                return 1;
            };
            return cb;
        }();
        static const VTermScreenCallbacks cbNone = [] { VTermScreenCallbacks cb{}; cb.sb_pushline = [](int, const VTermScreenCell *, void *) { return 1; }; return cb; }();
        if (v.screen) { screen = vterm_obtain_screen(vt); vterm_screen_reset(screen, 1); vterm_screen_set_callbacks(screen, v.pushline ? &cbPush : &cbNone, &scrollback); }
        QElapsedTimer t; t.start();
        qint64 fed = 0;
        while (fed < total) {
            vterm_input_write(vt, yes.constData(), size_t(yes.size()));
            if (screen && v.flush) vterm_screen_flush_damage(screen);
            fed += yes.size();
        }
        if (screen) vterm_screen_flush_damage(screen);
        const double seconds = t.elapsed() / 1000.0;
        row(v.name, QStringLiteral("%1 MiB/s").arg(fed / 1048576.0 / seconds, 0, 'f', 2), QStringLiteral("%1 MiB of `yes` in %2 s = %3 µs per line").arg(fed / 1048576.0, 0, 'f', 0).arg(seconds, 0, 'f', 2).arg(seconds * 1e6 / (fed / 2.0), 0, 'f', 2));
        vterm_free(vt);
    }
}

// --------------------------------------------------------------------------------------------------------------------- ssh
void ssh() {
    const ConnectionRequest req = request();
    std::vector<double> open;
    for (int i = 0; i < 30; ++i) {
        QElapsedTimer t; t.start();
        SshSession s;
        const SshResult r = s.open(req);
        if (r.status != SshResult::Status::Ok) { row("SSH open", QStringLiteral("FAILED"), r.message); return; }
        open.push_back(double(t.elapsed()));
    }
    row("SSH open + SFTP init + home listing (30 sequential)", QStringLiteral("p50 %1 ms, p95 %2 ms, max %3 ms").arg(percentile(open, 0.5), 0, 'f', 0).arg(percentile(open, 0.95), 0, 'f', 0).arg(*std::max_element(open.begin(), open.end()), 0, 'f', 0),
        QStringLiteral("loopback to a WSL server: protocol cost without network latency"));
    const ProcessStats before = stats();
    std::vector<std::unique_ptr<SshSession>> sessions;
    QElapsedTimer t; t.start();
    for (int i = 0; i < 16; ++i) {
        sessions.push_back(std::make_unique<SshSession>());
        if (sessions.back()->open(req).status != SshResult::Status::Ok) { row("16 simultaneous sessions", QStringLiteral("FAILED at %1").arg(i)); return; }
    }
    const ProcessStats after = stats();
    row("16 simultaneous sessions", QStringLiteral("%1 MiB total, %2 MiB per session").arg(after.workingSetMiB - before.workingSetMiB, 0, 'f', 1).arg((after.workingSetMiB - before.workingSetMiB) / 16, 0, 'f', 2),
        QStringLiteral("opened in %1 ms; threads %2 → %3, handles %4 → %5").arg(t.elapsed()).arg(before.threads).arg(after.threads).arg(before.handles).arg(after.handles));
    sessions.clear();
}

// ------------------------------------------------------------------------------------------------------------------- files
void files() {
    const QString dir = qEnvironmentVariable("FORTIQ_PERF_DIR");
    if (dir.isEmpty()) { row("Files: directory listing", QStringLiteral("SKIPPED"), QStringLiteral("FORTIQ_PERF_DIR not set")); return; }
    SshSession session;
    const SshResult opened = session.open(request());
    if (opened.status != SshResult::Status::Ok) { row("Files: directory listing", QStringLiteral("FAILED"), opened.message); return; }
    std::vector<double> times;
    int entries = 0;
    const ProcessStats before = stats();
    for (int i = 0; i < 5; ++i) {
        QElapsedTimer t; t.start();
        const SshResult r = session.list(dir);
        if (r.status != SshResult::Status::Ok) { row("Files: directory listing", QStringLiteral("FAILED"), r.message); return; }
        entries = int(r.entries.size());
        times.push_back(double(t.elapsed()));
    }
    const ProcessStats after = stats();
    row(qPrintable(QStringLiteral("Files: list %1 entries over SFTP (5 runs)").arg(entries)), QStringLiteral("median %1 ms, max %2 ms").arg(percentile(times, 0.5), 0, 'f', 0).arg(*std::max_element(times.begin(), times.end()), 0, 'f', 0),
        QStringLiteral("%1 entries/s; working set %2 → %3 MiB (peak %4)").arg(entries / (percentile(times, 0.5) / 1000.0), 0, 'f', 0).arg(before.workingSetMiB, 0, 'f', 0).arg(after.workingSetMiB, 0, 'f', 0).arg(after.peakMiB, 0, 'f', 0));
}

// ---------------------------------------------------------------------------------------------------------------- parsers
void parsers() {
    {   // Logs: journal JSON lines
        QByteArray line = R"({"__REALTIME_TIMESTAMP":"1792970466000000","PRIORITY":"3","_SYSTEMD_UNIT":"nginx.service","SYSLOG_IDENTIFIER":"nginx","MESSAGE":"connect() failed (111: Connection refused) while connecting to upstream, client: 10.0.0.5, server: example.org","_PID":"4242","_HOSTNAME":"srv"})";
        QElapsedTimer t; t.start();
        const int n = 200000;
        for (int i = 0; i < n; ++i) (void) LogsController::parseJournalLine(line);
        const double s = t.elapsed() / 1000.0;
        row("Logs: parse journal lines", QStringLiteral("%1 lines/s").arg(n / s, 0, 'f', 0), QStringLiteral("budget 10 000 lines/s: headroom ×%1").arg(n / s / 10000.0, 0, 'f', 0));
        QVariantList events;
        for (int i = 0; i < 2000; ++i) events << LogsController::parseJournalLine(line.replace("4242", QByteArray::number(i)));
        t.restart();
        for (int i = 0; i < 20; ++i) (void) LogsController::groupPatterns(events);
        row("Logs: group 2000 events into patterns", QStringLiteral("%1 ms").arg(t.elapsed() / 20.0, 0, 'f', 1), QStringLiteral("the 2000-event view limit (kMaxEvents)"));
    }
    {   // Databases
        QByteArray csv = "n,v\n";
        while (csv.size() < 4 * 1024 * 1024) csv += "12345,hello world\n";
        QElapsedTimer t; t.start();
        const DbResult a = parsePostgresCsv(csv, "<<N>>", 500, true);
        const double early = double(t.elapsed());
        t.restart();
        const DbResult b = parsePostgresCsv(csv, "<<N>>", 5000, true);
        row("DB: parse a 4 MiB psql answer", QStringLiteral("%1 ms (limit 500 rows), %2 ms (limit 5000 rows)").arg(early, 0, 'f', 0).arg(t.elapsed()), QStringLiteral("rows kept %1 / %2").arg(a.rows.size()).arg(b.rows.size()));
    }
    {   // Mail
        QString html = QStringLiteral("<html><body>");
        while (html.size() < 3 * 1024 * 1024) html += QStringLiteral("<p>Hello <b>world</b> <a href=\"http://example.org/x\">link</a> filler text filler text</p>");
        html += QStringLiteral("</body></html>");
        QElapsedTimer t; t.start();
        const SanitizedPage page = sanitizeHtml(html, QStringLiteral("https://mail.invalid/"), false);
        row("Mail/Browser: sanitize a 3 MB HTML page", QStringLiteral("%1 ms").arg(t.elapsed()), QStringLiteral("%1 links kept; measured, not yet optimized").arg(page.linkCount));
        QJsonArray parts;
        parts.append(QJsonObject { { "id", 1 }, { "content-type", "text/plain" }, { "content", QString(2 * 1024 * 1024, QLatin1Char('x')) } });
        const QJsonObject message { { "id", "m@x" }, { "match", true }, { "timestamp", 1 }, { "tags", QJsonArray() }, { "headers", QJsonObject { { "Subject", "s" } } }, { "body", parts } };
        const QByteArray json = QJsonDocument(QJsonArray { QJsonArray { QJsonArray { message, QJsonArray() } } }).toJson(QJsonDocument::Compact);
        t.restart();
        const QList<MailMessage> m = parseMailThread(json);
        row("Mail: parse a 2 MB message thread (notmuch JSON)", QStringLiteral("%1 ms").arg(t.elapsed()), QStringLiteral("%1 message").arg(m.size()));
    }
    {   // Paths
        QElapsedTimer t; t.start();
        QString sink;
        for (int i = 0; i < 200000; ++i) sink = cleanAbsolutePath(QStringLiteral("/home/user/../user/./Documents//Projects/%1/notes.txt").arg(i)).value_or(QString());
        row("Files: normalize 200 000 remote paths", QStringLiteral("%1 ms").arg(t.elapsed()), sink.left(0));
    }
}

// ------------------------------------------------------------------------------------------------------------------ soak
void soak(int minutes) {
    const ConnectionRequest req = request();
    SshSession longLived;
    if (longLived.open(req).status != SshResult::Status::Ok) { row("Soak", QStringLiteral("FAILED to open"), {}); return; }
    struct Sample { double minute; ProcessStats s; };
    std::vector<Sample> samples;
    QElapsedTimer clock; clock.start();
    qint64 cycles = 0, failures = 0;
    qint64 nextSample = 0;
    while (clock.elapsed() < qint64(minutes) * 60000) {
        SshSession s;                                           // one connect / list / close cycle per second
        if (s.open(req).status != SshResult::Status::Ok) ++failures;
        s.close();
        if (longLived.list(req.directory.isEmpty() ? QStringLiteral("/") : req.directory).status != SshResult::Status::Ok) ++failures;
        ++cycles;
        if (clock.elapsed() >= nextSample) { samples.push_back({ clock.elapsed() / 60000.0, stats() }); nextSample += 15000; }
        QThread::msleep(500);
    }
    samples.push_back({ clock.elapsed() / 60000.0, stats() });
    // least-squares slope of the working set over the second half (the first half contains warm-up)
    const size_t from = samples.size() / 2;
    double sx = 0, sy = 0, sxx = 0, sxy = 0; const double n = double(samples.size() - from);
    for (size_t i = from; i < samples.size(); ++i) { sx += samples[i].minute; sy += samples[i].s.workingSetMiB; sxx += samples[i].minute * samples[i].minute; sxy += samples[i].minute * samples[i].s.workingSetMiB; }
    const double slopePerMinute = n > 1 && (n * sxx - sx * sx) != 0 ? (n * sxy - sx * sy) / (n * sxx - sx * sx) : 0;
    const Sample &first = samples[std::min<size_t>(samples.size() - 1, 2)], &last = samples.back();
    row(qPrintable(QStringLiteral("Soak %1 min: %2 connect/list/close cycles + a long-lived session").arg(minutes).arg(cycles)),
        QStringLiteral("working set %1 → %2 MiB, trend %3 MiB/h").arg(first.s.workingSetMiB, 0, 'f', 1).arg(last.s.workingSetMiB, 0, 'f', 1).arg(slopePerMinute * 60, 0, 'f', 1),
        QStringLiteral("threads %1 → %2, handles %3 → %4, failures %5").arg(first.s.threads).arg(last.s.threads).arg(first.s.handles).arg(last.s.handles).arg(failures));
}
}  // namespace

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FORTIQ-perf"));
    QCoreApplication::setApplicationName(QStringLiteral("fortiq-perf"));
    std::printf("| scenario | result | notes |\n|---|---|---|\n");
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("terminal")) terminal();
        else if (a == QLatin1String("vterm")) vtermRaw();
        else if (a == QLatin1String("ssh")) ssh();
        else if (a == QLatin1String("files")) files();
        else if (a == QLatin1String("parsers")) parsers();
        else if (a.startsWith(QLatin1String("soak="))) soak(a.mid(5).toInt());
        else std::fprintf(stderr, "unknown section %s\n", argv[i]);
    }
    const ProcessStats end = stats();
    row("Process at the end", QStringLiteral("working set %1 MiB (peak %2)").arg(end.workingSetMiB, 0, 'f', 0).arg(end.peakMiB, 0, 'f', 0), QStringLiteral("threads %1, handles %2").arg(end.threads).arg(end.handles));
    return 0;
}
