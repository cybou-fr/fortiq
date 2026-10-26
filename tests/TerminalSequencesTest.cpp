// S-10 (docs/STABILIZATION_PLAN.md): hostile terminal output and hostile paste. No server needed: the stream a
// remote program can write is fed straight into the terminal widget (libvterm).
#include "terminal/TerminalView.h"

#include <QClipboard>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QtTest>

class TerminalSequencesTest : public QObject {
    Q_OBJECT
    static QByteArray collected(const QSignalSpy &spy) {
        QByteArray all;
        for (const QList<QVariant> &args : spy) all += args.at(0).toByteArray();
        return all;
    }
    static TerminalView *make() {
        auto *view = new TerminalView;
        view->setProperty("inputEnabled", true);
        view->setSize(QSizeF(900, 500));
        return view;
    }
private slots:
    // What the terminal sends BACK to the server (device reports, answers to queries) must never contain text taken
    // from the stream (title, query strings) and never a line break: a report that "types" a command is the classic
    // way to turn a hostile file or log into command execution.
    void repliesNeverCarryStreamTextOrNewlines() {
        std::unique_ptr<TerminalView> view(make());
        QSignalSpy spy(view.get(), &TerminalView::bytes);
        const QList<QByteArray> hostile = {
            "\x1b]0;INJECTED-TITLE rm -rf ~\x07", "\x1b]2;INJECTED-TITLE\x1b\\", "\x1b[21t", "\x1b[20t", "\x1b[22;0t", "\x1b[23;0t",
            "\x1bP$qINJECTED\x1b\\", "\x1bP+q494e4a\x1b\\", "\x1bP1$rINJECTED\x1b\\", "\x05", "\x1bZ", "\x1b[c", "\x1b[>c", "\x1b[=c",
            "\x1b[5n", "\x1b[6n", "\x1b[?6n", "\x1b[?15n", "\x1b[x", "\x1b[?996n", "\x1b]52;c;aW5qZWN0ZWQ=\x07", "\x1b]52;c;?\x07",
            "\x1b]4;1;?\x07", "\x1b]10;?\x07", "\x1b]11;?\x07", "\x1b]8;;http://INJECTED\x07text\x1b]8;;\x07", "\x1b]1337;File=INJECTED\x07",
            "\x1b[?1000h\x1b[?1006h\x1b[<0;5;5M", "\x1b[?2004h", "\x1b[?1049h\x1b[?1049l", "\x1b[8;99999;99999t", "\x1b[?25l\x1b[?25h",
            "\x1b#8", "\x1b" "c" };
        for (const QByteArray &seq : hostile) view->feed(seq);
        const QByteArray out = collected(spy);
        QVERIFY2(!out.contains("INJECTED") && !out.contains("rm -rf") && !out.contains("injected"), out.toHex());
        QVERIFY2(!out.contains('\n') && !out.contains('\r'), out.toHex());
        static const QRegularExpression onlySequences(QStringLiteral(
            "^(\\x1b\\[[0-9;?>=$ <]*[A-Za-z~]|\\x1bP[^\\x1b]*\\x1b\\\\|\\x1b\\][^\\x07\\x1b]*(\\x07|\\x1b\\\\))*$"));
        QVERIFY2(onlySequences.match(QString::fromLatin1(out)).hasMatch(), out.toHex());
    }

