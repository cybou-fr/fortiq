#include "CodeHighlighter.h"

#include <QFileInfo>

namespace {
constexpr int kInBlockComment = 1;

QTextCharFormat colour(const char *rgb, bool bold = false, bool italic = false) {
    QTextCharFormat f;
    f.setForeground(QColor(QLatin1String(rgb)));
    if (bold) f.setFontWeight(QFont::Bold);
    if (italic) f.setFontItalic(true);
    return f;
}

QSet<QString> words(const char *list) {
    QSet<QString> set;
    for (const QString &w : QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts)) set.insert(w);
    return set;
}

const char *kCLike =
    "alignas alignof auto bool break case catch char class const constexpr continue default delete do double else enum explicit export "
    "extern false final finally float for friend function goto if import in inline instanceof int interface let long namespace new "
    "nullptr null operator override package private protected public readonly return short signed sizeof static struct super switch "
    "template this throw throws true try typedef typeof union unsigned using var virtual void volatile while yield async await "
    "fn impl mut pub use mod match loop trait where type func go defer chan select range property signal as from of";
const char *kHash =
    "and as assert async await break case class continue def del do done elif else elsif end esac except False fi finally for from "
    "function global if import in is lambda None nonlocal not or pass raise return select then True try until while with yield "
    "let local export readonly source echo true false null";
const char *kSql =
    "select from where insert into values update set delete create table drop alter add index primary key foreign references join "
    "inner left right outer on group by order having limit offset union all distinct as and or not null is in like between exists "
    "case when then else end begin commit rollback true false";
}  // namespace

CodeSyntax::CodeSyntax(QTextDocument *document) : QSyntaxHighlighter(document) {
    m_comment = colour("#7F8FA9", false, true);
    m_string = colour("#A0E8AF");
    m_number = colour("#F4A261");
    m_keyword = colour("#7CC4FF", true);
    m_tag = colour("#C792EA");
}

CodeSyntax::Language CodeSyntax::languageForFile(const QString &fileName) {
    const QString name = QFileInfo(fileName).fileName().toLower();
    const QString ext = QFileInfo(name).suffix();
    static const QSet<QString> clike = words("c h cc cpp cxx hpp hh hxx cs java js mjs cjs ts tsx jsx go rs swift kt kts php qml scala dart css scss less");
    static const QSet<QString> hash = words("py sh bash zsh fish yml yaml toml ini conf cfg env rb pl pm r mk cmake service timer socket properties");
    static const QSet<QString> markup = words("html htm xml svg xhtml xsl vue");
    if (clike.contains(ext)) return Language::CLike;
    if (ext == QLatin1String("json") || ext == QLatin1String("jsonc")) return Language::Json;
    if (markup.contains(ext)) return Language::Markup;
    if (ext == QLatin1String("sql")) return Language::Sql;
    if (hash.contains(ext) || name == QLatin1String("makefile") || name == QLatin1String("dockerfile") || name.startsWith(QLatin1String(".bash"))
        || name == QLatin1String(".gitignore") || name == QLatin1String("crontab")) return Language::Hash;
    return Language::Plain;
}

void CodeSyntax::setLanguage(Language language) {
    if (language == m_language) return;
    m_language = language;
    switch (language) {
    case Language::CLike: m_keywords = words(kCLike); break;
    case Language::Hash: m_keywords = words(kHash); break;
    case Language::Sql: m_keywords = words(kSql); break;
    default: m_keywords.clear(); break;
    }
    rehighlight();
}

