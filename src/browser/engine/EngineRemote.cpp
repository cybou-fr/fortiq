// The only part of the engine that knows the application's RemoteController (kept apart so that the rest builds in the unit tests).
#include "EngineBrowser.h"

#include "../gateway/SshTunnel.h"
#include "../../core/RemoteController.h"

namespace fortiq::engine {

EngineBrowser::EngineBrowser(RemoteController *remote, QObject *parent) : QObject(parent), m_remote(remote) {
    m_source.connected = [remote] { return remote->connected(); };
    m_source.label = [remote] { return remote->serverLabel(); };
    m_source.token = [remote] { return remote->connectionToken(); };
    m_source.makeTunnel = [remote](QObject *parent) -> gateway::TunnelBackend * {
        return new gateway::SshTunnel(remote->connectionRequest(), parent);
    };
    connect(remote, &RemoteController::changed, this, &EngineBrowser::refresh);
}

}  // namespace fortiq::engine
