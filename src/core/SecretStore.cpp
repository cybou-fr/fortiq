#include "SecretStore.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#include <string>
#endif

namespace fortiq {

#ifdef Q_OS_WIN

namespace {
std::wstring target(const QString &key) { return L"FORTIQ/ssh/" + key.toStdWString(); }

QString winError(DWORD code) {
    return QStringLiteral("Windows Credential Manager error %1").arg(code);
}
}

bool SecretStore::available() { return true; }
QString SecretStore::unavailableReason() { return {}; }

SecretStore::Result SecretStore::save(const QString &key, const QString &secret) {
    Result r;
    if (key.isEmpty() || secret.isEmpty()) { r.error = QStringLiteral("Nothing to store."); return r; }
    QByteArray blob = secret.toUtf8();
    if (blob.size() > int(CRED_MAX_CREDENTIAL_BLOB_SIZE)) {
        blob.fill('\0');
        r.error = QStringLiteral("The secret is too long for the Windows Credential Manager.");
        return r;
    }
    const std::wstring name = target(key);
    CREDENTIALW credential {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(name.c_str());
    credential.UserName = const_cast<LPWSTR>(L"fortiq");
    credential.CredentialBlobSize = DWORD(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE; // this user, this PC; never roamed
    const BOOL written = CredWriteW(&credential, 0);
    const DWORD code = written ? 0 : GetLastError();
    blob.fill('\0');
    if (!written) { r.error = winError(code); return r; }
    r.ok = true;
    return r;
}

SecretStore::Result SecretStore::load(const QString &key) {
    Result r;
    PCREDENTIALW credential = nullptr;
    const std::wstring name = target(key);
    if (!CredReadW(name.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND) { r.notFound = true; r.error = QStringLiteral("No stored secret for this server."); }
        else r.error = winError(code);
        return r;
    }
    r.secret = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob), int(credential->CredentialBlobSize));
    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    r.ok = true;
    return r;
}

SecretStore::Result SecretStore::remove(const QString &key) {
    Result r;
    const std::wstring name = target(key);
    if (CredDeleteW(name.c_str(), CRED_TYPE_GENERIC, 0)) { r.ok = true; return r; }
    const DWORD code = GetLastError();
    if (code == ERROR_NOT_FOUND) { r.ok = true; r.notFound = true; return r; }
    r.error = winError(code);
    return r;
}

#else

bool SecretStore::available() { return false; }
QString SecretStore::unavailableReason() {
    return QStringLiteral("No protected secret store is available on this platform yet; the password is not saved.");
}
SecretStore::Result SecretStore::save(const QString &, const QString &) {
    Result r; r.unavailable = true; r.error = unavailableReason(); return r;
}
SecretStore::Result SecretStore::load(const QString &) {
    Result r; r.unavailable = true; r.error = unavailableReason(); return r;
}
SecretStore::Result SecretStore::remove(const QString &) {
    Result r; r.ok = true; r.notFound = true; return r;   // nothing can have been stored
}

#endif

} // namespace fortiq
