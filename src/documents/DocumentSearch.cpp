#include "DocumentSearch.h"
#include "TextSearch.h"

QVariantList DocumentSearch::matches(const QString &text, const QString &query, bool caseSensitive, bool wholeWord) const {
    QVariantList out;
    for (const qsizetype s : fortiq::findAll(text, query, { caseSensitive, wholeWord })) out << int(s);
    return out;
}
int DocumentSearch::step(const QVariantList &starts, int cursor, int queryLength, bool forward) const {
    QList<qsizetype> list;
    for (const QVariant &v : starts) list << v.toInt();
    return fortiq::stepMatch(list, cursor, queryLength, forward);
}
QString DocumentSearch::replaceAll(const QString &text, const QString &query, const QString &replacement, bool caseSensitive, bool wholeWord) const {
    return fortiq::replaceAll(text, query, replacement, { caseSensitive, wholeWord });
}
QVariantMap DocumentSearch::stats(const QString &text) const {
    const fortiq::TextStats s = fortiq::textStats(text);
    return { { "words", s.words }, { "characters", s.characters }, { "lines", s.lines } };
}
