#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>

namespace fortiq {

// Whether the connection to a server is believed to be alive, as seen by every part of the client that talks to it
// (Files, Logs, Système, Réseau, Courrier, Bases, ...). Each of them owns its own SshSession on its own worker thread, so
// the session layer reports here instead of every controller repeating the same bookkeeping:
//   - a session that finds its transport dead reports "lost";
//   - a session that opens or runs something successfully reports "ok".
// The UI reads the state of the active server (RemoteController::linkLost) and marks data as out of date.
//
// A server that was never reached is not "lost" (that is a plain connection error): only a server that answered once
// in this run can become lost. The state is keyed by user@host:port.
class LinkMonitor : public QObject {
    Q_OBJECT
public:
    struct State {
        bool lost = false;
        bool everOk = false;
        QDateTime lostAt;      // when the loss was noticed (null while the link works)
        QString reason;        // short French text, e.g. "Connexion SSH perdue."
    };

    static LinkMonitor &instance();                    // always lives in the application's (main) thread
    static QString key(const QString &host, int port, const QString &user);

    // Thread-safe: callable from any worker thread; applied in the main thread, in order.
    static void reportOk(const QString &key);
    static void reportLost(const QString &key, const QString &reason);
    static void forget(const QString &key);            // server removed or disconnected on purpose

    // Main thread only.
    State state(const QString &key) const;
    bool lost(const QString &key) const { return state(key).lost; }

signals:
    void changed(const QString &key);

private:
    LinkMonitor() = default;
    void applyOk(const QString &key);
    void applyLost(const QString &key, const QString &reason);
    void applyForget(const QString &key);
    QHash<QString, State> m_states;
};

}  // namespace fortiq
