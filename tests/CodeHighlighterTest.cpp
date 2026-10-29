// Éditeur syntax colouring (src/documents/CodeHighlighter.*): it only sets character formats; it must stay inert on hostile text.
#include "documents/CodeHighlighter.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTextBlock>
#include <QTextLayout>
#include <QtTest>

class CodeHighlighterTest : public QObject {
    Q_OBJECT
    static QColor colourAt(const QTextBlock &block, int pos) {
        for (const QTextLayout::FormatRange &r : block.layout()->formats())
            if (pos >= r.start && pos < r.start + r.length && r.format.foreground().style() != Qt::NoBrush) return r.format.foreground().color();
        return QColor();
    }
private slots:
    void languageFollowsTheFileName() {
        QCOMPARE(CodeSyntax::languageForFile("/srv/app/main.cpp"), CodeSyntax::Language::CLike);
        QCOMPARE(CodeSyntax::languageForFile("/etc/nginx/site.conf"), CodeSyntax::Language::Hash);
        QCOMPARE(CodeSyntax::languageForFile("/x/Makefile"), CodeSyntax::Language::Hash);
        QCOMPARE(CodeSyntax::languageForFile("/x/data.json"), CodeSyntax::Language::Json);
        QCOMPARE(CodeSyntax::languageForFile("/x/index.html"), CodeSyntax::Language::Markup);
        QCOMPARE(CodeSyntax::languageForFile("/x/q.SQL"), CodeSyntax::Language::Sql);
        QCOMPARE(CodeSyntax::languageForFile("/x/notes.txt"), CodeSyntax::Language::Plain);
    }
    void colorsCLikeCode() {
        QTextDocument doc;
        CodeSyntax syntax(&doc);
        syntax.setLanguage(CodeSyntax::Language::CLike);
        doc.setPlainText("int x = 42; // note\nconst char *s = \"a // b\";\n/* open\nstill */ return x;");
        syntax.rehighlight();
        const QTextBlock b0 = doc.findBlockByNumber(0), b1 = doc.findBlockByNumber(1), b2 = doc.findBlockByNumber(2), b3 = doc.findBlockByNumber(3);
        QVERIFY(colourAt(b0, 1).isValid());                 // int: keyword
        QVERIFY(!colourAt(b0, 4).isValid());                // x: plain
        QVERIFY(colourAt(b0, 8).isValid());                 // 42: number
        QVERIFY(colourAt(b0, 15).isValid());                // // comment
        QCOMPARE(colourAt(b1, 17), colourAt(b1, 21));       // the // inside the string stays string colour
        QVERIFY(colourAt(b2, 4).isValid());                 // block comment continues over lines
        QVERIFY(colourAt(b3, 2).isValid());
        QVERIFY(!colourAt(b3, 16).isValid());               // after */ the code is plain again (x)
        QVERIFY(colourAt(b3, 9).isValid());                 // return: keyword
    }
    void colorsHashAndMarkup() {
        QTextDocument shell;
        CodeSyntax s1(&shell);
        s1.setLanguage(CodeSyntax::Language::Hash);
        shell.setPlainText("echo \"hi\" # tail\n#!/bin/sh");
        s1.rehighlight();
        QVERIFY(colourAt(shell.findBlockByNumber(0), 12).isValid());
        QVERIFY(colourAt(shell.findBlockByNumber(1), 0).isValid());
        QTextDocument html;
        CodeSyntax s2(&html);
        s2.setLanguage(CodeSyntax::Language::Markup);
        html.setPlainText("<div class=\"a\">x</div>\n<!-- c\nd -->");
        s2.rehighlight();
        QVERIFY(colourAt(html.findBlockByNumber(0), 1).isValid());          // tag name
        QVERIFY(colourAt(html.findBlockByNumber(2), 1).isValid());          // comment continues
    }
    void neverChangesTheTextAndStaysFastOnHostileInput() {
        QTextDocument doc;
        CodeSyntax syntax(&doc);
        syntax.setLanguage(CodeSyntax::Language::CLike);
        QString hostile;
        for (int i = 0; i < 200; ++i) hostile += QString(8000, QLatin1Char('"')) + QLatin1Char('\n');
        hostile += QString(100000, QLatin1Char('/')) + QStringLiteral("*\n") + QString(50000, QLatin1Char('\\'));
        doc.setPlainText(hostile);
        QElapsedTimer timer;
        timer.start();
        syntax.rehighlight();
        QVERIFY2(timer.elapsed() < 3000, "highlighting must not blow up on long lines");
        QCOMPARE(doc.toPlainText(), hostile);
    }
};

QTEST_MAIN(CodeHighlighterTest)
#include "CodeHighlighterTest.moc"
