#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Pure HTTP/1.x wire helpers of the Browser gateway (docs/BROWSER.md, "Passerelle SSH"). No sockets, no Qt event loop: everything
// here is unit-tested without a server. The gateway only understands what a browser engine sends to an HTTP proxy:
// `CONNECT host:port` and absolute-form requests; it is deliberately strict (request smuggling, header injection).
namespace fortiq::gateway {

constexpr int kMaxHeadBytes = 16 * 1024;    // request or response head, request line included
constexpr int kMaxHeaderLine = 8 * 1024;
constexpr int kMaxHeaderCount = 100;

struct Header {
    QByteArray name;    // as received (case kept); compare with iequals
    QByteArray value;   // without leading / trailing blanks
};

inline bool iequals(const QByteArray &a, const char *b) { return a.compare(b, Qt::CaseInsensitive) == 0; }

struct RequestHead {
    QByteArray method;
    QByteArray target;
    QByteArray version;   // "HTTP/1.0" | "HTTP/1.1"
    QList<Header> headers;
    QByteArray header(const char *name) const;               // first value, empty when absent
    int count(const char *name) const;
    bool hasToken(const char *name, const char *token) const;  // comma list, case-insensitive ("Connection: keep-alive, Upgrade")
};

struct ResponseHead {
    int status = 0;
    QByteArray version;
    QByteArray reason;
    QList<Header> headers;
    QByteArray header(const char *name) const;
    int count(const char *name) const;
    bool hasToken(const char *name, const char *token) const;
};

enum class ParseStatus { NeedMore, Ok, Bad, TooLarge };
struct HeadParse {
    ParseStatus status = ParseStatus::NeedMore;
    qsizetype consumed = 0;   // bytes of the head including the empty line (Ok)
    QString error;            // Bad / TooLarge
};

// Looks for the end of a head ("\r\n\r\n") in `buffer`; parses it when complete. Only CRLF line ends, no obsolete line folding,
// no NUL / stray CR / LF in a line, at most kMaxHeadBytes / kMaxHeaderCount.
HeadParse parseRequestHead(const QByteArray &buffer, RequestHead *out);
HeadParse parseResponseHead(const QByteArray &buffer, ResponseHead *out);

// "host:port" (CONNECT) or "[v6]:port"; the port is mandatory when defaultPort <= 0. Host characters are restricted to
// letters, digits, '.', '-', '_' and the colons of a bracketed IPv6 literal (punycode only: no raw Unicode, no '%', no '@').
struct Authority {
    bool ok = false;
    QString error;
    QString host;   // lower-case, without brackets
    int port = 0;
};
Authority parseAuthority(const QByteArray &text, int defaultPort);

// `http://host[:port]/path?query` (absolute-form). https:// is refused here (a client must CONNECT). User info is refused.
struct HttpTarget {
    bool ok = false;
    QString error;
    QString host;
    int port = 80;
    QByteArray path;   // origin-form, "/" at least
};
HttpTarget parseAbsoluteHttp(const QByteArray &uri);

// Proxy-Authorization: Basic base64("<user>:<token>"), compared in constant time. The credential text is wiped from `head`.
bool proxyAuthorized(RequestHead *head, const QByteArray &expectedHeaderValue);
QByteArray basicAuthValue(const QByteArray &user, const QByteArray &token);   // "Basic xxxx"

// ---- body framing ----
class BodyFramer {
public:
    enum class Mode { None, Length, Chunked, UntilClose };
    void reset(Mode mode, qint64 length = 0);
    // Number of bytes of data[offset..] that belong to this body, chunk framing and trailers included (the rest is the next message).
    qsizetype consume(const QByteArray &data, qsizetype offset = 0);
    bool done() const { return m_done; }
    bool failed() const { return m_failed; }
    Mode mode() const { return m_mode; }
private:
    enum class Chunk { SizeLine, Data, DataEnd, Trailer };
    Mode m_mode = Mode::None;
    bool m_done = true, m_failed = false;
    qint64 m_remaining = 0;
    Chunk m_chunk = Chunk::SizeLine;
    QByteArray m_line;
    int m_dataEnd = 0;       // CR LF after the chunk data: how many seen
    int m_trailerLine = 0;   // bytes of the current trailer line
};

struct Framing {
    bool ok = true;
    QString error;
    BodyFramer::Mode mode = BodyFramer::Mode::None;
    qint64 length = 0;
};
Framing requestFraming(const RequestHead &head);                              // Content-Length XOR chunked, or no body
Framing responseFraming(const ResponseHead &head, const QByteArray &requestMethod);

// ---- forwarding ----
// Origin-form request for the server: hop-by-hop and proxy headers removed, Host = the target, `Connection: close` (one channel per
// request: the response ends with the framing or with the close of the channel).
QByteArray serializeOriginRequest(const RequestHead &head, const HttpTarget &target);
// Response head for the client: Connection / Keep-Alive / Proxy-Connection replaced by our own decision.
QByteArray serializeResponseHead(const ResponseHead &head, bool keepAlive);

// A complete small error response (always `Connection: close`). `extra` = additional raw header lines without CRLF.
QByteArray errorResponse(int status, const QString &text, const QList<QByteArray> &extra = {});

}  // namespace fortiq::gateway
