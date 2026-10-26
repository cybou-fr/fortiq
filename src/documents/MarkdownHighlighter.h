#pragma once
#include <QObject>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QtQml/qqmlregistration.h>

// Colours the Markdown source in the MDok editor: headings, emphasis, inline code, fenced code blocks, links, quotes, list markers.
// It only paints (character formats): it never changes the text, loads nothing and follows no link.
class MarkdownSyntax : public QSyntaxHighlighter {
public:
    explicit MarkdownSyntax(QTextDocument *document);
    static constexpr int kMaxLineLength = 4000;   // a longer line is left plain: no regular expression ever runs over a hostile megabyte
protected:
    void highlightBlock(const QString &text) override;
private:
    QTextCharFormat m_heading, m_code, m_fence, m_bold, m_italic, m_link, m_quote, m_marker, m_rule;
};

class MarkdownHighlighter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
public:
    explicit MarkdownHighlighter(QObject *parent = nullptr) : QObject(parent) {}
    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
signals:
    void documentChanged();
private:
    QQuickTextDocument *m_document = nullptr;
    MarkdownSyntax *m_syntax = nullptr;
};
