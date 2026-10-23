#pragma once
// Internal to the RemoteController implementation units (Remote*.cpp).
#include "RemoteController.h"
#include "SecretStore.h"
#include "TaskManager.h"
#include "LocalDocument.h"
#include "RemotePath.h"
#include "../ssh/SshClient.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QPointer>
#include <QLocale>
#include <QStringDecoder>
#include <QSettings>
#include <QUrl>
#include <QtConcurrentRun>

namespace fortiq {
namespace remote_detail {
struct Outcome {
    SshResult result;
    int elapsedMs = 0;
};
inline constexpr qint64 kTextLimit = 1024 * 1024;
inline constexpr qint64 kImageLimit = 16 * 1024 * 1024;

inline QString imageMime(const QString &path) {
    const QString ext = path.section('.', -1).toLower();
    static const QHash<QString, QString> types {
        { "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" },
        { "gif", "image/gif" }, { "webp", "image/webp" }, { "bmp", "image/bmp" },
        { "svg", "image/svg+xml" }, { "ico", "image/x-icon" } };
    return path.section('/', -1).contains('.') ? types.value(ext) : QString();
}
inline QString hexDump(const QByteArray &bytes) {
    QString out;
    for (qsizetype row = 0; row < bytes.size(); row += 16) {
        const QByteArray chunk = bytes.mid(row, 16);
        QString ascii;
        for (char ch : chunk) ascii += (ch >= 32 && ch < 127) ? QChar(ch) : QChar('.');
        out += QStringLiteral("%1  %2 %3\n").arg(row, 8, 16, QChar('0'))
                   .arg(QString::fromLatin1(chunk.toHex(' ')), -48).arg(ascii);
    }
    return out;
}
inline QString humanSize(qint64 bytes) {
    if (bytes < 0) return QStringLiteral("taille inconnue");
    return QLocale(QLocale::French).formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}
}  // namespace remote_detail
using namespace remote_detail;
}  // namespace fortiq
