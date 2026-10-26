#pragma once
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// QML face of TextSearch.h for the MDok editor: the window asks for matches and replaces them in the TextEdit itself (so Undo keeps working).
class DocumentSearch : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit DocumentSearch(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE QVariantList matches(const QString &text, const QString &query, bool caseSensitive, bool wholeWord) const;   // start offsets
    Q_INVOKABLE int step(const QVariantList &starts, int cursor, int queryLength, bool forward) const;
    Q_INVOKABLE QString replaceAll(const QString &text, const QString &query, const QString &replacement, bool caseSensitive, bool wholeWord) const;
    Q_INVOKABLE QVariantMap stats(const QString &text) const;                                                                 // words, characters, lines
};
