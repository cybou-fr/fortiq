// SPDX-License-Identifier: GPL-3.0-only
#include "OutgoingAttachment.h"
#include "MailAttachment.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace fortiq {
namespace {
bool validPath(const QString &path) {
    return path.startsWith('/') && path.size() <= 4096 && !path.contains(QChar::Null)
        && QDir::cleanPath(path) == path;
}
bool validName(const QString &name) {
    return !name.isEmpty() && name == sanitizeAttachmentName(name);
}
bool validMetadata(const QVariantMap &a) {
    static const QRegularExpression digits(QStringLiteral("^[0-9]{1,30}$"));
    static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{64}$"));
    bool ok = false;
    const qint64 size = a.value("size").toLongLong(&ok);
    if (!ok || size < 0 || size > OutgoingAttachmentLimit
        || !validPath(a.value("path").toString()) || !validName(a.value("name").toString())
        || !hash.match(a.value("sha256").toString()).hasMatch()) return false;
    for (const char *key : {"mtimeNs", "device", "inode"})
        if (!digits.match(a.value(key).toString()).hasMatch()) return false;
    return true;
}

QByteArray python(const QJsonObject &payload, const QByteArray &body) {
    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact).toBase64();
    return QByteArray(
        "export LC_ALL=C.UTF-8\n"
        "command -v python3 >/dev/null 2>&1 || { echo 'FQ-ERR missing-python3' >&2; exit 14; }\n"
        "timeout -k 5 30 python3 - <<'FQOUTGOINGPY'\n") + R"PY(import sys
try:
    import os, stat, json, base64, hashlib, mimetypes
    from email import policy
    from email.parser import BytesParser
    from email.message import EmailMessage
    if not hasattr(EmailMessage, 'add_attachment'):
        raise ImportError('email capability')
except ImportError:
    print('FQ-ERR missing-python-email', file=sys.stderr)
    sys.exit(14)
LIMIT = 10 * 1024 * 1024
def read_file(spec, remaining, verify):
    path = spec['path']
    if not os.path.isabs(path) or not stat.S_ISREG(os.lstat(path).st_mode):
        raise ValueError('not regular')
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as f:
        before = os.fstat(f.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size > remaining:
            raise ValueError('too large')
        content = f.read(remaining + 1)
        after = os.fstat(f.fileno())
    def signature(s):
        return (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
    if signature(before) != signature(after) or len(content) != before.st_size or len(content) > remaining:
        raise ValueError('changed while reading')
    result = dict(path=path, name=spec['name'], size=len(content),
                  mtimeNs=str(before.st_mtime_ns), device=str(before.st_dev),
                  inode=str(before.st_ino), sha256=hashlib.sha256(content).hexdigest())
    if verify and any(result[k] != spec[k] for k in result):
        raise ValueError('selected version changed')
    return result, content
try:
)PY" + "    payload = json.loads(base64.b64decode('" + data + "'))\n" + body
        + "except Exception:\n    print('FQ-ATTACH-REFUSED', file=sys.stderr)\n    sys.exit(15)\nFQOUTGOINGPY\n";
}
}

QByteArray buildOutgoingProbeScript(const QString &path, const QString &name, qint64 remaining) {
    if (!validPath(path) || !validName(name) || remaining < 0 || remaining > OutgoingAttachmentLimit) return {};
    return python({{"path", path}, {"name", name}, {"remaining", remaining}},
        "    info, content = read_file(payload, payload['remaining'], False)\n"
        "    print(json.dumps(info, ensure_ascii=True))\n");
}

QVariantMap parseOutgoingProbe(const QByteArray &data, const QString &path, const QString &name) {
    if (data.size() > 16384) return {};
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return {};
    const auto object = doc.object();
    if (!object.value("size").isDouble()) return {};
    const double size = object.value("size").toDouble();
    if (!(size >= 0 && size <= OutgoingAttachmentLimit) || size != qint64(size)) return {};
    QVariantMap out;
    for (const char *key : {"path", "name", "mtimeNs", "device", "inode", "sha256"})
        out.insert(QLatin1String(key), object.value(QLatin1String(key)).toString());
    out.insert("size", qint64(size));
    if (out.value("path").toString() != path || out.value("name").toString() != name || !validMetadata(out)) return {};
    return out;
}

QByteArray buildOutgoingMimeScript(const QVariantList &attachments) {
    if (attachments.isEmpty() || attachments.size() > OutgoingAttachmentCount) return {};
    qint64 total = 0;
    QStringList paths;
    for (const auto &value : attachments) {
        const auto a = value.toMap();
        if (!validMetadata(a) || paths.contains(a.value("path").toString())) return {};
        paths << a.value("path").toString();
        total += a.value("size").toLongLong();
        if (total > OutgoingAttachmentLimit) return {};
    }
    // The shell trap in MailSend owns and removes $t, also after Python failure.
    return "export FQ_MESSAGE_FILE=\"$t\"\n" + python({{"attachments", QJsonArray::fromVariantList(attachments)}}, R"PY(    target = os.environ['FQ_MESSAGE_FILE']
    with open(target, 'rb') as f:
        original = f.read(24 * 1024 * 1024 + 1)
    if len(original) > 24 * 1024 * 1024:
        raise ValueError('message too large')
    message = BytesParser(policy=policy.SMTP).parsebytes(original)
    if message.defects:
        raise ValueError('invalid message')
    remaining = LIMIT
    for spec in payload['attachments']:
        info, content = read_file(spec, remaining, True)
        remaining -= len(content)
        mime, encoding = mimetypes.guess_type(info['name'])
        mime = mime if mime and not encoding else 'application/octet-stream'
        major, minor = mime.split('/', 1)
        if major not in ('text', 'image', 'audio', 'video', 'application'):
            major, minor = 'application', 'octet-stream'
        message.add_attachment(content, maintype=major, subtype=minor, filename=info['name'])
    result = message.as_bytes()
    if len(result) > 24 * 1024 * 1024:
        raise ValueError('message too large')
    with open(target, 'wb') as f:
        f.write(result)
)PY") + "rc=$?\n[ \"$rc\" -eq 0 ] || exit 15\n";
}
}
