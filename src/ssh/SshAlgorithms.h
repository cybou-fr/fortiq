#pragma once
// The SSH algorithm policy (S-1), shared by the blocking SshSession and the gateway tunnel.
#include <libssh2.h>

namespace fortiq {
// Applies the policy to a fresh session; false if a list would be empty or libssh2 refuses it.
bool applyAlgorithmPolicy(LIBSSH2_SESSION *session);
}
