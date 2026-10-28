#include "MarkdownHighlighter.h"

#include <QRegularExpression>

namespace {
constexpr int kInFence = 1;

QTextCharFormat colour(const char *rgb, bool bold = false, bool italic = false) {
    QTextCharFormat f;
    f.setForeground(QColor(QLatin1String(rgb)));
    if (bold) f.setFontWeight(QFont::Bold);
    if (italic) f.setFontItalic(true);
    return f;
}
}  // namespace

MarkdownSyntax::MarkdownSyntax(QTextDocument *document) : QSyntaxHighlighter(document) {
    m_heading = colour("#7CC4FF", true);
    m_code = colour("#F4A261");
    m_fence = colour("#F4A261");
    m_bold = colour("#FFFFFF", true);
    m_italic = colour("#DDE6F5", false, true);
    m_link = colour("#A0E8AF");
    m_quote = colour("#9AA8C0", false, true);
    m_marker = colour("#FCD34D", true);
    m_rule = colour("#6B7A99");
    m_code.setFontFamilies({ QStringLiteral("Consolas"), QStringLiteral("monospace") });
    m_fence.setFontFamilies({ QStringLiteral("Consolas"), QStringLiteral("monospace") });
}

void MarkdownSyntax::highlightBlock(const QString &text) {
    const bool wasInFence = previousBlockState() == kInFence;
    if (text.size() > kMaxLineLength) { setCurrentBlockState(wasInFence ? kInFence : 0); if (wasInFence) setFormat(0, kMaxLineLength, m_code); return; }

    static const QRegularExpression fence(QStringLiteral("^\\s{0,3}(```|~~~)"));
    if (fence.match(text).hasMatch()) {
        setFormat(0, text.size(), m_fence);
        setCurrentBlockState(wasInFence ? 0 : kInFence);
        return;
    }
    if (wasInFence) { setFormat(0, text.size(), m_code); setCurrentBlockState(kInFence); return; }
    setCurrentBlockState(0);

    static const QRegularExpression heading(QStringLiteral("^\\s{0,3}#{1,6}\\s.*$"));
    static const QRegularExpression rule(QStringLiteral("^\\s{0,3}([-*_])(\\s*\\1){2,}\\s*$"));
    static const QRegularExpression quote(QStringLiteral("^\\s{0,3}>.*$"));
    static const QRegularExpression listMarker(QStringLiteral("^\\s*([-*+]|\\d{1,9}[.)])\\s"));
    static const QRegularExpression inlineCode(QStringLiteral("`[^`\\n]+`"));
    static const QRegularExpression bold(QStringLiteral("(\\*\\*|__)(?=\\S)(.+?)(?<=\\S)\\1"));
    static const QRegularExpression italic(QStringLiteral("(?<![*_\\w])([*_])(?=\\S)([^*_\\n]+?)(?<=\\S)\\1(?![*_\\w])"));
    static const QRegularExpression link(QStringLiteral("!?\\[[^\\]\\n]*\\]\\([^)\\n]*\\)"));

    if (heading.match(text).hasMatch()) { setFormat(0, text.size(), m_heading); return; }
    if (rule.match(text).hasMatch()) { setFormat(0, text.size(), m_rule); return; }
    if (quote.match(text).hasMatch()) setFormat(0, text.size(), m_quote);
    if (const auto m = listMarker.match(text); m.hasMatch()) setFormat(m.capturedStart(), m.capturedLength(), m_marker);
    // Most prose blocks contain no Markdown delimiters. Avoid scanning every
    // block with four global regexes when their opening tokens are absent.
    const bool hasStar=text.contains(QLatin1Char('*'));
    const bool hasUnderscore=text.contains(QLatin1Char('_'));
    if ((hasStar && text.contains(QStringLiteral("**"))) ||
        (hasUnderscore && text.contains(QStringLiteral("__"))))
        for (auto it=bold.globalMatch(text);it.hasNext();) {const auto m=it.next();setFormat(m.capturedStart(),m.capturedLength(),m_bold);}
    if (hasStar || hasUnderscore)
        for (auto it=italic.globalMatch(text);it.hasNext();) {const auto m=it.next();setFormat(m.capturedStart(),m.capturedLength(),m_italic);}
    if (text.contains(QLatin1Char('[')) && text.contains(QStringLiteral("](")))
        for (auto it=link.globalMatch(text);it.hasNext();) {const auto m=it.next();setFormat(m.capturedStart(),m.capturedLength(),m_link);}
    if (text.contains(QLatin1Char('`')))
        for (auto it=inlineCode.globalMatch(text);it.hasNext();) {const auto m=it.next();setFormat(m.capturedStart(),m.capturedLength(),m_code);}
}

void MarkdownHighlighter::setDocument(QQuickTextDocument *document) {
    if (document == m_document) return;
    delete m_syntax;
    m_syntax = nullptr;
    m_document = document;
    if (m_document && m_document->textDocument()) m_syntax = new MarkdownSyntax(m_document->textDocument());
    emit documentChanged();
}
