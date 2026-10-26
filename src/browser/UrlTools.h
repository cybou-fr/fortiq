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

// What the user typed is a search, not an address: it has white space, or it is a single word with no dot, colon or slash
// ("localhost" and bracketed IPv6 are addresses). An address with a scheme is never a search.
bool looksLikeSearch(const QString &typed);

// A search address from an engine template ("https://host/search?q=%s"): the query is percent-encoded and replaces %s.
// Empty when the result would exceed the address limit.
QString searchUrl(const QString &engineTemplate, const QString &query);

// One field of a GET form: name, kind ("text" is typed by the user, "hidden" is sent as the page gave it) and its value.
struct FormValue {
    QString name;
    QString value;
};

// The address a GET form submits to: action + "?" + the percent-encoded fields (action's own query is kept). Empty when the
// action is not an http(s) address or the result would exceed the address limit. Never follows a POST: callers only pass GET forms.
QString formUrl(const QString &action, const QList<FormValue> &values);

}  // namespace fortiq
