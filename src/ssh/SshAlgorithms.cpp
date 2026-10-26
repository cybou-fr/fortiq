#include "SshAlgorithms.h"

#include <QByteArray>
#include <QByteArrayList>

#include <initializer_list>

namespace fortiq {
namespace {
// S-1: the algorithms the client will negotiate. libssh2's own list (in ITS order, so the host-key type that is
// negotiated, and with it every saved pin, stays what it was) minus what is obsolete or weak: SHA-1 key exchange,
// SHA-1 (ssh-rsa) and DSA host keys, CBC / 3DES / RC4 / Blowfish / CAST ciphers, MD5 / SHA-1 MACs, "none".
bool obsoleteAlgorithm(int method, const QByteArray &name)
{
    const auto has = [&](std::initializer_list<const char *> parts) {
        for (const char *part : parts) if (name.contains(part)) return true;
        return false;
    };
    if (name.contains("none")) return true;
    switch (method) {
    case LIBSSH2_METHOD_KEX: return has({ "sha1", "group1-" });
    case LIBSSH2_METHOD_HOSTKEY: return name == "ssh-rsa" || has({ "dss", "sha1" }) || name.startsWith("ssh-rsa-cert");
    case LIBSSH2_METHOD_CRYPT_CS: case LIBSSH2_METHOD_CRYPT_SC: return has({ "cbc", "3des", "arcfour", "blowfish", "cast", "rijndael" });
    case LIBSSH2_METHOD_MAC_CS: case LIBSSH2_METHOD_MAC_SC: return has({ "md5", "sha1", "ripemd" });
    default: return false;
    }
}
// Applies the policy to a fresh session; false if a list would be empty or libssh2 refuses it.
}  // namespace

bool applyAlgorithmPolicy(LIBSSH2_SESSION *session)
{
    for (const int method : { LIBSSH2_METHOD_KEX, LIBSSH2_METHOD_HOSTKEY, LIBSSH2_METHOD_CRYPT_CS, LIBSSH2_METHOD_CRYPT_SC,
                              LIBSSH2_METHOD_MAC_CS, LIBSSH2_METHOD_MAC_SC }) {
        const char **supported = nullptr;
        const int count = libssh2_session_supported_algs(session, method, &supported);
        if (count <= 0 || !supported) return false;
        QByteArrayList allowed;
        for (int i = 0; i < count; ++i)
            if (!obsoleteAlgorithm(method, QByteArray(supported[i]))) allowed << QByteArray(supported[i]);
        libssh2_free(session, static_cast<void *>(supported));
        if (method == LIBSSH2_METHOD_HOSTKEY) {
            // The host-key type decides the fingerprint a user has pinned. Keep the order that libssh2 1.11.1 negotiated (ECDSA before
            // Ed25519 before RSA/SHA-2) whatever the order of the library in use is, so that saved pins survive a libssh2 update.
            static const QByteArrayList preferred = { "ecdsa-sha2-nistp256", "ecdsa-sha2-nistp384", "ecdsa-sha2-nistp521", "ssh-ed25519",
                                                      "rsa-sha2-512", "rsa-sha2-256" };
            QByteArrayList ordered;
            for (const QByteArray &name : preferred) if (allowed.contains(name)) ordered << name;
            for (const QByteArray &name : allowed) if (!ordered.contains(name)) ordered << name;
            allowed = ordered;
        }
        if (allowed.isEmpty() || libssh2_session_method_pref(session, method, allowed.join(',').constData()) != 0) return false;
    }
    return true;
}
}  // namespace fortiq
