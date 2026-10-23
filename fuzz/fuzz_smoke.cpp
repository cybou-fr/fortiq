// Deterministic driver for the fuzz targets: every seed file goes through every target, then a fixed number of seeded
// mutations of the seeds do. Same inputs on every machine, so it can be a ctest; the real search is libFuzzer (docs/FUZZING.md).
// Usage: fortiq_fuzz_smoke <seed dir>... [--iterations N] [--seed S]
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>

#include <cstdio>

#include "fuzz_targets.h"

static QByteArray mutate(QByteArray d, QRandomGenerator &rng) {
    static const char *tokens[] = { "<script>", "</", "<a href=\"javascript:", "<img src=x onerror=", "&#x", "&amp", "\"", "'", ";", "--", "/*", "$$", "\\", "\n.", "\r\n", "\xc3\xa9", "\xff\xfe", "\xed\xa0\x80", "<!--", "<![CDATA[", "http://", "@", "<", ">", "\n\n", "%00", "::ffff:127.0.0.1", "169.254.169.254" };
    const int n = 1 + int(rng.bounded(4));
    for (int k = 0; k < n; ++k) {
        switch (rng.bounded(6)) {
        case 0: if (!d.isEmpty()) d[int(rng.bounded(d.size()))] = char(rng.bounded(256)); break;
        case 1: d.insert(int(rng.bounded(d.size() + 1)), QByteArray(tokens[rng.bounded(int(sizeof(tokens) / sizeof(tokens[0])))])); break;
        case 2: if (!d.isEmpty()) d.remove(int(rng.bounded(d.size())), int(1 + rng.bounded(8))); break;
        case 3: if (d.size() > 2) { const int a = int(rng.bounded(d.size())); d.insert(int(rng.bounded(d.size() + 1)), d.mid(a, int(1 + rng.bounded(32)))); } break;
        case 4: d.truncate(int(rng.bounded(d.size() + 1))); break;
        default: d.insert(int(rng.bounded(d.size() + 1)), QByteArray(1, '\0')); break;
        }
        if (d.size() > 60000) d.truncate(60000);
    }
    return d;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--list")) { for (int t = 0; t < fortiq::fuzz::targetCount(); ++t) std::printf("%s\n", fortiq::fuzz::targetName(t)); return 0; }
    int iterations = 3000; quint32 seed = 20261024;
    QStringList dirs;
    const QStringList args = app.arguments().mid(1);
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == "--iterations" && i + 1 < args.size()) iterations = args[++i].toInt();
        else if (args[i] == "--seed" && i + 1 < args.size()) seed = args[++i].toUInt();
        else dirs << args[i];
    }
    QList<QByteArray> seeds;
    for (const QString &dir : dirs) {
        QDir d(dir);
        const auto files = d.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files) {
            QFile f(fi.absoluteFilePath());
            if (f.open(QIODevice::ReadOnly)) seeds << f.read(200000);
        }
    }
    if (seeds.isEmpty()) { std::fprintf(stderr, "no seed files found in the given directories\n"); return 2; }
    seeds << QByteArray() << QByteArray("\0", 1) << QByteArray("<p>x</p>");
    const int targets = fortiq::fuzz::targetCount();
    long runs = 0;
    for (const QByteArray &s : seeds)
        for (int t = 0; t < targets; ++t) { fortiq::fuzz::runTarget(t, s); ++runs; }
    const QString lastInput = qEnvironmentVariable("FORTIQ_FUZZ_LAST");   // optional: file that always holds the running mutation (selector byte + data)
    QRandomGenerator rng(seed);
    for (int n = 0; n < iterations; ++n) {
        const QByteArray m = mutate(seeds[int(rng.bounded(seeds.size()))], rng);
        const int t = int(rng.bounded(targets));
        if (!lastInput.isEmpty()) { QFile f(lastInput); if (f.open(QIODevice::WriteOnly)) { f.write(QByteArray(1, char(t))); f.write(m); } }   // the input that was running if the next line aborts
        fortiq::fuzz::runTarget(t, m); ++runs;
    }
    std::printf("fuzz smoke: %d seeds x %d targets + %d mutations = %ld runs, no crash, invariants held\n", int(seeds.size()), targets, iterations, runs);
    return 0;
}
