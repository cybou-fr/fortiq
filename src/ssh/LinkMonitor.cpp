#include "LinkMonitor.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

namespace fortiq {

LinkMonitor &LinkMonitor::instance() {
    static LinkMonitor *monitor = [] {
        auto *m = new LinkMonitor;   // lives as long as the process
        if (QCoreApplication *app = QCoreApplication::instance(); app && m->thread() != app->thread()) m->moveToThread(app->thread());
        return m;
    }();
    return *monitor;
}

QString LinkMonitor::key(const QString &host, int port, const QString &user) {
    return user.trimmed() + QLatin1Char('@') + host.trimmed().toLower() + QLatin1Char(':') + QString::number(port);
}

void LinkMonitor::reportOk(const QString &key) {
    if (key.isEmpty()) return;
    QMetaObject::invokeMethod(&instance(), [key] { instance().applyOk(key); }, Qt::QueuedConnection);
}

void LinkMonitor::reportLost(const QString &key, const QString &reason) {
    if (key.isEmpty()) return;
    QMetaObject::invokeMethod(&instance(), [key, reason] { instance().applyLost(key, reason); }, Qt::QueuedConnection);
}

void LinkMonitor::forget(const QString &key) {
    if (key.isEmpty()) return;
    QMetaObject::invokeMethod(&instance(), [key] { instance().applyForget(key); }, Qt::QueuedConnection);
}

LinkMonitor::State LinkMonitor::state(const QString &key) const { return m_states.value(key); }

void LinkMonitor::applyOk(const QString &key) {
    State &s = m_states[key];
    const bool transition = !s.everOk || s.lost;      // first answer, or the link came back
    s.everOk = true;
    s.lost = false;
    s.lostAt = QDateTime();
    s.reason.clear();
    if (transition) emit changed(key);
}

void LinkMonitor::applyLost(const QString &key, const QString &reason) {
    auto it = m_states.find(key);
    if (it == m_states.end() || !it->everOk) return;     // never reached: an ordinary connection error, not a lost link
    if (it->lost) { it->reason = reason; return; }
    it->lost = true;
    it->lostAt = QDateTime::currentDateTime();
    it->reason = reason;
    emit changed(key);
}

void LinkMonitor::applyForget(const QString &key) {
    if (m_states.remove(key) > 0) emit changed(key);
}

}  // namespace fortiq
