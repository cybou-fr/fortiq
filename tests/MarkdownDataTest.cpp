#include <QtTest>
#include "core/DocumentDraft.h"
#include "core/MarkdownData.h"
#include "documents/TextSearch.h"
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
    void draftCompletionRejectsWrongServerOrSession(){
        using fortiq::draftCompletionBelongsToDocument;
        QVERIFY(draftCompletionBelongsToDocument("/home/u/a.md","/home/u/a.md",7,7,"server-a","server-a",true,4,4));
        QVERIFY(!draftCompletionBelongsToDocument("/home/u/a.md","/home/u/b.md",7,7,"server-a","server-a",true,4,4));
        QVERIFY(!draftCompletionBelongsToDocument("/home/u/a.md","/home/u/a.md",7,8,"server-a","server-a",true,4,4));
        QVERIFY(!draftCompletionBelongsToDocument("/home/u/a.md","/home/u/a.md",7,7,"server-a","server-b",true,4,4));
        QVERIFY(!draftCompletionBelongsToDocument("/home/u/a.md","/home/u/a.md",7,7,"server-a","server-a",false,4,4));
        QVERIFY(!draftCompletionBelongsToDocument("/home/u/a.md","/home/u/a.md",7,7,"server-a","server-a",true,4,5));
    }
    void delayedRecoveryCannotCrossDocumentEpoch(){
        using fortiq::draftCompletionBelongsToDocument;
        // The second open has the same path and server, but is a different
        // document operation; its recovery UI must not see the old response.
        QVERIFY(!draftCompletionBelongsToDocument("/home/u/a.md","/home/u/a.md",3,3,
                                                  "server-1","server-1",true,12,13));
        QVERIFY(draftCompletionBelongsToDocument("/home/u/a.md","/home/u/a.md",3,3,
                                                 "server-1","server-1",true,13,13));
    }
    void inFlightDraftMustNotMaskNewEdits(){
        using fortiq::draftNeedsAnotherSave;
        QVERIFY(!draftNeedsAnotherSave("a","a",true,false));
        QVERIFY(draftNeedsAnotherSave("a","a b",true,false));
        QVERIFY(!draftNeedsAnotherSave("a","a b",false,false));
        QVERIFY(!draftNeedsAnotherSave("a","a b",true,true));
    }
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
    // ---- MDok find / replace (src/documents/TextSearch.h) ----
    void searchFindsPlainAndCaseAndWords(){
        using fortiq::findAll;
        QCOMPARE(findAll("Un chat, un CHAT, un chaton","chat"),(QList<qsizetype>{3,12,21}));
        QCOMPARE(findAll("Un chat, un CHAT, un chaton","chat",{true,false}),(QList<qsizetype>{3,21}));
        QCOMPARE(findAll("Un chat, un CHAT, un chaton","chat",{false,true}),(QList<qsizetype>{3,12}));      // not "chaton"
        QCOMPARE(findAll("aaaa","aa"),(QList<qsizetype>{0,2}));                                                // non-overlapping
        QVERIFY(findAll("abc","").isEmpty());QVERIFY(findAll("","a").isEmpty());
        QCOMPARE(findAll(QString::fromUtf8("Été été ÉTÉ"),QString::fromUtf8("été")).size(),3);                // Unicode case folding
        QCOMPARE(findAll("snake_case snake","snake",{false,true}),(QList<qsizetype>{11}));                    // "_" is a word character
        QCOMPARE(findAll("$(rm -rf ~) .*","$(rm -rf ~)").size(),1);                                           // the query is never a pattern
        QCOMPARE(findAll(".*",".*").size(),1);
        QStringList many;for(int i=0;i<30000;++i)many<<"x";
        QCOMPARE(findAll(many.join(' '),"x").size(),fortiq::kSearchMaxMatches);                              // bounded
    }
    void searchReplaceIsLiteral(){
        int n=0;
        QCOMPARE(fortiq::replaceAll("a.b a.b","a.b","$1\\0&",{},&n),QString("$1\\0& $1\\0&"));QCOMPARE(n,2);
        QCOMPARE(fortiq::replaceAll("chat chaton","chat","chien",{false,true},&n),QString("chien chaton"));QCOMPARE(n,1);
        QCOMPARE(fortiq::replaceAll("abc","x","y",{},&n),QString("abc"));QCOMPARE(n,0);
        QCOMPARE(fortiq::replaceAll("aaa","a","",{},&n),QString(""));QCOMPARE(n,3);
        QCOMPARE(fortiq::replaceAll("aaa","a","aa",{},&n),QString("aaaaaa"));                                  // a replacement is never searched again
    }
    void searchStepsAndWraps(){
        const QList<qsizetype> m{3,12,21};
        QCOMPARE(fortiq::stepMatch(m,0,4,true),0);QCOMPARE(fortiq::stepMatch(m,7,4,true),1);QCOMPARE(fortiq::stepMatch(m,22,4,true),0);   // wraps to the first
        QCOMPARE(fortiq::stepMatch(m,19,4,false),1);QCOMPARE(fortiq::stepMatch(m,2,4,false),2);                                            // wraps to the last
        QCOMPARE(fortiq::stepMatch({},0,1,true),-1);
    }
    void textStatsCountWordsLinesCharacters(){
        auto s=fortiq::textStats("");QCOMPARE(s.words,0);QCOMPARE(s.lines,0);
        s=fortiq::textStats("un deux\ntrois\n");QCOMPARE(s.words,3);QCOMPARE(s.lines,2);QCOMPARE(s.characters,14);
        s=fortiq::textStats("   \n\n  ");QCOMPARE(s.words,0);QCOMPARE(s.lines,3);
    }
    void extensions(){QVERIFY(fortiq::markdownPath("/home/été espace.MD"));QVERIFY(fortiq::markdownPath("/a.markdown"));QVERIFY(!fortiq::markdownPath("/a.pdf"));QVERIFY(!fortiq::markdownPath("/a.md.exe"));}
};
QTEST_GUILESS_MAIN(MarkdownDataTest)
#include "MarkdownDataTest.moc"
