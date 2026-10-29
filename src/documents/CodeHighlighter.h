#pragma once
#include <QObject>
#include <QQuickTextDocument>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QtQml/qqmlregistration.h>

// Colours source code in the Éditeur app: comments, strings, numbers and keywords, chosen by the file extension. Like the Markdown
// highlighter it only sets character formats: it never changes the text, loads nothing and runs no regular expression over a hostile line.
class CodeSyntax : public QSyntaxHighlighter {
public:
    enum class Language { Plain, CLike, Hash, Json, Markup, Sql };
    explicit CodeSyntax(QTextDocument *document);
    static Language languageForFile(const QString &fileName);
    void setLanguage(Language language);
    Language language() const { return m_language; }
    static constexpr int kMaxLineLength = 4000;   // a longer line is left plain
protected:
    void highlightBlock(const QString &text) override;
private:
    Language m_language = Language::Plain;
    QSet<QString> m_keywords;
    QTextCharFormat m_comment, m_string, m_number, m_keyword, m_tag;
};

class CodeHighlighter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY fileNameChanged)
public:
    explicit CodeHighlighter(QObject *parent = nullptr) : QObject(parent) {}
    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
    QString fileName() const { return m_fileName; }
    void setFileName(const QString &name);
signals:
    void documentChanged();
    void fileNameChanged();
private:
    QQuickTextDocument *m_document = nullptr;
    CodeSyntax *m_syntax = nullptr;
    QString m_fileName;
};
