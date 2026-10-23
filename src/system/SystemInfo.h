#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace fortiq {

// System app (read-only): processes, systemd services and a short overview
// (load, memory, uptime) of the selected Linux server. Existing tools run over
// SSH (`ps`, `systemctl`, /proc files); nothing is started, stopped or killed.
// See docs/SYSTEM.md.

struct ProcessInfo {
    int pid = 0, ppid = 0;
    QString user;
    QString state;      // ps STAT: R, S, D, Z, T, I + modifiers
    double cpu = 0, mem = 0;
    qint64 rssKb = 0;
    qint64 elapsedSec = 0;
    int threads = 1;
    QString command;    // full command line as ps prints it
    QString name;       // executable base name, or the [kernel thread] name
    bool kernelThread = false;
};

struct ServiceInfo {
    QString unit;       // "ssh.service"
    QString load;       // loaded | not-found | masked | error
    QString active;     // active | inactive | failed | activating | deactivating
    QString sub;        // running | dead | exited | failed | ...
    QString description;
};

struct SystemOverview {
    double load1 = 0, load5 = 0, load15 = 0;
    int cores = 0;
    qint64 uptimeSec = 0;
    qint64 memTotalKb = 0, memAvailableKb = 0, memFreeKb = 0, buffersKb = 0, cachedKb = 0, swapTotalKb = 0, swapFreeKb = 0;
    QString os, kernel, hostname;
    QString systemdState;    // is-system-running: running | degraded | maintenance | starting | stopping ...
};

struct SystemSnapshot {
    bool ok = false;
    QString problem;
    SystemOverview overview;
    QList<ProcessInfo> processes;
    bool processesTruncated = false;
    bool hasSystemd = false;
    QList<ServiceInfo> services;
    QStringList notes;
};

constexpr int kMaxProcesses = 3000;

// Script for `sh -s` printing "#section <name>" frames. Read-only.
QByteArray buildSystemSnapshotScript();
SystemSnapshot parseSystemSnapshot(const QByteArray &output);

// Individual parsers (exposed for tests).
QList<ProcessInfo> parsePs(const QByteArray &text);
QList<ServiceInfo> parseServices(const QByteArray &text);
void parseOverviewSection(const QString &name, const QByteArray &body, SystemOverview &overview);

// QML views.
QVariantList processesToVariant(const QList<ProcessInfo> &list);
QVariantList servicesToVariant(const QList<ServiceInfo> &list);
QVariantMap overviewToVariant(const SystemOverview &overview, int processCount, int running, int zombies, int failedServices, int activeServices, int serviceCount);

QString humanKb(qint64 kb);
QString humanDuration(qint64 seconds);   // "3 j 4 h", "5 h 12 min", "42 s"

}  // namespace fortiq
