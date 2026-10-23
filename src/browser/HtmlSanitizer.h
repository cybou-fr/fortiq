#pragma once

#include <QString>
#include <QStringList>

namespace fortiq {

// Turns untrusted HTML into a small, inert subset that Qt's rich text can
// show. There is no JavaScript, no CSS, no forms and no automatic loading of
// anything: images, frames, objects, media, styles and scripts are removed
// (images become their alt text), and only http(s) links survive, resolved
// against the page address. The output contains no attribute except `href`
// (on links) and `colspan`/`rowspan`/`start` (numbers).
struct SanitizedPage {
    QString title;
    QString html;          // safe subset, ready for a RichText item
    QString plainText;     // text only, for statistics and the "empty page" check
    QString refreshUrl;    // <meta http-equiv=refresh> target (never followed automatically)
    int linkCount = 0;
    int scriptCount = 0;   // <script> elements that were not run
    int imageCount = 0;    // images replaced by their alt text
    bool readerExtracted = false;  // true when only <main>/<article> was kept
    bool truncated = false;        // output size limit reached
};

// reader: keep only <main> / <article> when the page has one, and drop
// navigation, headers, footers and sidebars otherwise.
SanitizedPage sanitizeHtml(const QString &html, const QString &baseUrl, bool reader);

// Decodes HTML entities (named subset + numeric) in a plain string.
QString decodeEntities(const QString &text);

}  // namespace fortiq
