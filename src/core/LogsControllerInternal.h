#pragma once
// Internal to the LogsController implementation units (Logs*.cpp).
#include "LogsController.h"
#include "RemoteController.h"
#include "../ssh/SshClient.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <optional>

namespace fortiq {
namespace logs_detail {
inline constexpr int kMaxEvents = 2000;
inline constexpr qint64 kMaxOutput = 8 * 1024 * 1024;

inline QString priorityName(int p) {
    switch (p) {
    case 0: return QStringLiteral("urgence");
    case 1: return QStringLiteral("alerte");
    case 2: return QStringLiteral("critique");
    case 3: return QStringLiteral("erreur");
    case 4: return QStringLiteral("avertissement");
    case 5: return QStringLiteral("notice");
    case 6: return QStringLiteral("info");
    case 7: return QStringLiteral("debug");
    default: return QStringLiteral("inconnu");
    }
}
inline QString timeText(double ms) {
    if (ms <= 0) return QString();
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ms)).toString(QStringLiteral("dd/MM HH:mm:ss"));
}
// Best-effort timestamp of a plain log line: ISO-8601 or syslog "Mon dd hh:mm:ss".
inline double fileLineTime(const QString &line) {
    static const QRegularExpression iso(QStringLiteral("^\\[?(\\d{4}-\\d{2}-\\d{2}[T ]\\d{2}:\\d{2}:\\d{2})"));
    static const QRegularExpression syslog(QStringLiteral("^([A-Z][a-z]{2}) +(\\d{1,2}) (\\d{2}:\\d{2}:\\d{2})"));
    if (const auto m = iso.match(line); m.hasMatch()) {
        QString t = m.captured(1);
        t[10] = 'T';
        const QDateTime dt = QDateTime::fromString(t, Qt::ISODate);
        if (dt.isValid()) return static_cast<double>(dt.toMSecsSinceEpoch());
    }
    if (const auto m = syslog.match(line); m.hasMatch()) {
        static const QStringList months { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        const int month = static_cast<int>(months.indexOf(m.captured(1))) + 1;
        const QDate today = QDate::currentDate();
        int year = today.year();
        if (month > today.month()) --year; // syslog omits the year
        const QDateTime dt(QDate(year, month, m.captured(2).toInt()), QTime::fromString(m.captured(3), QStringLiteral("HH:mm:ss")));
        if (dt.isValid()) return static_cast<double>(dt.toMSecsSinceEpoch());
    }
    return 0;
}
inline QString hms(double ms) {
    return ms > 0 ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ms)).toString(QStringLiteral("HH:mm:ss")) : QStringLiteral("?");
}
inline QVariantMap item(const QString &text, const QList<int> &refs, const QString &action = {}, const QString &arg = {}) {
    QVariantList r;
    for (int i : refs.mid(0, 40)) r << i;
    QVariantMap m { { "text", text }, { "refs", r } };
    if (!action.isEmpty()) { m.insert("action", action); m.insert("arg", arg); }
    return m;
}
struct Lifecycle { QString kind, unit, detail; };
// systemd's own unit messages (English; other locales are simply not classified).
inline std::optional<Lifecycle> lifecycleOf(const QVariantMap &e) {
    static const QRegularExpression restart(QStringLiteral("Scheduled restart job, restart counter is at (\\d+)"));
    static const QRegularExpression failed(QStringLiteral("Failed with result '([^']+)'"));
    static const QRegularExpression exited(QStringLiteral("Main process exited, code=(\\w+), status=([^\\s]+)"));
    static const QRegularExpression started(QStringLiteral("^Started (.+?)\\.?$"));
    static const QRegularExpression stopped(QStringLiteral("^Stopped (.+?)\\.?$"));
    static const QRegularExpression tooQuick(QStringLiteral("Start request repeated too quickly"));
    const QString msg = e.value("message").toString();
    QString unit = e.value("subject").toString();
    auto pick = [&](const QString &fallback) { return unit.isEmpty() ? fallback : unit; };
    if (auto m = restart.match(msg); m.hasMatch()) return Lifecycle { "restart", pick(e.value("unit").toString()), m.captured(1) };
    if (auto m = tooQuick.match(msg); m.hasMatch()) return Lifecycle { "gaveup", pick(e.value("unit").toString()), {} };
    if (auto m = failed.match(msg); m.hasMatch()) return Lifecycle { "failed", pick(e.value("unit").toString()), m.captured(1) };
    if (auto m = exited.match(msg); m.hasMatch()) return Lifecycle { "exited", pick(e.value("unit").toString()), m.captured(2) };
    if (!unit.isEmpty()) {
        if (auto m = started.match(msg); m.hasMatch()) return Lifecycle { "started", unit, {} };
        if (auto m = stopped.match(msg); m.hasMatch()) return Lifecycle { "stopped", unit, {} };
    }
    return std::nullopt;
}
}  // namespace logs_detail
using namespace logs_detail;
}  // namespace fortiq
