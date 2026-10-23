#include "RemoteControllerInternal.h"

namespace fortiq {
void RemoteController::openFile(const QString &path) {
    if (!m_connected || m_saving) return;
    m_editHash.clear();
    const auto clean = cleanAbsolutePath(path);
    if (!clean) return;
    const int seq = ++m_previewSeq;
    const int token = m_generation;
    const QString mime = imageMime(*clean);
    m_previewPath = *clean;
    m_previewKind.clear();
    m_previewText.clear();
    m_previewImage.clear();
    m_previewInfo = QStringLiteral("Lecture…");
    m_previewBusy = true;
    emit previewChanged();

    ConnectionRequest req;
    req.host = m_host; req.port = m_port; req.user = m_user;
    req.password = m_password; req.keyPath = m_keyPath;
    req.expectedHostFingerprint = QSettings{}.value(pinKey()).toString();
    req.directory = m_currentDirectory;
    const qint64 limit = mime.isEmpty() ? kTextLimit : kImageLimit + 1;
    auto slot = m_session;
    auto *watcher = new QFutureWatcher<SshResult>(this);
    connect(watcher, &QFutureWatcher<SshResult>::finished, this, [this, watcher, seq, token, mime] {
        SshResult r = watcher->result();
        watcher->deleteLater();
        if (token != m_generation || seq != m_previewSeq || m_previewPath.isEmpty()) return;
        m_previewBusy = false;
        if (r.status != SshResult::Status::Ok) {
            m_previewKind = QStringLiteral("error");
            m_previewInfo = r.message;
        } else if (!mime.isEmpty()) {
            if (r.truncated) {
                m_previewKind = QStringLiteral("error");
                m_previewInfo = QStringLiteral("Image trop volumineuse pour l'aperçu (%1, limite 16 Mo).").arg(humanSize(r.fileSize));
            } else {
                m_previewKind = QStringLiteral("image");
                m_previewImage = QStringLiteral("data:%1;base64,").arg(mime) + QString::fromLatin1(r.data.toBase64());
                m_previewInfo = humanSize(r.data.size());
            }
        } else {
            QByteArray sample = r.data;
            // A truncated read may cut a UTF-8 sequence; drop the partial tail.
            if (r.truncated) {
                int back = 0;
                while (back < 4 && back < sample.size()
                       && (static_cast<uchar>(sample.at(sample.size() - 1 - back)) & 0xC0) == 0x80) ++back;
                if (back < sample.size() && static_cast<uchar>(sample.at(sample.size() - 1 - back)) >= 0xC0)
                    sample.chop(back + 1);
            }
            QStringDecoder decoder(QStringDecoder::Utf8);
            QString text = decoder.decode(sample);
            if (decoder.hasError() || r.data.left(8192).contains('\0')) {
                m_previewKind = QStringLiteral("binary");
                m_previewText = hexDump(r.data.left(1024));
                m_previewInfo = QStringLiteral("Fichier binaire, %1 — premiers 1 Ko en hexadécimal").arg(humanSize(r.fileSize));
            } else {
                m_previewKind = QStringLiteral("text");
                m_previewText = std::move(text);
                // Only complete files are editable; remember their exact version.
                if (!r.truncated) {
                    m_editHash = QCryptographicHash::hash(r.data, QCryptographicHash::Sha256);
                    m_editCrlf = r.data.contains("\r\n");
                    m_editBom = r.data.startsWith("\xEF\xBB\xBF");
                }
                m_previewInfo = r.truncated
                    ? QStringLiteral("%1 — aperçu du premier Mo").arg(humanSize(r.fileSize))
                    : QStringLiteral("%1 — %2 lignes").arg(humanSize(r.fileSize)).arg(m_previewText.count('\n') + (m_previewText.endsWith('\n') ? 0 : 1));
            }
        }
        emit previewChanged();
    });
    const QString target = *clean;
    watcher->setFuture(QtConcurrent::run(&m_pool, [slot, req, target, limit] {
        if (!*slot) *slot = std::make_unique<SshSession>();
        SshSession &session = **slot;
        if (!session.isOpen()) {
            const SshResult reopened = session.open(req); // pin re-checked
            if (reopened.status != SshResult::Status::Ok) return reopened;
        }
        SshResult r = session.readFile(target, limit);
        if (!session.isOpen() && session.open(req).status == SshResult::Status::Ok)
            r = session.readFile(target, limit);
        return r;
    }));
}

void RemoteController::closePreview() {
    ++m_previewSeq;
    m_editHash.clear();
    m_previewBusy = false;
    m_previewPath.clear();
    m_previewKind.clear();
    m_previewText.clear();
    m_previewImage.clear();
    m_previewInfo.clear();
    emit previewChanged();
}
}  // namespace fortiq
