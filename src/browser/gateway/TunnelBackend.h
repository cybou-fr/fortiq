#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

namespace fortiq::gateway {

// What the gateway needs from the SSH side: byte channels to an IP:port as the SERVER sees it (SSH `direct-tcpip`) and small scripts
// run on the server (name resolution). SshTunnel is the real implementation; the unit tests use a loopback fake.
// Everything is asynchronous and used from ONE thread (the gateway's): calls return at once, results arrive as signals in that thread.
class TunnelBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    enum class State { Disconnected, Connecting, Connected };
    Q_ENUM(State)

    // Opens a channel to `ip`:`port` (a numeric address, never a name: the gateway pinned it). Returns an id (never 0).
    virtual quint64 openChannel(const QString &ip, int port) = 0;
    // Queues bytes for the server. The backend reports consumption with channelWritten() so the caller can apply backpressure.
    virtual void write(quint64 id, const QByteArray &data) = 0;
    // Sends EOF after everything queued (half-close); the channel stays readable.
    virtual void shutdownWrite(quint64 id) = 0;
    // false = stop reading from the server (the SSH window fills and the remote program waits): backpressure for slow clients.
    virtual void setReading(quint64 id, bool on) = 0;
    // Closes now; no channelClosed() is emitted for a close the caller asked for.
    virtual void closeChannel(quint64 id) = 0;
    // Runs a POSIX sh script on the server (stdin), at most maxOut bytes of stdout. Returns an id; result in execFinished().
    virtual quint64 exec(const QByteArray &script, int maxOut) = 0;

signals:
    void channelOpened(quint64 id);
    void channelFailed(quint64 id, const QString &message);
    void channelData(quint64 id, const QByteArray &data);
    void channelWritten(quint64 id, qint64 bytes);              // bytes handed over to the SSH layer
    void channelClosed(quint64 id, const QString &reason);      // peer closed (reason empty) or the transport died (reason set)
    void execFinished(quint64 id, bool ok, const QByteArray &output, const QString &problem);
    void stateChanged(fortiq::gateway::TunnelBackend::State state, const QString &message);
};

}  // namespace fortiq::gateway
