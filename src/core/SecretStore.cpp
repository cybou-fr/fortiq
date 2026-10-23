#include "SecretStore.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#include <string>
#endif
#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QSettings>
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

#elif defined(Q_OS_ANDROID)

// Android: the key is an AES-256-GCM key inside the Android Keystore
// (fr.cybou.fortiq.SecretVault); only ciphertext is kept in the app's private
// settings, bound to the server id. Without the Keystore key it is useless.
namespace {
constexpr const char *kVault = "fr/cybou/fortiq/SecretVault";

QByteArray aadFor(const QString &key) { return (QStringLiteral("fortiq/ssh/") + key).toUtf8(); }

jbyteArray toJava(QJniEnvironment &env, const QByteArray &bytes) {
    jbyteArray array = env->NewByteArray(bytes.size());
    if (array) env->SetByteArrayRegion(array, 0, bytes.size(), reinterpret_cast<const jbyte *>(bytes.constData()));
    return array;
}

QByteArray fromJava(QJniEnvironment &env, jbyteArray array) {
    QByteArray out;
    if (!array) return out;
    out.resize(env->GetArrayLength(array));
    env->GetByteArrayRegion(array, 0, out.size(), reinterpret_cast<jbyte *>(out.data()));
    return out;
}

QString vaultError() {
    const QJniObject text = QJniObject::callStaticObjectMethod(kVault, "lastError", "()Ljava/lang/String;");
    const QString message = text.isValid() ? text.toString() : QString();
    return message.isEmpty() ? QStringLiteral("Android Keystore is not available") : message;
}

// Runs encrypt/decrypt on the Java side; returns false with `error` set on failure.
bool vault(const char *method, const QByteArray &aad, const QByteArray &input, QByteArray &output, QString &error) {
    QJniEnvironment env;
    jbyteArray jAad = toJava(env, aad);
    jbyteArray jInput = toJava(env, input);
    if (!jAad || !jInput) {
        if (jAad) env->DeleteLocalRef(jAad);
        if (jInput) env->DeleteLocalRef(jInput);
        error = QStringLiteral("Android Keystore call could not be prepared");
        return false;
    }
    const QJniObject result = QJniObject::callStaticObjectMethod(kVault, method, "([B[B)[B", jAad, jInput);
    env->DeleteLocalRef(jAad);
    env->DeleteLocalRef(jInput);
    if (env.checkAndClearExceptions() || !result.isValid()) { error = vaultError(); return false; }
    output = fromJava(env, result.object<jbyteArray>());
    return true;
}

QString settingsKey(const QString &key) { return QStringLiteral("secrets/") + key; }
}

bool SecretStore::available() { return true; }
QString SecretStore::unavailableReason() { return {}; }

SecretStore::Result SecretStore::save(const QString &key, const QString &secret) {
    Result r;
    if (key.isEmpty() || secret.isEmpty()) { r.error = QStringLiteral("Nothing to store."); return r; }
    QByteArray plain = secret.toUtf8();
    QByteArray blob;
    const bool ok = vault("encrypt", aadFor(key), plain, blob, r.error);
    plain.fill('\0');
    if (!ok) return r;
    QSettings settings;
    settings.setValue(settingsKey(key), blob.toBase64());
    settings.sync();
    if (settings.status() != QSettings::NoError) { r.error = QStringLiteral("Could not write the encrypted secret"); return r; }
    r.ok = true;
    return r;
}

SecretStore::Result SecretStore::load(const QString &key) {
    Result r;
    QSettings settings;
    if (!settings.contains(settingsKey(key))) { r.notFound = true; r.error = QStringLiteral("No stored secret for this server."); return r; }
    const QByteArray blob = QByteArray::fromBase64(settings.value(settingsKey(key)).toByteArray());
    QByteArray plain;
    if (!vault("decrypt", aadFor(key), blob, plain, r.error)) return r;
    r.secret = QString::fromUtf8(plain);
    plain.fill('\0');
    r.ok = true;
    return r;
}

SecretStore::Result SecretStore::remove(const QString &key) {
    Result r;
    QSettings settings;
    r.notFound = !settings.contains(settingsKey(key));
    settings.remove(settingsKey(key));
    settings.sync();
    r.ok = true;
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
