#include <QtTest>
#include "core/MarkdownData.h"
class MarkdownDataTest:public QObject {
    Q_OBJECT
private slots:
    void roundTrip(){
        const auto body=QString::fromUtf8("# Été\nПривет **monde**\n");
        for(bool bom:{false,true})for(bool crlf:{false,true}){
            const auto bytes=fortiq::encodeMarkdown(body,bom,crlf);const auto decoded=fortiq::decodeMarkdown(bytes);
            QVERIFY(decoded.error.isEmpty());QCOMPARE(decoded.text,body);QCOMPARE(decoded.bom,bom);QCOMPARE(decoded.crlf,crlf);
            QCOMPARE(fortiq::encodeMarkdown(decoded.text,decoded.bom,decoded.crlf),bytes);
        }
    }
    void rejectsInvalid(){
        for(const QByteArray &bytes:QList<QByteArray>{QByteArray("a\0b",3),QByteArray::fromHex("ff"),QByteArray::fromHex("e282"),QByteArray("a\rb"),QByteArray("a\r\nb\n"),QByteArray(262145,'x')})
            QVERIFY2(!fortiq::decodeMarkdown(bytes).error.isEmpty(),bytes.toHex().left(30));
        QVERIFY(!fortiq::decodeMarkdown("valid",true).error.isEmpty());
        QVERIFY(fortiq::decodeMarkdown({}).error.isEmpty());
        QVERIFY(fortiq::decodeMarkdown(QByteArray(262144,'x')).error.isEmpty());
    }
    void extensions(){QVERIFY(fortiq::markdownPath("/home/été espace.MD"));QVERIFY(fortiq::markdownPath("/a.markdown"));QVERIFY(!fortiq::markdownPath("/a.pdf"));QVERIFY(!fortiq::markdownPath("/a.md.exe"));}
};
QTEST_GUILESS_MAIN(MarkdownDataTest)
#include "MarkdownDataTest.moc"
