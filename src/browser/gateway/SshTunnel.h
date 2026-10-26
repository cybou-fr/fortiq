#pragma once

#include "TunnelBackend.h"
#include "../../ssh/SshClient.h"

#include <atomic>
#include <memory>

namespace fortiq::gateway {

// Many SSH `direct-tcpip` channels (and a few exec channels for name resolution) over ONE dedicated SSH session of the Browser to the
// selected server. It is a layer of its own: SshSession is blocking and single-threaded and stays untouched.
//   - the host key is pinned like every other session: the pin (req.expectedHostFingerprint) is REQUIRED and checked before
//     authentication, on the first connection and on every reconnection; a mismatch fails every request ("changed key");
//   - one worker thread runs a non-blocking multiplexing loop over libssh2 (poll on the one socket, round-robin over the channels);
//   - backpressure both ways (see TunnelBackend); a lost transport closes every channel with a clear reason, the next request
//     reconnects (pin checked again);
//   - the only socket this class ever creates is the TCP connection to req.host:req.port (connectCalls() counts them).
// Create it, use it and destroy it in ONE thread with an event loop (results are queued signals).
class SshTunnel : public TunnelBackend {
    Q_OBJECT
public:
    explicit SshTunnel(const ConnectionRequest &request, QObject *parent = nullptr);
    ~SshTunnel() override;

    quint64 openChannel(const QString &ip, int port) override;
    void write(quint64 id, const QByteArray &data) override;
    void shutdownWrite(quint64 id) override;
    void setReading(quint64 id, bool on) override;
    void closeChannel(quint64 id) override;
    quint64 exec(const QByteArray &script, int maxOut) override;

    State state() const;
    int connectCalls() const;        // TCP connections made towards the server (test: no other outgoing socket)
    int sessionsEstablished() const; // successful SSH logins (reconnections included)
    void simulateTransportLoss();    // test hook: drops the TCP connection abruptly (cable cut)

    // Default 15 s for a server that does not answer a channel request / 20 s for a resolution script.
    void setTimeouts(int channelOpenMs, int execMs);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace fortiq::gateway
