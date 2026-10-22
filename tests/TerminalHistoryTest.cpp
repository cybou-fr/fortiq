#include <QtTest>
#include "core/TerminalHistory.h"
class TerminalHistoryTest:public QObject {
    Q_OBJECT
private slots:
    void timestampsUnicodeAndUntrustedText() {
        const auto rows=fortiq::terminalHistoryRows(QString::fromUtf8("#1792670000\npwd\n#1792670001\nprintf 'été Привет'\n<img src=remote>\n").toUtf8());
        QCOMPARE(rows.size(),3);
        QCOMPARE(rows[0].toMap().value("text").toString(),QString("<img src=remote>"));
        QCOMPARE(rows[1].toMap().value("text").toString(),QString::fromUtf8("printf 'été Привет'"));
        QCOMPARE(rows[2].toMap().value("text").toString(),QString("pwd"));
    }
    void limitsAndControlCharacters() {
        QByteArray data;for(int i=0;i<250;++i)data+=QByteArray::number(i)+'\n';
        auto rows=fortiq::terminalHistoryRows(data);QCOMPARE(rows.size(),200);
        QCOMPARE(rows.first().toMap().value("text").toString(),QString("249"));
        QCOMPARE(rows.last().toMap().value("text").toString(),QString("50"));
        rows=fortiq::terminalHistoryRows(QByteArray(5000,'x')+'\n'+QByteArray("hello\r\0\033\177\n",10));
        QCOMPARE(rows.last().toMap().value("text").toString().size(),4096);
        QCOMPARE(rows.first().toMap().value("text").toString(),QString("hello"));
    }
};
QTEST_GUILESS_MAIN(TerminalHistoryTest)
#include "TerminalHistoryTest.moc"
