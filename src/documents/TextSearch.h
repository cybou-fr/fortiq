#pragma once
// MDok: find and replace over the plain text of the document. Pure functions (no Qt Quick), unit-tested in tests/MarkdownDataTest.cpp.
#include <QList>
#include <QString>

namespace fortiq {

struct SearchOptions { bool caseSensitive = false; bool wholeWord = false; };

inline constexpr int kSearchMaxMatches = 20000;   // enough for a 256 KiB document; a pathological query stops here

inline bool isWordChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

// Start offsets (UTF-16 units) of the non-overlapping matches, left to right. An empty query matches nothing.
inline QList<qsizetype> findAll(const QString &text, const QString &query, SearchOptions options = {}, int limit = kSearchMaxMatches) {
    QList<qsizetype> out;
    if (query.isEmpty() || text.isEmpty()) return out;
    const Qt::CaseSensitivity cs = options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    qsizetype from = 0;
    while (out.size() < limit) {
        const qsizetype at = text.indexOf(query, from, cs);
        if (at < 0) break;
        const qsizetype end = at + query.size();
        const bool wordOk = !options.wholeWord || ((at == 0 || !isWordChar(text.at(at - 1))) && (end >= text.size() || !isWordChar(text.at(end))));
        if (wordOk) { out << at; from = end; }
        else from = at + 1;
    }
    return out;
}

// Replaces every match; `count` receives their number. The replacement is literal (no back-references, no regular expressions).
inline QString replaceAll(const QString &text, const QString &query, const QString &replacement, SearchOptions options = {}, int *count = nullptr) {
    const QList<qsizetype> starts = findAll(text, query, options);
    if (count) *count = int(starts.size());
    if (starts.isEmpty()) return text;
    QString out;
    out.reserve(text.size() + starts.size() * qMax<qsizetype>(0, replacement.size() - query.size()));
    qsizetype pos = 0;
    for (const qsizetype s : starts) { out += QStringView(text).mid(pos, s - pos); out += replacement; pos = s + query.size(); }
    out += QStringView(text).mid(pos);
    return out;
}

// Index in `starts` of the next (or previous) match relative to a cursor offset, wrapping around; -1 when there are none.
// "Next" is the first match at or after `cursor`; "previous" the last match that ends before `cursor`.
inline int stepMatch(const QList<qsizetype> &starts, qsizetype cursor, qsizetype queryLength, bool forward) {
    if (starts.isEmpty()) return -1;
    if (forward) {
        for (int i = 0; i < starts.size(); ++i) if (starts[i] >= cursor) return i;
        return 0;
    }
    for (int i = int(starts.size()) - 1; i >= 0; --i) if (starts[i] + queryLength <= cursor) return i;
    return int(starts.size()) - 1;
}

struct TextStats { int words = 0, characters = 0, lines = 0; };

inline TextStats textStats(const QString &text) {
    TextStats s;
    s.characters = int(text.size());
    s.lines = text.isEmpty() ? 0 : int(text.count(QLatin1Char('\n'))) + (text.endsWith(QLatin1Char('\n')) ? 0 : 1);
    bool in = false;
    for (const QChar c : text) {
        if (c.isSpace()) in = false;
        else if (!in) { in = true; ++s.words; }
    }
    return s;
}

}  // namespace fortiq
