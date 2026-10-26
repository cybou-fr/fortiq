// MDok syntax colouring (src/documents/MarkdownHighlighter.*): it only sets character formats; it must stay fast and inert on hostile text.
#include "documents/MarkdownHighlighter.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTextBlock>
#include <QTextLayout>
#include <QtTest>

class MarkdownHighlighterTest : public QObject {
    Q_OBJECT
    // colour of the character at `pos` of a block, from the formats the highlighter attached
    static QColor colourAt(const QTextBlock &block, int pos) {
        for (const QTextLayout::FormatRange &r : block.layout()->formats())
            if (pos >= r.start && pos < r.start + r.length && r.format.foreground().style() != Qt::NoBrush) return r.format.foreground().color();
        return QColor();
    }
    static QTextBlock block(QTextDocument &doc, int n) { return doc.findBlockByNumber(n); }
private slots:
    void colorsTheMarkdownConstructs() {
        QTextDocument doc;
        MarkdownSyntax syntax(&doc);
        doc.setPlainText("# Title\nplain **bold** and *it* and `code` and [a](http://x)\n> quote\n- item\n```\n# not a heading\n```\nafter");
        syntax.rehighlight();
        QVERIFY(colourAt(block(doc, 0), 2).isValid());                              // heading
        const QTextBlock b1 = block(doc, 1);
        QVERIFY(!colourAt(b1, 1).isValid());                                         // plain text keeps the default colour
        QVERIFY(colourAt(b1, 8).isValid());                                          // **bold**
        QVERIFY(colourAt(b1, 22).isValid());                                         // *it*
        QVERIFY(colourAt(b1, 31).isValid());                                         // `code`
        QVERIFY(colourAt(b1, 45).isValid());                                         // [a](http://x)
        QVERIFY(colourAt(block(doc, 2), 3).isValid());                               // quote
        QVERIFY(colourAt(block(doc, 3), 0).isValid());                               // list marker
        QCOMPARE(colourAt(block(doc, 5), 2), colourAt(block(doc, 4), 1));            // inside a fence: code colour, NOT the heading colour
        QVERIFY(colourAt(block(doc, 5), 2) != colourAt(block(doc, 0), 2));
        QVERIFY(!colourAt(block(doc, 7), 1).isValid());                              // the fence closed: back to plain
    }
    void neverChangesTheText() {
        QTextDocument doc;
        MarkdownSyntax syntax(&doc);
        const QString text = QStringLiteral("# a\n**b** `c` [d](e)\n```\nx\n```\n");
        doc.setPlainText(text);
        syntax.rehighlight();
        QCOMPARE(doc.toPlainText(), text);
    }
    void hostileTextStaysFastAndInert() {
        QTextDocument doc;
        MarkdownSyntax syntax(&doc);
        QString evil;
        evil += QString(200000, QLatin1Char('*')) + QLatin1Char('\n');                       // a very long line of emphasis marks
        evil += QString(3000, QLatin1Char('[')) + QString(3000, QLatin1Char('(')) + QLatin1Char('\n');
        evil += QStringLiteral("**a ").repeated(900) + QLatin1Char('\n');                    // many unclosed openers on one (allowed) line
        evil += QStringLiteral("`").repeated(3500) + QLatin1Char('\n');
        for (int i = 0; i < 2000; ++i) evil += QStringLiteral("- *x* **y** `z` [l](u)\n");
        QElapsedTimer t;
        t.start();
        doc.setPlainText(evil);
        syntax.rehighlight();
        QVERIFY2(t.elapsed() < 5000, qPrintable(QString::number(t.elapsed()) + " ms"));
        QCOMPARE(doc.toPlainText(), evil);
    }
    void anUnclosedFenceStaysCode() {
        QTextDocument doc;
        MarkdownSyntax syntax(&doc);
        doc.setPlainText("```\nopen\n# still code\n");
        syntax.rehighlight();
        QVERIFY(colourAt(block(doc, 2), 1).isValid());
        QCOMPARE(colourAt(block(doc, 2), 1), colourAt(block(doc, 1), 1));
    }
};
QTEST_MAIN(MarkdownHighlighterTest)
#include "MarkdownHighlighterTest.moc"
