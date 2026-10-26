#include <QtTest>
#include "core/DocumentDraft.h"
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
    // ---- MDok server drafts and the conflict diff (src/core/DocumentDraft.h) ----
    void draftIdIsAHashOfThePathOnly(){
        const auto a=fortiq::draftId("/home/u/notes.md"),b=fortiq::draftId("/home/u/other.md");
        QVERIFY(fortiq::validDraftId(a));QVERIFY(a!=b);QVERIFY(!a.contains("notes"));
        QVERIFY(!fortiq::validDraftId("../etc/passwd"));QVERIFY(!fortiq::validDraftId(QString(63,'a')));QVERIFY(!fortiq::validDraftId(QString(64,'G')));
        QVERIFY(fortiq::buildDraftWriteScript("../x","m","d","T").isEmpty());QVERIFY(fortiq::buildDraftReadScript("x; rm -rf ~").isEmpty());QVERIFY(fortiq::buildDraftDeleteScript("").isEmpty());
    }
    void draftMetaRoundTripAndValidation(){
        fortiq::DraftMeta m;m.path=QString::fromUtf8("/home/été/note.md");m.baseHashHex=QString(64,'a');m.savedAtSecs=1792970000;m.bom=true;m.crlf=true;
        fortiq::DraftMeta back;QVERIFY(fortiq::parseDraftMeta(fortiq::draftMetaText(m),back));
        QCOMPARE(back.path,m.path);QCOMPARE(back.baseHashHex,m.baseHashHex);QCOMPARE(back.savedAtSecs,m.savedAtSecs);QVERIFY(back.bom&&back.crlf);
        fortiq::DraftMeta bad;
        QVERIFY(!fortiq::parseDraftMeta("",bad));
        QVERIFY(!fortiq::parseDraftMeta("path="+QByteArray("relative/x.md").toBase64()+"\nbase="+QByteArray(64,'a')+"\n",bad));   // not absolute
        QVERIFY(!fortiq::parseDraftMeta("path="+QByteArray("/a.md").toBase64()+"\nbase=nothex\n",bad));
        QVERIFY(!fortiq::parseDraftMeta("path="+QByteArray("/a.md").toBase64()+"\nbase="+QByteArray(64,'A')+"\n",bad));        // upper case is not what we write
    }
    void draftScriptsCarryNoRawText(){
        const QString id=fortiq::draftId("/home/u/a.md");
        const QByteArray evil="$(touch /tmp/pwned) `id` '; rm -rf ~ ; FQDXM\nFQDXB\n";
        const QByteArray s=fortiq::buildDraftWriteScript(id,"path=x\n",evil,"X");
        QVERIFY(!s.isEmpty());
        QVERIFY2(!s.contains("touch /tmp/pwned")&&!s.contains("rm -rf ~"),"the document text must travel as base64 only");
        QVERIFY(s.contains("umask 077"));QVERIFY(s.contains("chmod 700"));QVERIFY(s.contains("mv -f"));
        // the document is published before its meta: a reader that sees the meta always sees a complete document
        QVERIFY(s.indexOf(id.toLatin1()+".md")<s.indexOf(id.toLatin1()+".meta"));
    }
    void draftReadParsesAndRefusesGarbage(){
        fortiq::DraftMeta m;m.path="/home/u/a.md";m.baseHashHex=QString(64,'b');m.savedAtSecs=5;
        const QByteArray out="#meta\n"+fortiq::draftMetaText(m)+"#doc\n"+QByteArray("hello\nworld\n").toBase64()+"\n#end\n";
        const auto r=fortiq::parseDraftRead(out);QVERIFY(r.found);QCOMPARE(r.documentBytes,QByteArray("hello\nworld\n"));QCOMPARE(r.meta.path,m.path);
        QVERIFY(!fortiq::parseDraftRead("#none\n").found);QVERIFY(!fortiq::parseDraftRead("#meta\ngarbage\n#doc\nAAAA\n#end\n").found);QVERIFY(!fortiq::parseDraftRead("").found);
        QVERIFY(!fortiq::parseDraftRead("#doc\nAAAA\n#meta\n#end").found);
    }
    void lineDiffShowsOnlyTheChanges(){
        const auto same=fortiq::lineDiff("a\nb\nc","a\nb\nc");QCOMPARE(same.onlyMine,0);QCOMPARE(same.onlyServer,0);
        const auto d=fortiq::lineDiff("one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten","one\ntwo\nthree\nFOUR\nfive\nsix\nseven\neight\nnine\nten");
        QCOMPARE(d.onlyMine,1);QCOMPARE(d.onlyServer,1);QVERIFY(d.text.contains("- four"));QVERIFY(d.text.contains("+ FOUR"));
        QVERIFY2(d.text.contains("…"),"far-away unchanged lines are folded");QVERIFY(!d.text.contains("one\n")||d.text.indexOf("one")>=0);
        const auto added=fortiq::lineDiff("a","a\nb");QCOMPARE(added.onlyServer,1);QCOMPARE(added.onlyMine,0);
        const auto empty=fortiq::lineDiff("","x");QCOMPARE(empty.onlyServer,1);
        QStringList many;for(int i=0;i<fortiq::kDraftDiffMaxLines+5;++i)many<<QString::number(i);
        const auto big=fortiq::lineDiff(many.join('\n'),"x");QVERIFY(big.tooLong);QVERIFY(!big.text.isEmpty());
    }
    void extensions(){QVERIFY(fortiq::markdownPath("/home/été espace.MD"));QVERIFY(fortiq::markdownPath("/a.markdown"));QVERIFY(!fortiq::markdownPath("/a.pdf"));QVERIFY(!fortiq::markdownPath("/a.md.exe"));}
};
QTEST_GUILESS_MAIN(MarkdownDataTest)
#include "MarkdownDataTest.moc"
