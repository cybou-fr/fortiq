#pragma once

#include <QString>
#include <QUrl>

namespace fortiq {

// Address handling for the Browser. The Browser only ever asks the Linux server
// (through curl) for http and https resources; everything else is refused here,
// before a command is built.
struct UrlCheck {
    bool ok = false;
    QString url;     // normalised absolute http(s) URL when ok
    QString error;   // user-facing reason when !ok
};

// Turns what the user typed into an absolute http(s) URL: trims, adds "https://"
// when no scheme is given, lower-cases scheme and host, drops the fragment.
// Refuses other schemes (file, ftp, gopher, javascript, data...), control
// characters, embedded credentials (user:pass@host), empty hosts and very long input.
UrlCheck normalizeUrl(const QString &input);

// Resolves a link found in a page against the page URL. Returns an empty string
// when the target must not be followed (javascript:, data:, mailto:, file:, an
// unparsable value) or when it is only a fragment of the current page.
QString resolveLink(const QString &base, const QString &href);

}  // namespace fortiq