void CodeSyntax::highlightBlock(const QString &text) {
    const bool inBlock = previousBlockState() == kInBlockComment;
    setCurrentBlockState(0);
    if (m_language == Language::Plain) return;
    if (text.size() > kMaxLineLength) {
        if (inBlock) setCurrentBlockState(kInBlockComment);
        return;
    }
    const bool cStyle = m_language == Language::CLike || m_language == Language::Json || m_language == Language::Sql;
    const bool markup = m_language == Language::Markup;
    const int n = text.size();
    int i = 0;
    auto isWord = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); };
    // Opens a comment at `start` that runs to `closer`, or to the end of the line (then the next line continues it).
    auto comment = [&](int start, int bodyStart, const QString &closer) {
        const int end = text.indexOf(closer, bodyStart);
        if (end < 0) { setFormat(start, n - start, m_comment); setCurrentBlockState(kInBlockComment); i = n; }
        else { setFormat(start, end + closer.size() - start, m_comment); i = end + closer.size(); }
    };
    const QString blockCloser = markup ? QStringLiteral("-->") : QStringLiteral("*/");
    if (inBlock) comment(0, 0, blockCloser);

    while (i < n) {
        const QChar c = text.at(i);
        if (markup) {
            if (text.mid(i, 4) == QLatin1String("<!--")) { comment(i, i + 4, blockCloser); continue; }
            if (c == QLatin1Char('<')) {
                int j = i + 1;
                while (j < n && (text.at(j) == QLatin1Char('/') || text.at(j) == QLatin1Char('!') || text.at(j) == QLatin1Char('?'))) ++j;
                const int nameStart = j;
                while (j < n && (isWord(text.at(j)) || text.at(j) == QLatin1Char('-') || text.at(j) == QLatin1Char(':'))) ++j;
                if (j > nameStart) setFormat(i, j - i, m_tag);
                i = j > i ? j : i + 1;
                continue;
            }
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'') || (c == QLatin1Char('`') && (m_language == Language::CLike || m_language == Language::Sql))) {
            int j = i + 1;
            while (j < n && text.at(j) != c) j += (text.at(j) == QLatin1Char('\\') && j + 1 < n) ? 2 : 1;
            if (j < n) { setFormat(i, j + 1 - i, m_string); i = j + 1; }
            else if (c == QLatin1Char('"')) { setFormat(i, n - i, m_string); i = n; }   // unterminated double quote: rest of the line
            else ++i;                                                                    // a lone apostrophe is not a string
            continue;
        }
        if (cStyle && c == QLatin1Char('/') && i + 1 < n) {
            if (text.at(i + 1) == QLatin1Char('/') && m_language != Language::Sql) { setFormat(i, n - i, m_comment); break; }
            if (text.at(i + 1) == QLatin1Char('*')) { comment(i, i + 2, blockCloser); continue; }
        }
        if (m_language == Language::Sql && c == QLatin1Char('-') && i + 1 < n && text.at(i + 1) == QLatin1Char('-')) { setFormat(i, n - i, m_comment); break; }
        if (m_language == Language::Hash && c == QLatin1Char('#') && (i == 0 || !isWord(text.at(i - 1)))) { setFormat(i, n - i, m_comment); break; }
        if (c.isDigit() && (i == 0 || !isWord(text.at(i - 1)))) {
            int j = i + 1;
            while (j < n && (text.at(j).isLetterOrNumber() || text.at(j) == QLatin1Char('.') || text.at(j) == QLatin1Char('_'))) ++j;
            setFormat(i, j - i, m_number);
            i = j;
            continue;
        }
        if (isWord(c)) {
            int j = i + 1;
            while (j < n && isWord(text.at(j))) ++j;
            const QString word = text.mid(i, j - i);
            if (m_keywords.contains(word) || (m_language == Language::Sql && m_keywords.contains(word.toLower()))) setFormat(i, j - i, m_keyword);
            else if (m_language == Language::Json && (word == QLatin1String("true") || word == QLatin1String("false") || word == QLatin1String("null"))) setFormat(i, j - i, m_keyword);
            i = j;
            continue;
        }
        ++i;
    }
}

void CodeHighlighter::setDocument(QQuickTextDocument *document) {
    if (document == m_document) return;
    delete m_syntax;
    m_syntax = nullptr;
    m_document = document;
    if (m_document && m_document->textDocument()) {
        m_syntax = new CodeSyntax(m_document->textDocument());
        m_syntax->setLanguage(CodeSyntax::languageForFile(m_fileName));
    }
    emit documentChanged();
}

void CodeHighlighter::setFileName(const QString &name) {
    if (name == m_fileName) return;
    m_fileName = name;
    if (m_syntax) m_syntax->setLanguage(CodeSyntax::languageForFile(m_fileName));
    emit fileNameChanged();
}
