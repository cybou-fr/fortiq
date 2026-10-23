#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace fortiq {

// The Browser is a GUI over curl running on the selected Linux server (over
// SSH): every request leaves from the server's network, never from this
// device. This file builds the one POSIX sh script that does a fetch and parses
// what it prints. No secret is ever passed as a command argument: the URL is
// data inside a script sent on stdin.

constexpr qint64 kMaxBodyBytes = 5 * 1024 * 1024;     // enforced by curl --max-filesize and by the SSH output cap
constexpr qint64 kMaxFetchOutput = kMaxBodyBytes + 256 * 1024;  // body + headers + frames

// Script for `sh -s`. `url` must already be a normalised http(s) URL. Redirects are NOT followed by curl: the caller reads
// the Location and applies the address policy to every hop (UrlPolicy). `resolveSpec` ("host:port:ip", see curlResolveSpec)
// pins the connection to the address the policy approved; empty for IP literals. curl ignores ~/.curlrc (-q) and any proxy
// of the server's environment (--noproxy '*'), either of which could bypass the pin.
QByteArray buildCurlScript(const QString &url, const QString &resolveSpec = {});


struct CurlResult {
    bool framed = false;          // the output had the expected frames at all
    QString problem;              // "missing-curl", "mktemp", "protocol" ... when the run itself failed
    int curlExit = -1;            // curl's exit status; 0 = transfer completed
    QString curlMessage;          // curl's own error text (never contains the URL query secrets beyond what curl prints)
    int status = 0;               // final HTTP status
    QString statusText;
    QString effectiveUrl;
    QString contentType;
    qint64 sizeDownload = 0;
    int redirects = 0;
    QString remoteIp;
    QString httpVersion;
    int tlsVerify = -1;           // CURLINFO_SSL_VERIFY_RESULT: 0 = verified; -1 unknown (plain http)
    double timeDns = 0, timeConnect = 0, timeTls = 0, timeFirstByte = 0, timeTotal = 0;
    QVariantList headers;         // final response: [{name, value}]
    QVariantList redirectChain;   // earlier hops: [{url?, status, location}]
    QByteArray body;
    bool truncated = false;       // curl stopped at the size limit
};

// Parses what the script printed. Tolerant of binary bodies; never throws.
CurlResult parseCurlOutput(const QByteArray &out);

// The target of a redirect response (301/302/303/307/308) as written in its Location header, or empty.
QString redirectLocation(const CurlResult &r);

// Human text for a curl exit code ("Could not resolve host", "Timed out", ...).
QString describeCurlExit(int code);

// Decodes a body to text using the Content-Type charset, a BOM or (for HTML) a
// <meta charset>. UTF-8 is the default. Supports UTF-8/16, ISO-8859-1 and
// windows-1252 (no ICU needed); other legacy charsets are reported in
// `note` and shown as UTF-8 with replacement characters.
struct DecodedText {
    QString text;
    QString charset;   // what was used
    QString note;      // empty, or a warning
};
DecodedText decodeBody(const QByteArray &body, const QString &contentType, bool html);

// Value of a header (case-insensitive) in the final response, or empty.
QString headerValue(const QVariantList &headers, const QString &name);

}  // namespace fortiq