    // OSC 52 (write to / read from the clipboard) is not implemented: the stream cannot touch the local clipboard.
    void streamCannotWriteOrReadTheClipboard() {
        std::unique_ptr<TerminalView> view(make());
        const QString before = QGuiApplication::clipboard()->text();   // the real clipboard of this machine: put it back afterwards
        QGuiApplication::clipboard()->setText(QStringLiteral("SENTINEL"));
        QSignalSpy spy(view.get(), &TerminalView::bytes);
        view->feed("\x1b]52;c;SU5KRUNURUQ=\x07\x1b]52;p;SU5KRUNURUQ=\x1b\\\x1b]52;c;?\x07");
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("SENTINEL"));
        QVERIFY2(!collected(spy).contains("SENTINEL"), "the clipboard must not be sent to the server");
        QGuiApplication::clipboard()->setText(before);
    }

    // A pasted text may hold the end-of-paste marker; it must not be able to leave bracketed-paste mode.
    void pasteCannotLeaveBracketedPaste() {
        std::unique_ptr<TerminalView> view(make());
        view->feed("\x1b[?2004h");
        QSignalSpy spy(view.get(), &TerminalView::bytes);
        view->paste(QStringLiteral("safe\x1b[201~echo pwned\x1b[200~x\x03y\n"));
        const QByteArray out = collected(spy);
        QVERIFY2(out.startsWith("\x1b[200~") && out.endsWith("\x1b[201~"), out.toHex());
        const QByteArray body = out.mid(6, out.size() - 12);
        QVERIFY2(!body.contains('\x1b') && !body.contains('\x03'), body.toHex());
        QVERIFY2(body.startsWith("safe") && body.contains("echo pwned"), body.toHex());   // the text itself is kept
        // without bracketed paste, no escape or control byte gets through either (tab and line breaks stay)
        view->feed("\x1b[?2004l");
        spy.clear();
        view->paste(QStringLiteral("a\x1b[31mb\tc\x7f\u0085\n"));
        const QByteArray plain = collected(spy);
        QVERIFY2(!plain.contains('\x1b') && !plain.contains('\x7f') && !plain.contains("\xc2\x85"), plain.toHex());
        QVERIFY(plain.contains('\t'));
    }

    // Memory and time stay bounded whatever the stream says.
    void hostileStream_data() {
        QTest::addColumn<QByteArray>("stream");
        for (const char *final : { "C", "D", "@", "P", "L", "M", "X", "b", "S", "T", "A", "B", "G", "d", "H", "E", "F", "I", "Z", "J", "K" })
            QTest::newRow(qPrintable(QStringLiteral("csi-%1").arg(QLatin1String(final))))
                << (QByteArray("\x1b[") + "99999999999999999999" + final + "\x1b[2147483647" + final + "\x1b[-1" + final
                    + "\x1b[1;99999999999999999999" + final + "\x1b[0" + final + "\x1b[" + final);
        QTest::newRow("sgr-100000-params") << ("\x1b[" + QByteArray("1;").repeated(100000) + "m");
        QTest::newRow("osc-title-1M") << ("\x1b]0;" + QByteArray(1 << 20, 'A') + "\x07");
        QTest::newRow("dcs-1M") << ("\x1bP" + QByteArray(1 << 20, 'B') + "\x1b\\");
        QTest::newRow("osc52-1M") << ("\x1b]52;c;" + QByteArray(1 << 20, 'C') + "\x07");
        QTest::newRow("invalid-utf8") << QByteArray("\xff\xfe\xc0\x80\xed\xa0\x80", 7).repeated(50000);
        QTest::newRow("combining-200000") << QByteArray("e\xcc\x81", 3).repeated(200000);
        QTest::newRow("altscreen-flood") << ("\x1b[?1049h" + QByteArray("x\r\n").repeated(5000) + "\x1b[?1049l");
        QTest::newRow("wide-chars") << QByteArray("\xe4\xb8\x96", 3).repeated(100000);
        QTest::newRow("resize-requests") << QByteArray("\x1b[8;99999;99999t\x1b[4;99999;99999t\x1b[?3h\x1b[?3l\x1b[?40h");
        QTest::newRow("scroll-regions") << QByteArray("\x1b[0;0r\x1b[99999;1r\x1b[5;2r\x1b[?69h\x1b[1;99999s\x1b[99999;1;1;99999$z\x1b[1;1;99999;99999$x");
        QTest::newRow("tabs-and-charsets") << QByteArray("\x1b[99999I\x1b[99999Z\x1bH\x1b[3g\x1b(0\x1b)B\x1b*0\x1b+A\x0e\x0f\x1bn\x1bo").repeated(2000);
        QTest::newRow("c1-controls") << QByteArray("\x84\x85\x88\x8d\x8e\x8f\x90\x9b\x9d\x9e\x9f", 11).repeated(1000);
        QTest::newRow("random-bytes") << [] {
            QByteArray random(500000, 0);
            quint32 state = 12345;
            for (char &c : random) { state = state * 1664525u + 1013904223u; c = char(state >> 24); }
            return random;
        }();
    }
    void hostileStream() {
        QFETCH(QByteArray, stream);
        std::unique_ptr<TerminalView> view(make());
        const int columns = view->columns(), rows = view->rows();
        QElapsedTimer clock;
        clock.start();
        view->feed(stream);
        QVERIFY2(clock.elapsed() < 20000, qPrintable(QString::number(clock.elapsed())));
        QCOMPARE(view->columns(), columns);
        QCOMPARE(view->rows(), rows);
        // the widget is still usable afterwards
        view->feed("\x1b" "c");
        view->feed("ok\r\n");
        QVERIFY(view->screenText().contains(QStringLiteral("ok")));
        view->findText(QStringLiteral("ok"), false);
        view->clearSelection();
    }

    // Performance baseline finding: libvterm's screen layer costs about 10 us per scrolled line (a flood of short lines froze the window for
    // hundreds of milliseconds per read). feedLater() interprets the same bytes in ~8 ms slices between event-loop turns.
    void feedLaterGivesTheSameScreenAndKeepsTheLoopAlive() {
        QByteArray flood;
        for (int i = 0; i < 60000; ++i) flood += "y" + QByteArray::number(i % 7) + "\r\n";
        flood += "\x1b[31mred\x1b[0m end\r\n";
        std::unique_ptr<TerminalView> direct(make()), sliced(make());
        direct->feed(flood);
        QSignalSpy backlog(sliced.get(), &TerminalView::backlogChanged);
        sliced->feedLater(flood.left(100000));
        sliced->feedLater(flood.mid(100000));                                    // two writes: the order must be kept
        QVERIFY(sliced->backlog() > 0);                                          // nothing interpreted yet: the caller returned at once
        qint64 worstGapMs = 0;
        QElapsedTimer sinceTick;
        sinceTick.start();
        QTimer ticker;
        ticker.setInterval(1);
        connect(&ticker, &QTimer::timeout, this, [&] { worstGapMs = qMax(worstGapMs, sinceTick.elapsed()); sinceTick.restart(); });
        ticker.start();
        QTRY_VERIFY_WITH_TIMEOUT(sliced->backlog() == 0, 30000);
        ticker.stop();
        QCOMPARE(sliced->screenText(), direct->screenText());
        QCOMPARE(sliced->scrollbackLines(), direct->scrollbackLines());
        QVERIFY2(worstGapMs < 150, qPrintable(QStringLiteral("the event loop was blocked for %1 ms").arg(worstGapMs)));
        QVERIFY(backlog.size() >= 2);
    }
    void resetDropsTheBacklog() {
        std::unique_ptr<TerminalView> view(make());
        view->feedLater(QByteArray(2000000, 'z'));
        QVERIFY(view->backlog() > 0);
        view->reset();
        QCOMPARE(view->backlog(), 0);
        QTest::qWait(50);                                                       // a slice scheduled before the reset must not resurrect old output
        QVERIFY(!view->screenText().contains(QLatin1Char('z')));
        view->feedLater("after\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(view->screenText().contains(QStringLiteral("after")), 5000);
    }

    void scrollbackStaysBounded() {
        std::unique_ptr<TerminalView> view(make());
        QElapsedTimer clock;
        clock.start();
        // 300000 lines of output: the scrollback keeps the last 1000 lines (TerminalView::ScrollbackLimit) and no more
        QByteArray chunk;
        for (int i = 0; i < 1000; ++i) chunk += "line " + QByteArray::number(i) + " " + QByteArray(150, 'x') + "\r\n";
        for (int i = 0; i < 300; ++i) view->feed(chunk);
        QVERIFY2(clock.elapsed() < 30000, qPrintable(QString::number(clock.elapsed())));
        QVERIFY2(view->scrollbackLines() <= 1000, qPrintable(QString::number(view->scrollbackLines())));
        QVERIFY(view->scrollbackLines() > 0);
        // still alive and sensible afterwards
        view->feed("\x1b" "c");
        view->feed("ok\r\n");
        QVERIFY(view->screenText().contains(QStringLiteral("ok")));
    }
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    TerminalSequencesTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "TerminalSequencesTest.moc"
