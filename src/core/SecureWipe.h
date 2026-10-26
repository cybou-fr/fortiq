#pragma once

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstring>

// Secrets in memory (S-6). Two problems with the obvious `buffer.fill(0)`:
//  1. the compiler may drop a store to memory that is freed right afterwards (dead-store elimination, worse with LTO);
//  2. QString/QByteArray are implicitly shared: `fill()` on a shared buffer DETACHES first, i.e. it copies the secret and zeroes
//     the copy, leaving the original untouched.
// secureWipe() cannot be optimized away. wipe(QString/QByteArray) zeroes the real buffer only when this object is its sole owner
// and otherwise just drops the reference: every holder wipes its own handle, so the LAST holder zeroes the buffer for real and
// no extra copy of the secret is ever made. SecretString/SecretBytes call it from the destructor.
namespace fortiq {

inline void secureWipe(void *data, std::size_t size) {
    if (!data || size == 0) return;
    // A volatile function pointer keeps the call (and so the store) from being removed, with or without LTO.
    static void *(*volatile const zero)(void *, int, std::size_t) = std::memset;
    zero(data, 0, size);
}

inline void wipe(QByteArray &bytes) {
    if (!bytes.isEmpty() && bytes.isDetached()) secureWipe(bytes.data(), static_cast<std::size_t>(bytes.size()));
    bytes.clear();
}

inline void wipe(QString &text) {
    if (!text.isEmpty() && text.isDetached()) secureWipe(const_cast<QChar *>(text.constData()), static_cast<std::size_t>(text.size()) * sizeof(QChar));
    text.clear();
}

// A QString / QByteArray that wipes itself when it dies (and when it is overwritten by `wipeNow()`).
class SecretString : public QString {
public:
    using QString::QString;
    SecretString() = default;
    SecretString(const QString &other) : QString(other) {}
    SecretString(const SecretString &) = default;
    SecretString(SecretString &&) = default;
    SecretString &operator=(const SecretString &) = default;
    SecretString &operator=(SecretString &&) = default;
    SecretString &operator=(const QString &other) { QString::operator=(other); return *this; }
    ~SecretString() { wipe(*this); }
    void wipeNow() { wipe(*this); }
};

class SecretBytes : public QByteArray {
public:
    using QByteArray::QByteArray;
    SecretBytes() = default;
    SecretBytes(const QByteArray &other) : QByteArray(other) {}
    SecretBytes(const SecretBytes &) = default;
    SecretBytes(SecretBytes &&) = default;
    SecretBytes &operator=(const SecretBytes &) = default;
    SecretBytes &operator=(SecretBytes &&) = default;
    SecretBytes &operator=(const QByteArray &other) { QByteArray::operator=(other); return *this; }
    ~SecretBytes() { wipe(*this); }
    void wipeNow() { wipe(*this); }
};

}  // namespace fortiq
