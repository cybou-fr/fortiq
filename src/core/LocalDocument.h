#pragma once
#include <QIODevice>
#include <QUrl>
#include <functional>
#include <memory>

namespace fortiq {
// SAF identifiers are opaque URLs, never decoded into filesystem paths.
QString localDocumentPath(const QUrl &url);
bool isContentDocument(const QString &path);
QString localDocumentName(const QString &path);
struct DocumentExportResult {
    bool ok = false;
    bool attempted = false; // provider may have changed once opening is attempted
    bool cancelled = false;
};
// Called only with a fully received, verified staging file. The factory is
// deliberately deferred until the staging stream can be rewound and cancel checked.
DocumentExportResult exportDocument(QIODevice &staging,
    const std::function<std::unique_ptr<QIODevice>()> &openDestination,
    const std::function<bool()> &keepGoing = {});
}
