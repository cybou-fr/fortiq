#pragma once

#include <QString>

#include "SecureWipe.h"

namespace fortiq {

// Protected storage for one SSH secret per saved server (password or key
// passphrase). Windows: Credential Manager (per-user DPAPI). Other platforms
// report "unavailable" explicitly: there is NO fallback to a plain file or to
// QSettings. Secrets are never logged and never passed through arguments.
class SecretStore {
public:
    struct Result {
        bool ok = false;
        bool notFound = false;   // load/remove: nothing stored under this key (not an error for remove)
        bool unavailable = false;
        SecretString secret;     // load only; wiped when the last holder dies
        QString error;           // user-safe text, never contains the secret
    };

    // Whether this platform has a protected store FORTIQ can use.
    static bool available();
    static QString unavailableReason();

    // `key` is the saved-server id; it is namespaced internally.
    static Result save(const QString &key, const QString &secret);
    static Result load(const QString &key);
    static Result remove(const QString &key);
};

} // namespace fortiq
