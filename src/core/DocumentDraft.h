#pragma once
// MDok: server-side drafts and the conflict screen. Pure functions (no I/O), unit-tested in tests/MarkdownDataTest.cpp.
//
// A draft lives ONLY on the server the document came from, in ~/.cache/fortiq-mdok/<id>.md (+ <id>.meta), mode 0600, written
// through the same SSH link as everything else (no local persistent cache, no extra network path). The client never reads a
// draft except through the open document's own server.
#include <QByteArray>
#include <QCryptographicHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace fortiq {

inline constexpr int kDraftDiffMaxLines = 3000;

// Serialize a manual file Save after any pending draft upload. This prevents
// the server-side draft from being rewritten after a successful Save deletes it.
// Callers must not silently drop the user's Save request: refuse with an
// explicit status and let them retry after the background upload finishes.
inline bool pendingManualSaveMayRun(bool userRequested, bool draftConfirmed,
                                    bool sourceStillValid, bool resultUnknown) {
    return userRequested && draftConfirmed && sourceStillValid && !resultUnknown;
}

inline bool fileSaveCanStart(bool documentOperationBusy, bool draftUploadPending) {
    return !documentOperationBusy && !draftUploadPending;
}

// A successful draft upload covers exactly the immutable editor snapshot it sent.
// If editing continued meanwhile, a new autosave is required before saying saved.
// Distinguish a current-server acknowledgement from an obsolete completion.
// In particular, a delayed upload from a previous document must never update
// the currently displayed draft status or trigger another write.
inline bool draftCompletionBelongsToDocument(const QString &submittedPath,
                                             const QString &currentPath,
                                             int submittedToken, int currentToken,
                                             const QString &submittedServer,
                                             const QString &currentServer,
                                             bool connected, int submittedEpoch, int currentEpoch) {
    return connected && submittedEpoch == currentEpoch && submittedToken == currentToken &&
           submittedServer == currentServer && submittedPath == currentPath;
}

inline bool draftNeedsAnotherSave(const QString &submittedText, const QString &currentText,
                                 bool dirty, bool resultUnknown) {
    return dirty && !resultUnknown && submittedText != currentText;
}
   // longer documents get a summary, not a line diff

// Draft id: a hash of the absolute document path (the file name on the server never contains the path itself).
inline QString draftId(const QString &documentPath) {
    return QString::fromLatin1(QCryptographicHash::hash(documentPath.toUtf8(), QCryptographicHash::Sha256).toHex());
}

struct DraftMeta {
    QString path;          // document path the draft belongs to
    QString baseHashHex;   // SHA-256 (hex) of the server version the draft was based on
    qint64 savedAtSecs = 0;
    bool bom = false, crlf = false;
};

inline QByteArray draftMetaText(const DraftMeta &m) {
    return "path=" + m.path.toUtf8().toBase64() + "\nbase=" + m.baseHashHex.toLatin1() + "\nsaved=" + QByteArray::number(m.savedAtSecs)
           + "\nbom=" + (m.bom ? "1" : "0") + "\ncrlf=" + (m.crlf ? "1" : "0") + "\n";
}

// "" meta (or any malformed line) -> invalid: ok=false. The meta is data from the server: every field is validated.
inline bool parseDraftMeta(const QByteArray &text, DraftMeta &out) {
    DraftMeta m;
    bool havePath = false, haveBase = false;
    for (const QByteArray &line : text.split('\n')) {
        const qsizetype eq = line.indexOf('=');
        if (eq <= 0) continue;
        const QByteArray key = line.left(eq), value = line.mid(eq + 1).trimmed();
        if (key == "path") { m.path = QString::fromUtf8(QByteArray::fromBase64(value)); havePath = !m.path.isEmpty() && m.path.startsWith(QLatin1Char('/')); }
        else if (key == "base") {
            static const QByteArray hex = "0123456789abcdef";
            bool ok = value.size() == 64;
            for (const char c : value) ok = ok && hex.contains(c);
            if (ok) { m.baseHashHex = QString::fromLatin1(value); haveBase = true; }
        }
        else if (key == "saved") m.savedAtSecs = value.toLongLong();
        else if (key == "bom") m.bom = value == "1";
        else if (key == "crlf") m.crlf = value == "1";
    }
    if (!havePath || !haveBase) return false;
    out = m;
    return true;
}

// ---- line diff for the conflict screen ----------------------------------------------------------------------------------
// Longest-common-subsequence over lines (documents are limited to 256 KiB). `mine` -> `server`: "-" = only in my version,
// "+" = only on the server, " " = common. Unchanged runs far from a change are folded into "…" so the screen stays short.
struct DiffSummary { int onlyMine = 0, onlyServer = 0; bool tooLong = false; QString text; };

