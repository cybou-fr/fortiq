#include "LocalDocument.h"
#include <QFileDevice>
#include <QFileInfo>

namespace fortiq {
QString localDocumentPath(const QUrl &url) {
    if (!url.isValid() || url.isEmpty()) return {};
    if (url.isLocalFile()) return url.toLocalFile();
    if (url.scheme() == "content" && !url.host().isEmpty() && !url.hasFragment())
        return url.toString(QUrl::FullyEncoded);
    if (url.scheme().isEmpty()) return url.toString();
    return {}; // no HTTP/cloud fallback or arbitrary URL scheme
}
bool isContentDocument(const QString &path) { return QUrl(path).scheme() == "content"; }
QString localDocumentName(const QString &path) {
    // Qt's Android content engine queries DocumentsProvider DISPLAY_NAME.
    // Do not derive a name from an opaque /document/... identifier.
    return QFileInfo(path).fileName();
}
DocumentExportResult exportDocument(QIODevice &staging,
    const std::function<std::unique_ptr<QIODevice>()> &openDestination,
    const std::function<bool()> &keepGoing) {
    DocumentExportResult r;
    if (keepGoing && !keepGoing()) { r.cancelled = true; return r; }
    if (!staging.isOpen() || !staging.isReadable() || !staging.seek(0)) return r;
    r.attempted = true;
    auto out = openDestination();
    if (!out || !out->isOpen() || !out->isWritable()) return r;
    char buffer[65536];
    while (true) {
        if (keepGoing && !keepGoing()) { r.cancelled = true; return r; }
        const qint64 n = staging.read(buffer, sizeof(buffer));
        if (n < 0) return r;
        if (n == 0) break;
        qint64 offset = 0;
        while (offset < n) {
            const qint64 written = out->write(buffer + offset, n - offset);
            if (written <= 0) return r;
            offset += written;
        }
    }
    if (auto *file = qobject_cast<QFileDevice *>(out.get()); file && !file->flush()) return r;
    out->close();
    if (auto *file = qobject_cast<QFileDevice *>(out.get()); file && file->error() != QFileDevice::NoError) return r;
    r.ok = true;
    return r;
}
}