inline DiffSummary lineDiff(const QString &mine, const QString &server, int context = 2) {
    DiffSummary out;
    const QStringList a = mine.split(QLatin1Char('\n')), b = server.split(QLatin1Char('\n'));
    if (a.size() > kDraftDiffMaxLines || b.size() > kDraftDiffMaxLines) {
        out.tooLong = true;
        out.text = QStringLiteral("Documents trop longs pour un comparatif ligne à ligne (%1 / %2 lignes).").arg(a.size()).arg(b.size());
        return out;
    }
    const int n = a.size(), m = b.size();
    QList<int> table((n + 1) * (m + 1), 0);
    auto at = [&](int i, int j) -> int & { return table[i * (m + 1) + j]; };
    for (int i = n - 1; i >= 0; --i)
        for (int j = m - 1; j >= 0; --j)
            at(i, j) = a[i] == b[j] ? at(i + 1, j + 1) + 1 : qMax(at(i + 1, j), at(i, j + 1));
    struct Op { QChar kind; QString line; };
    QList<Op> ops;
    int i = 0, j = 0;
    while (i < n && j < m) {
        if (a[i] == b[j]) { ops.push_back({ QLatin1Char(' '), a[i] }); ++i; ++j; }
        else if (at(i + 1, j) >= at(i, j + 1)) { ops.push_back({ QLatin1Char('-'), a[i] }); ++i; ++out.onlyMine; }
        else { ops.push_back({ QLatin1Char('+'), b[j] }); ++j; ++out.onlyServer; }
    }
    while (i < n) { ops.push_back({ QLatin1Char('-'), a[i++] }); ++out.onlyMine; }
    while (j < m) { ops.push_back({ QLatin1Char('+'), b[j++] }); ++out.onlyServer; }
    QList<bool> keep(ops.size(), false);
    for (int k = 0; k < ops.size(); ++k)
        if (ops[k].kind != QLatin1Char(' '))
            for (int c = qMax(0, k - context); c <= qMin(int(ops.size()) - 1, k + context); ++c) keep[c] = true;
    bool folded = false;
    for (int k = 0; k < ops.size(); ++k) {
        if (!keep[k]) { if (!folded) { out.text += QStringLiteral("  …\n"); folded = true; } continue; }
        folded = false;
        out.text += ops[k].kind + QLatin1Char(' ') + ops[k].line + QLatin1Char('\n');
    }
    return out;
}

// ---- server scripts (POSIX sh on stdin of `sh -s`; every value is data: ids are hex, the text travels as base64) ----------
inline bool validDraftId(const QString &id) {
    if (id.size() != 64) return false;
    for (const QChar c : id) if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f')))) return false;
    return true;
}

inline QByteArray buildDraftWriteScript(const QString &id, const QByteArray &meta, const QByteArray &documentBytes, const QString &token) {
    if (!validDraftId(id) || token.isEmpty()) return {};
    const QByteArray d = "FQD" + token.toLatin1();
    QByteArray s = "umask 077\nexport LC_ALL=C.UTF-8\ndir=\"${XDG_CACHE_HOME:-$HOME/.cache}/fortiq-mdok\"\n"
                   "mkdir -p \"$dir\" && chmod 700 \"$dir\" || { echo 'FQ-ERR mkdir' >&2; exit 5; }\n"
                   "t1=$(mktemp \"$dir/.w.XXXXXX\") && t2=$(mktemp \"$dir/.w.XXXXXX\") || { echo 'FQ-ERR mktemp' >&2; exit 5; }\n"
                   "base64 -d > \"$t1\" <<'" + d + "M'\n" + meta.toBase64() + "\n" + d + "M\n"
                   "base64 -d > \"$t2\" <<'" + d + "B'\n" + documentBytes.toBase64() + "\n" + d + "B\n"
                   "chmod 600 \"$t1\" \"$t2\"\n"
                   // the document first, the meta last: a reader that finds the meta always finds a complete document
                   "mv -f \"$t2\" \"$dir/" + id.toLatin1() + ".md\" && mv -f \"$t1\" \"$dir/" + id.toLatin1() + ".meta\" || { rm -f \"$t1\" \"$t2\"; echo 'FQ-ERR move' >&2; exit 8; }\n"
                   "echo '#ok'\n";
    return s;
}

inline QByteArray buildDraftReadScript(const QString &id) {
    if (!validDraftId(id)) return {};
    const QByteArray f = id.toLatin1();
    return "export LC_ALL=C.UTF-8\ndir=\"${XDG_CACHE_HOME:-$HOME/.cache}/fortiq-mdok\"\n"
           "if [ -f \"$dir/" + f + ".meta\" ] && [ -f \"$dir/" + f + ".md\" ]; then\n"
           "  echo '#meta'; head -c 4096 \"$dir/" + f + ".meta\"; echo '#doc'; head -c 400000 \"$dir/" + f + ".md\" | base64 -w0; echo; echo '#end'\n"
           "else echo '#none'; fi\n";
}

inline QByteArray buildDraftDeleteScript(const QString &id) {
    if (!validDraftId(id)) return {};
    return "dir=\"${XDG_CACHE_HOME:-$HOME/.cache}/fortiq-mdok\"\nrm -f \"$dir/" + id.toLatin1() + ".md\" \"$dir/" + id.toLatin1() + ".meta\"\necho '#deleted'\n";
}

struct DraftRead { bool found = false; DraftMeta meta; QByteArray documentBytes; };

inline DraftRead parseDraftRead(const QByteArray &output) {
    DraftRead r;
    const qsizetype m = output.indexOf("#meta\n"), d = output.indexOf("#doc\n"), e = output.indexOf("#end");
    if (m < 0 || d < m || e < d) return r;
    if (!parseDraftMeta(output.mid(m + 6, d - m - 6), r.meta)) return r;
    r.documentBytes = QByteArray::fromBase64(output.mid(d + 5, e - d - 5).trimmed());
    r.found = true;
    return r;
}

}  // namespace fortiq
