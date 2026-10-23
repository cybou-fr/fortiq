#include <QtTest>
#include "browser/CurlFetch.h"
#include "browser/HtmlSanitizer.h"
#include "browser/UrlPolicy.h"
#include "browser/UrlTools.h"

#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

namespace fortiq {
class BrowserTest : public QObject {
    Q_OBJECT
    static QString clean(const QString &in, const QString &base = "https://e.com/a/b/", bool reader = false) {
        return sanitizeHtml(in, base, reader).html;
    }
    // Every kept tag must be closed: a crude balance check on the sanitized output.
    static bool balanced(const QString &html) {
        QList<QString> stack;
        static const QRegularExpression re(QStringLiteral("<(/?)([a-z0-9]+)[^>]*>"));
        auto it = re.globalMatch(html);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString name = m.captured(2);
            if (name == "br" || name == "hr") continue;
            if (m.captured(1).isEmpty()) stack.append(name);
            else { if (stack.isEmpty() || stack.last() != name) return false; stack.removeLast(); }
        }
        return stack.isEmpty();
    }
    static QByteArray frame(int rc, const QString &meta, const QByteArray &headers, const QByteArray &err, const QByteArray &body) {
        QByteArray o = "FORTIQ-RC " + QByteArray::number(rc) + "\n" + meta.toUtf8() + "\n";
        o += "FORTIQ-HEADERS " + QByteArray::number(headers.size()) + "\n" + headers + "\n";
        o += "FORTIQ-ERRTEXT " + QByteArray::number(err.size()) + "\n" + err + "\n";
        o += "FORTIQ-BODY " + QByteArray::number(body.size()) + "\n" + body;
        return o;
    }
private slots:
    // ---------------- addresses ----------------
    void normalizesTypedAddresses() {
        QCOMPARE(normalizeUrl("example.com").url, QString("https://example.com/"));
        QCOMPARE(normalizeUrl("  Example.COM/Path?q=1&b=2  ").url, QString("https://example.com/Path?q=1&b=2"));
        QCOMPARE(normalizeUrl("http://example.com").url, QString("http://example.com/"));
        QCOMPARE(normalizeUrl("example.com:8080/x").url, QString("https://example.com:8080/x"));
        QCOMPARE(normalizeUrl("//example.com/x").url, QString("https://example.com/x"));
        QCOMPARE(normalizeUrl("https://e.com/a#frag").url, QString("https://e.com/a"));
        QCOMPARE(normalizeUrl("http://[::1]:8080/").url, QString("http://[::1]:8080/"));
        QVERIFY(normalizeUrl("https://xn--bcher-kva.example/").ok);
    }
    void refusesDangerousOrInvalidAddresses_data() {
        QTest::addColumn<QString>("input");
        for (const QString &s : { QString(""), QString("   "), QString("file:///etc/passwd"), QString("ftp://example.com/x"),
                                  QString("javascript:alert(1)"), QString("data:text/html,<script>1</script>"), QString("gopher://x/"),
                                  QString("scp://u@h/x"), QString("telnet://h"), QString("https://user:pw@example.com/"),
                                  QString("https://"), QString("http:///x"), QString("a b"), QString("https://e.com/\nfoo"),
                                  QString("https://e.com/\x01"), QString("https://e.com/") + QString(3000, 'a'), QString("http:example.com") })
            QTest::newRow(qPrintable(s.left(30).replace('\n', "\\n"))) << s;
    }
    void refusesDangerousOrInvalidAddresses() {
        QFETCH(QString, input);
        const UrlCheck c = normalizeUrl(input);
        QVERIFY2(!c.ok, qPrintable(c.url));
        QVERIFY(!c.error.isEmpty());
    }
    void resolvesLinksSafely() {
        const QString base = "https://e.com/a/b/c";
        QCOMPARE(resolveLink(base, "../x"), QString("https://e.com/a/x"));
        QCOMPARE(resolveLink(base, "/root?x=1"), QString("https://e.com/root?x=1"));
        QCOMPARE(resolveLink(base, "//cdn.e.com/y"), QString("https://cdn.e.com/y"));
        QCOMPARE(resolveLink(base, "  other.html "), QString("https://e.com/a/b/other.html"));
        QCOMPARE(resolveLink(base, "HTTP://Other.COM/z"), QString("http://other.com/z"));
        for (const QString &bad : { QString("#top"), QString(""), QString("javascript:alert(1)"), QString("JaVaScRiPt:alert(1)"),
                                    QString("mailto:a@b.c"), QString("data:text/html;base64,AAAA"), QString("file:///etc/passwd"),
                                    QString("ftp://x/y"), QString("tel:+33123456789"), QString("vbscript:x"), QString("https://u:p@h/") })
            QVERIFY2(resolveLink(base, bad).isEmpty(), qPrintable(bad));
    }

    // ---------------- script and framing ----------------
    void scriptRestrictsProtocolsAndQuotesTheUrl() {
        const QByteArray s = buildCurlScript(QStringLiteral("https://e.com/?q='; rm -rf /; echo '"));
        QVERIFY(s.startsWith("u='https://e.com/?q='\\''; rm -rf /; echo '\\'''\n"));
        QVERIFY(s.contains("--proto '=http,https'"));
        QVERIFY(s.contains("--max-filesize 5242880"));
        QVERIFY(!s.contains("--location") && !s.contains("--max-redirs"));   // redirects are followed hop by hop by the client, under the address policy
        QVERIFY(s.contains("--max-time 30"));
        QVERIFY(s.contains("-- \"$u\""));            // the URL is data after "--", never an option
        QVERIFY(s.contains("trap 'rm -rf \"$d\"' EXIT"));
        QVERIFY(!s.contains("--insecure"));          // certificates are always verified
        QVERIFY(!s.contains(" -k "));
    }
    void parsesAFullResponse() {
        const QByteArray headers = "HTTP/1.1 301 Moved Permanently\r\nLocation: https://e.com/\r\n\r\n"
                                   "HTTP/2 200 \r\ncontent-type: text/html; charset=UTF-8\r\nx-folded: a\r\n b\r\n\r\n";
        const QString meta = "FORTIQ-META\t200\thttps://e.com/\ttext/html; charset=UTF-8\t5\t1\t203.0.113.7\t2\t0\t0.011\t0.022\t0.055\t0.080\t0.120";
        const CurlResult r = parseCurlOutput(frame(0, meta, headers, "", "hello"));
        QVERIFY(r.framed);
        QCOMPARE(r.curlExit, 0);
        QCOMPARE(r.status, 200);
        QCOMPARE(r.effectiveUrl, QString("https://e.com/"));
        QCOMPARE(r.contentType, QString("text/html; charset=UTF-8"));
        QCOMPARE(r.sizeDownload, 5LL);
        QCOMPARE(r.redirects, 1);
        QCOMPARE(r.remoteIp, QString("203.0.113.7"));
        QCOMPARE(r.httpVersion, QString("2"));
        QCOMPARE(r.tlsVerify, 0);
        QVERIFY(qFuzzyCompare(r.timeTotal, 0.120));
        QCOMPARE(r.body, QByteArray("hello"));
        QCOMPARE(r.redirectChain.size(), 1);
        QCOMPARE(r.redirectChain.first().toMap().value("status").toInt(), 301);
        QCOMPARE(r.redirectChain.first().toMap().value("location").toString(), QString("https://e.com/"));
        QCOMPARE(headerValue(r.headers, "Content-Type"), QString("text/html; charset=UTF-8"));
        QCOMPARE(headerValue(r.headers, "X-FOLDED"), QString("a b"));   // obsolete line folding
        QCOMPARE(headerValue(r.headers, "missing"), QString());
    }
    void binaryBodiesAreCarriedByLength() {
        QByteArray body("A\0FORTIQ-BODY 3\nBBB\r\n\0\xFF", 23);   // 23 bytes: the literal has no more (ASan caught a read of 25)
        const CurlResult r = parseCurlOutput(frame(0, "FORTIQ-META\t200\thttp://e.com/\tapplication/octet-stream\t23\t0\t1.2.3.4\t1.1\t\t0\t0\t0\t0\t0", "HTTP/1.1 200 OK\r\n\r\n", "", body));
        QVERIFY(r.framed);
        QCOMPARE(r.body, body);
        QCOMPARE(r.tlsVerify, -1);     // plain http: no TLS verdict
    }
    void shortBodyIsFlaggedTruncated() {
        QByteArray out = frame(0, "FORTIQ-META\t200\thttp://e.com/\ttext/plain\t100\t0\t1.2.3.4\t1.1\t\t0\t0\t0\t0\t0", "HTTP/1.1 200 OK\r\n\r\n", "", QByteArray(10, 'x'));
        out.replace("FORTIQ-BODY 10", "FORTIQ-BODY 100");
        const CurlResult r = parseCurlOutput(out);
        QVERIFY(r.framed);
        QVERIFY(r.truncated);
        QCOMPARE(r.body.size(), 10);
    }
    void curlFailuresAreExplicit() {
        const CurlResult r = parseCurlOutput(frame(6, "FORTIQ-META\t000\t\t\t0\t0\t\t\t\t0\t0\t0\t0\t0", "", "curl: (6) Could not resolve host: nope.invalid", ""));
        QVERIFY(r.framed);
        QCOMPARE(r.curlExit, 6);
        QCOMPARE(r.status, 0);
        QVERIFY(r.curlMessage.contains("resolve host"));
        QVERIFY(describeCurlExit(6).contains("DNS"));
        QVERIFY(describeCurlExit(28).contains("Délai"));
        QVERIFY(describeCurlExit(60).contains("Certificat"));
        QVERIFY(describeCurlExit(63).contains("5 Mio"));
        QVERIFY(describeCurlExit(0).isEmpty());
    }
    void brokenOutputIsRejected() {
        QCOMPARE(parseCurlOutput("FORTIQ-ERR missing-curl\n").problem, QString("missing-curl"));
        QCOMPARE(parseCurlOutput("FORTIQ-ERR mktemp\n").problem, QString("mktemp"));
        for (const QByteArray &junk : { QByteArray(), QByteArray("garbage"), QByteArray("FORTIQ-RC x\n"), QByteArray("FORTIQ-RC 0\nFORTIQ-META\t1\n"),
                                        QByteArray("FORTIQ-RC 0\nFORTIQ-META\t200\ta\tb\t1\t0\tc\td\t0\t0\t0\t0\t0\t0\nFORTIQ-HEADERS -4\n") }) {
            const CurlResult r = parseCurlOutput(junk);
            QVERIFY(!r.framed);
            QVERIFY(!r.problem.isEmpty());
        }
    }

    // ---------------- text decoding ----------------
    void decodesCharsets() {
        QCOMPARE(decodeBody("caf\xC3\xA9", "text/plain", false).text, QString::fromUtf8("café"));
        QCOMPARE(decodeBody("caf\xE9", "text/plain; charset=ISO-8859-1", false).text, QString::fromUtf8("café"));
        QCOMPARE(decodeBody("\x93hi\x94 \x80", "text/plain; charset=\"windows-1252\"", false).text, QString::fromUtf8("“hi” €"));
        QCOMPARE(decodeBody("\xEF\xBB\xBF" "ok", "text/plain; charset=latin1", false).text, QString("ok"));     // BOM wins
        const DecodedText meta = decodeBody("<meta charset=\"iso-8859-1\"><p>caf\xE9</p>", "text/html", true);
        QCOMPARE(meta.charset, QString("iso-8859-1"));
        QVERIFY(meta.text.contains(QString::fromUtf8("café")));
        // 8-bit text that claims nothing: shown as windows-1252 rather than as garbage.
        const DecodedText legacy = decodeBody("caf\xE9", "text/plain", false);
        QCOMPARE(legacy.text, QString::fromUtf8("café"));
        QVERIFY(!legacy.note.isEmpty());
        const DecodedText unsupported = decodeBody("abc", "text/plain; charset=shift_jis", false);
        QVERIFY(unsupported.note.contains("shift_jis"));
    }

    // ---------------- HTML sanitizer ----------------
    void removesActiveContent() {
        const auto page = sanitizeHtml("<p>Hi</p><script>alert(1)</script><style>p{x:y}</style><iframe src=x>fallback</iframe>"
                                       "<object data=y>o</object><svg><script>1</script></svg><noscript>n</noscript><p>Bye</p>", "https://e.com/", false);
        QCOMPARE(page.html, QString("<p>Hi</p><p>Bye</p>"));
        QCOMPARE(page.scriptCount, 2);   // including the one hidden inside the removed <svg>
        QVERIFY(!page.plainText.contains("alert"));
    }
    void removesEveryAttributeButTheWhitelisted() {
        QCOMPARE(clean("<p onclick=\"x()\" style=\"color:red\" class=a id=b onmouseover=y>A</p>"), QString("<p>A</p>"));
        QCOMPARE(clean("<table><tr><td colspan=2 onclick=x style=z>A</td><td colspan=\"9999999\">B</td></tr></table>"),
                 QString("<table><tr><td colspan=\"2\">A</td><td>B</td></tr></table>"));
        QCOMPARE(clean("<ol start=3 onclick=x><li>a</li></ol>"), QString("<ol start=\"3\"><li>a</li></ol>"));
    }
    void linksAreResolvedAndFiltered() {
        const auto page = sanitizeHtml("<a href=\"/doc?a=1&b=2\" onclick=x target=_blank>Doc</a> "
                                       "<a href=\"javascript:alert(1)\">J</a> <a href=\"JAVASCRIPT:x\">K</a> "
                                       "<a href=\"data:text/html,x\">D</a> <a href=\"mailto:a@b.c\">M</a> <a href=\"#top\">T</a> "
                                       "<a href=\"../up\">U</a> <a>none</a>", "https://e.com/a/b/c", false);
        QCOMPARE(page.linkCount, 2);
        QVERIFY(page.html.contains("<a href=\"https://e.com/doc?a=1&amp;b=2\">Doc</a>"));
        QVERIFY(page.html.contains("<a href=\"https://e.com/a/up\">U</a>"));
        QVERIFY(!page.html.contains("javascript", Qt::CaseInsensitive));
        QVERIFY(!page.html.contains("data:"));
        QVERIFY(!page.html.contains("mailto"));
        QVERIFY(page.html.contains("<a>J</a>"));    // the text stays, the target is gone
        QVERIFY(!page.html.contains("target"));
    }
    void imagesBecomeAltText() {
        const auto page = sanitizeHtml("<p><img src=\"http://track.example/p.gif\" alt=\"Logo FORTIQ\" onerror=x> <img src=y></p>", "https://e.com/", false);
        QCOMPARE(page.imageCount, 2);
        QVERIFY(page.html.contains("[Logo FORTIQ]"));
        QVERIFY(!page.html.contains("track.example"));
        QVERIFY(!page.html.contains("<img"));
        QVERIFY(!page.html.contains("src"));
    }
    void decodesAndReescapesEntities() {
        QCOMPARE(decodeEntities("&lt;b&gt; &amp; &eacute; &#233; &#x41; &unknown; &#0; &#xD800;"), QString::fromUtf8("<b> & é é A &unknown; &#0; &#xD800;"));
        QCOMPARE(clean("<p>&lt;b&gt; &amp; &eacute; &#x41;</p>"), QString::fromUtf8("<p>&lt;b&gt; &amp; é A</p>"));
        QCOMPARE(clean("<p>&lt;script&gt;alert(1)&lt;/script&gt;</p>"), QString("<p>&lt;script&gt;alert(1)&lt;/script&gt;</p>"));   // text stays text
    }
    void toleratesBrokenMarkup() {
        QVERIFY(balanced(clean("<p>One<p>Two<ul><li>a<li>b")));
        QVERIFY(balanced(clean("<b><i>x</b></i>y")));
        QVERIFY(balanced(clean("</div></p>text</span><p>ok</p>")));
        QCOMPARE(clean("a < b and c > d"), QString("a &lt; b and c &gt; d"));
        QVERIFY(!clean("<<<>>><a href=\"x").contains("<script"));
        QVERIFY(balanced(clean("<div><div><div>deep")));
    }
    void caseAndNestingTricksDoNotSurvive() {
        for (const QString &in : { QString("<SCRIPT>alert(1)</SCRIPT>"), QString("<ScRiPt >alert(1)</sCrIpT >"),
                                   QString("<scr<script>ipt>alert(1)</scr</script>ipt>"), QString("<script\n>alert(1)</script\n>"),
                                   QString("<script>alert(1)"), QString("<img src=x onerror=alert(1)>"), QString("<a href=\" javascript:alert(1)\">x</a>"),
                                   QString("<a href=\"java\tscript:alert(1)\">x</a>"), QString("<style>@import url(http://x)</style>") }) {
            const QString out = clean(in);
            QVERIFY2(!out.contains("<script", Qt::CaseInsensitive), qPrintable(in));
            QVERIFY2(!out.contains("onerror", Qt::CaseInsensitive), qPrintable(in));
            QVERIFY2(!out.contains("javascript:", Qt::CaseInsensitive), qPrintable(in));
            QVERIFY2(!out.contains("<img"), qPrintable(in));
            QVERIFY2(!out.contains("@import"), qPrintable(in));
            QVERIFY2(balanced(out), qPrintable(in + " -> " + out));
        }
    }
    void titleAndHead() {
        auto page = sanitizeHtml("<html><head><title> My &amp; Page </title><meta charset=utf-8><link rel=stylesheet href=x.css></head><body><p>X</p></body></html>", "https://e.com/", false);
        QCOMPARE(page.title, QString("My & Page"));
        QCOMPARE(page.html, QString("<p>X</p>"));
        page = sanitizeHtml("<html><head><title>T</title><body><p>X</p>", "https://e.com/", false);   // head never closed
        QCOMPARE(page.title, QString("T"));
        QCOMPARE(page.html, QString("<p>X</p>"));
    }
    void metaRefreshIsReportedNotFollowed() {
        const auto page = sanitizeHtml("<meta http-equiv=\"refresh\" content=\"0; url=/next?x=1\"><p>Moved</p>", "https://e.com/a", false);
        QCOMPARE(page.refreshUrl, QString("https://e.com/next?x=1"));
        const auto bad = sanitizeHtml("<meta http-equiv=\"refresh\" content=\"0; url=javascript:alert(1)\">", "https://e.com/a", false);
        QVERIFY(bad.refreshUrl.isEmpty());
    }
    void spacesAroundInlineElementsStayOutside() {
        QCOMPARE(clean("<p>First with a <a href=\"/x\">link</a> and <b>bold </b>next, <i>ok</i>.</p>"),
                 QString("<p>First with a <a href=\"https://e.com/x\">link</a> and <b>bold</b> next, <i>ok</i>.</p>"));
        QCOMPARE(clean("<p>a<b>b</b>c</p>"), QString("<p>a<b>b</b>c</p>"));                 // no invented spaces
        QCOMPARE(clean("<p>  lead</p>  <p>next  </p>"), QString("<p>lead</p><p>next</p>"));
        QCOMPARE(clean("<ul><li>one</li> <li>two</li></ul>"), QString("<ul><li>one</li><li>two</li></ul>"));
    }
    void preformattedTextKeepsItsWhitespace() {
        QCOMPARE(clean("<pre>a\n  b</pre><p>x   \n  y</p>"), QString("<pre>a\n  b</pre><p>x y</p>"));
    }
    void readerModeKeepsTheMainContent() {
        const QString page = "<header>Logo</header><nav>Menu</nav><main><h1>T</h1><p>Body</p></main><footer>Copyright</footer>";
        QCOMPARE(clean(page, "https://e.com/", true), QString("<div><h1>T</h1><p>Body</p></div>"));
        const QString full = clean(page, "https://e.com/", false);
        QVERIFY(full.contains("Menu") && full.contains("Copyright") && full.contains("Body"));
        QVERIFY(sanitizeHtml(page, "https://e.com/", true).readerExtracted);
        // No <main>/<article>: furniture is dropped, the rest stays.
        QCOMPARE(clean("<nav>M</nav><p>X</p><footer>F</footer><aside>A</aside>", "https://e.com/", true), QString("<p>X</p>"));
        QVERIFY(!sanitizeHtml("<p>X</p>", "https://e.com/", true).readerExtracted);
    }
    void limitsOutputAndDepth() {
        QString big;
        for (int i = 0; i < 400000; ++i) big += "<p>xxxxxxxx</p>";
        const auto page = sanitizeHtml(big, "https://e.com/", false);
        QVERIFY(page.truncated);
        QVERIFY(page.html.size() < 2 * 1024 * 1024 + 4096);
        QVERIFY(balanced(page.html));
        QString deep;
        for (int i = 0; i < 2000; ++i) deep += "<div>";
        deep += "end";
        QVERIFY(balanced(clean(deep)));
    }
    // ---- Address policy (audit R1) ----
    void classifiesAddresses_data() {
        QTest::addColumn<QString>("ip");
        QTest::addColumn<int>("cls");   // 0 public, 1 internal, 2 blocked
        auto row = [](const char *ip, int cls) { QTest::newRow(ip) << QString::fromLatin1(ip) << cls; };
        for (const char *ip : { "93.184.216.34", "8.8.8.8", "1.1.1.1", "172.15.255.255", "172.32.0.1", "100.63.255.255", "100.128.0.1", "2606:4700:4700::1111", "2a00:1450:4001::1" }) row(ip, 0);
        for (const char *ip : { "127.0.0.1", "127.255.255.254", "10.0.0.1", "10.255.255.255", "172.16.0.1", "172.31.255.255", "192.168.1.1", "100.64.0.1", "100.127.255.255", "198.18.0.1", "198.19.255.255",
                                "192.0.2.5", "198.51.100.1", "203.0.113.9", "::1", "fc00::1", "fd12:3456::1", "2001:db8::1", "fec0::1" }) row(ip, 1);
        for (const char *ip : { "169.254.169.254", "169.254.170.2", "169.254.0.1", "169.254.1.1", "100.100.100.200", "0.0.0.0", "0.1.2.3", "224.0.0.1", "239.255.255.255", "240.0.0.1", "255.255.255.255",
                                "::", "fe80::1", "febf::1", "ff02::1", "fd00:ec2::254" }) row(ip, 2);
        // IPv4 inside IPv6: judged by the IPv4 (the classic bypasses)
        row("::ffff:127.0.0.1", 1); row("::ffff:169.254.169.254", 2); row("::ffff:8.8.8.8", 0); row("::ffff:10.1.2.3", 1);
        row("64:ff9b::7f00:1", 1); row("64:ff9b::a9fe:a9fe", 2); row("64:ff9b::808:808", 0);
        row("2002:7f00:1::", 1); row("2002:a9fe:a9fe::1", 2); row("2002:808:808::1", 0);
        row("::127.0.0.1", 1); row("fe80::1%eth0", 2);
        row("not an address", 2); row("", 2); row("999.1.1.1", 2);
    }
    void classifiesAddresses() {
        QFETCH(QString, ip);
        QFETCH(int, cls);
        const AddressVerdict v = classifyAddress(ip);
        QCOMPARE(static_cast<int>(v.cls == AddressClass::Public ? 0 : v.cls == AddressClass::Internal ? 1 : 2), cls);
        QVERIFY(!v.label.isEmpty());
    }
    void metadataIsNamedAsSuch() {
        QVERIFY(classifyAddress("169.254.169.254").label.contains("métadonnées"));
        QVERIFY(classifyAddress("fd00:ec2::254").label.contains("métadonnées"));
        QVERIFY(classifyAddress("127.0.0.1").label.contains("localhost"));
    }

    void plansCanonicalHostsOnly() {
        auto plan = [](const char *url) { return planHost(QString::fromLatin1(url)); };
        QVERIFY(plan("https://example.com/").ok); QCOMPARE(plan("https://example.com/").port, 443);
        QCOMPARE(plan("http://example.com/").port, 80); QCOMPARE(plan("http://example.com:8080/x").port, 8080);
        QVERIFY(plan("http://my-host_1.sub.example.org/").ok);
        QVERIFY(plan("http://127.0.0.1:8099/").literalIp); QCOMPARE(plan("http://127.0.0.1:8099/").ip, QString("127.0.0.1"));
        QVERIFY(plan("http://[::1]:8099/").literalIp); QCOMPARE(plan("http://[::1]:8099/").ip, QString("::1"));
        QVERIFY(plan("http://[::ffff:127.0.0.1]/").literalIp);
        QCOMPARE(plan("http://[::ffff:127.0.0.1]/").ip, QString("::ffff:127.0.0.1"));
        // Odd IPv4 spellings. The product normalises every address with QUrl first, which already rewrites decimal / hex / octal / short
        // forms to the dotted quad; whatever Qt leaves unrewritten is refused here. Either way the address curl gets is canonical.
        auto fromText = [](const char *text) {
            const UrlCheck c = normalizeUrl(QString::fromLatin1(text));
            return c.ok ? planHost(c.url) : HostPlan();
        };
        for (const char *text : { "http://2130706433/", "http://0x7f000001/", "http://0x7f.0.0.1/", "http://0177.0.0.1/", "http://127.1/", "http://127.0.1/", "http://017700000001/" }) {
            const HostPlan p = fromText(text);
            QVERIFY2(!p.ok || (p.literalIp && p.ip == "127.0.0.1"), text);          // refused, or exactly the loopback it means
            if (p.ok) QCOMPARE(classifyAddress(p.ip).cls, AddressClass::Internal);
        }
        for (const char *text : { "http://2852039166/", "http://0xa9fea9fe/", "http://0251.0376.0251.0376/", "http://169.254.43518/", "http://0xa9.0xfe.0xa9.0xfe/" }) {
            const HostPlan p = fromText(text);
            QVERIFY2(!p.ok || (p.literalIp && p.ip == "169.254.169.254"), text);   // refused, or the metadata address, which is then blocked
            if (p.ok) QCOMPARE(classifyAddress(p.ip).cls, AddressClass::Blocked);
        }
        for (const char *url : { "http://256.1.1.1/", "http://1.2.3.4.5/", "http://example.123/", "http://0x7f.example.com/", "http://127.0.0.1./" }) {
            const HostPlan p = plan(url);
            QVERIFY2(!p.ok && !p.error.isEmpty(), url);
        }
        // Metadata host names never get as far as a lookup.
        for (const char *url : { "http://metadata.google.internal/computeMetadata/v1/", "http://METADATA/", "http://instance-data/latest/", "http://instance-data.ec2.internal/" }) {
            const HostPlan p = plan(url);
            QVERIFY2(p.blockedName && !p.ok, url);
        }
        QVERIFY(!plan("ftp://example.com/").ok);
        QVERIFY(!plan("http://exa mple.com/").ok);
        QVERIFY(!plan("http://-evil.example/").ok);
    }

    void decidesFromResolvedAddresses() {
        QCOMPARE(decideAddresses({ "93.184.216.34" }).kind, PolicyDecision::Kind::Allow);
        QCOMPARE(decideAddresses({ "93.184.216.34" }).pinIp, QString("93.184.216.34"));
        QCOMPARE(decideAddresses({ "93.184.216.34", "2606:2800:220:1::1" }).kind, PolicyDecision::Kind::Allow);
        QCOMPARE(decideAddresses({ "127.0.0.1" }).kind, PolicyDecision::Kind::Confirm);
        QCOMPARE(decideAddresses({ "10.1.2.3" }).pinIp, QString("10.1.2.3"));
        // DNS rebinding shape: a public address next to an internal one is internal, and the internal one is what gets pinned and confirmed.
        const PolicyDecision mixed = decideAddresses({ "93.184.216.34", "127.0.0.1" });
        QCOMPARE(mixed.kind, PolicyDecision::Kind::Confirm);
        QCOMPARE(mixed.pinIp, QString("127.0.0.1"));
        QVERIFY(mixed.reason.contains("127.0.0.1"));
        QCOMPARE(mixed.details.size(), 2);
        QCOMPARE(mixed.details.at(0).toMap().value("cls").toString(), QString("public"));
        QCOMPARE(mixed.details.at(1).toMap().value("cls").toString(), QString("internal"));
        // Anything blocked in the set blocks the whole request: no way to confirm it.
        const PolicyDecision meta = decideAddresses({ "93.184.216.34", "169.254.169.254" });
        QCOMPARE(meta.kind, PolicyDecision::Kind::Block);
        QVERIFY(meta.pinIp.isEmpty());
        QVERIFY(meta.reason.contains("169.254.169.254"));
        QCOMPARE(decideAddresses({ "127.0.0.1", "fe80::1" }).kind, PolicyDecision::Kind::Block);
        QCOMPARE(decideAddresses({}).kind, PolicyDecision::Kind::Block);
        QCOMPARE(decideAddresses({ "garbage" }).kind, PolicyDecision::Kind::Block);
    }

    void resolveScriptAndOutput() {
        const QByteArray script = buildResolveScript("exa'mple.com");
        QVERIFY(script.contains("getent ahosts 'exa'\\''mple.com'"));
        QVERIFY(script.contains("head -n 8"));
        QVERIFY(!script.contains("curl"));
        QString problem;
        QCOMPARE(parseResolveOutput("93.184.216.34\n2606:2800:220:1:248:1893:25c8:1946\n93.184.216.34\n", &problem), QStringList({ "93.184.216.34", "2606:2800:220:1:248:1893:25c8:1946" }));
        QCOMPARE(parseResolveOutput("fe80::1%eth0\n127.0.0.1\n", &problem), QStringList({ "fe80::1", "127.0.0.1" }));      // zone dropped
        QVERIFY(parseResolveOutput("", &problem).isEmpty());
        QVERIFY(parseResolveOutput("not-an-ip\n; rm -rf /\n", &problem).isEmpty());
        QVERIFY(parseResolveOutput("FORTIQ-ERR missing-getent\n", &problem).isEmpty());
        QCOMPARE(problem, QString("missing-getent"));
        QCOMPARE(curlResolveSpec("example.com", 443, "93.184.216.34"), QString("example.com:443:93.184.216.34"));
        QCOMPARE(curlResolveSpec("example.com", 8443, "2606:2800::1"), QString("example.com:8443:[2606:2800::1]"));
        QVERIFY(curlResolveSpec("example.com", 80, "").isEmpty());
    }

    // The pin and the hardening flags are part of the script; curl never follows a redirect by itself any more.
    void curlScriptIsPinnedAndDoesNotFollowRedirects() {
        const QByteArray plain = buildCurlScript("https://example.com/a?b=1");
        QVERIFY(plain.contains("curl -q --silent"));                 // -q first: ~/.curlrc is ignored
        QVERIFY(plain.contains("--noproxy '*'"));
        QVERIFY(plain.contains("--proto '=http,https'"));
        QVERIFY(!plain.contains("--location"));
        QVERIFY(!plain.contains("--max-redirs"));
        QVERIFY(!plain.contains("--resolve"));
        const QByteArray pinned = buildCurlScript("https://example.com/", "example.com:443:93.184.216.34");
        QVERIFY(pinned.contains("--resolve 'example.com:443:93.184.216.34'"));
        QVERIFY(pinned.contains("--max-filesize 5242880") && pinned.contains("--max-time 30"));
        QVERIFY(buildCurlScript("https://x.example/", "x.example:443:[::1]").contains("--resolve 'x.example:443:[::1]'"));
    }

    void redirectTargets() {
        auto result = [](int status, const char *location) {
            CurlResult r; r.status = status;
            if (location) r.headers.push_back(QVariantMap { { "name", "Location" }, { "value", QString::fromLatin1(location) } });
            return r;
        };
        QCOMPARE(redirectLocation(result(302, "/next")), QString("/next"));
        QCOMPARE(redirectLocation(result(301, "https://example.com/x")), QString("https://example.com/x"));
        for (int status : { 303, 307, 308 }) QVERIFY(!redirectLocation(result(status, "/a")).isEmpty());
        QVERIFY(redirectLocation(result(200, "/next")).isEmpty());   // a Location on a normal page is not a redirect
        QVERIFY(redirectLocation(result(304, "/next")).isEmpty());
        QVERIFY(redirectLocation(result(302, nullptr)).isEmpty());
        // What the controller does with the target: relative is resolved, unsafe schemes are dropped.
        QCOMPARE(resolveLink("https://a.example/dir/page", "../other"), QString("https://a.example/other"));
        QVERIFY(resolveLink("https://a.example/", "file:///etc/passwd").isEmpty());
        QVERIFY(resolveLink("https://a.example/", "javascript:alert(1)").isEmpty());
    }

    // Audit 23.10.2026: the output grammar, not a blacklist. Whatever is fed in, every tag that comes out is in a small allow-list, carries
    // only href (http/https) / colspan / rowspan / start, and nothing executable or loadable survives.
    void structuredAttacksProduceOnlyTheAllowedGrammar() {
        static const QStringList fragments {
            "<script>alert(1)</script>", "<SCRIPT SRC=//x.example/a.js></SCRIPT>", "<img src=x onerror=alert(1)>", "<img src=\"http://track.example/p.gif\">",
            "<a href=\"javascript:alert(1)\">x</a>", "<a href=\"  JaVaScRiPt:alert(1)\">x</a>", "<a href=\"data:text/html;base64,PHNjcmlwdD4=\">d</a>",
            "<a href=\"vbscript:x\">v</a>", "<a href=\"//evil.example/x\">protocol-relative</a>", "<a href=\"https://ok.example/p?q=1&amp;r=2\" onclick=\"x()\" style=\"color:red\">ok</a>",
            "<a href=\"file:///etc/passwd\">f</a>", "<a href=\"ftp://x\">ftp</a>", "<iframe src=\"//x\"></iframe>", "<object data=x></object>", "<embed src=x>",
            "<svg onload=alert(1)><circle/></svg>", "<math><mi xlink:href=\"javascript:x\">x</mi></math>", "<form action=\"//x\"><input name=a value=b><button>go</button></form>",
            "<link rel=stylesheet href=//x.css>", "<meta http-equiv=\"refresh\" content=\"0;url=javascript:x\">", "<base href=\"//evil.example/\">",
            "<style>body{background:url(//x)}</style>", "<div style=\"background:url(javascript:x)\">s</div>", "<body onload=alert(1)>", "<p onmouseover=\"x()\">hover</p>",
            "<video src=x controls></video>", "<audio src=x></audio>", "<textarea><script>x</script></textarea>", "<noscript><img src=x onerror=y></noscript>",
            "<table><tr><td colspan=2 rowspan=3 onclick=x>c</td></tr></table>", "<ol start=\"3\" type=\"I\"><li>i</li></ol>", "<!-- <script>x</script> -->", "<![CDATA[<script>x</script>]]>",
            "<a href=\"https://ok.example/\"><a href=\"https://ok2.example/\">nested</a></a>", "&lt;script&gt;alert(1)&lt;/script&gt;", "&#x3C;script&#x3E;", "<scr<script>ipt>x</scr</script>ipt>",
            "<a href='https://ok.example/\" onclick=\"x'>quote</a>", "<a href=`javascript:x`>b</a>", "\x00<script>", "<template><script>x</script></template>", "<h1>Title</h1><p>text <b>bold</b></p>" };
        QRandomGenerator rng(20261023);
        static const QSet<QString> allowedTags { "a", "p", "br", "b", "i", "em", "strong", "u", "s", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "table", "thead", "tbody",
                                                "tfoot", "tr", "td", "th", "caption", "blockquote", "pre", "code", "hr", "div", "span", "sub", "sup", "dl", "dt", "dd", "small", "big", "center", "tt", "cite", "q", "del", "ins", "mark", "abbr", "font" };
        static const QRegularExpression tagRe(QStringLiteral("<(/?)([A-Za-z][A-Za-z0-9]*)([^<>]*)>"));
        static const QRegularExpression attrRe(QStringLiteral("([A-Za-z-]+)=\"([^\"]*)\""));
        for (int round = 0; round < 4000; ++round) {
            QString in;
            const int parts = rng.bounded(1, 7);
            for (int i = 0; i < parts; ++i) in += fragments.at(rng.bounded(fragments.size())) + (rng.bounded(3) == 0 ? QStringLiteral("text ") : QString());
            const auto page = sanitizeHtml(in, "https://base.example/dir/", round % 3 == 0);
            const QString out = page.html;
            auto it = tagRe.globalMatch(out);
            while (it.hasNext()) {
                const auto m = it.next();
                const QString tag = m.captured(2).toLower();
                QVERIFY2(allowedTags.contains(tag), qPrintable(tag + " in " + out + " from " + in));
                const QString attrs = m.captured(3);
                auto ai = attrRe.globalMatch(attrs);
                QString leftover = attrs;
                while (ai.hasNext()) {
                    const auto a = ai.next();
                    const QString name = a.captured(1).toLower(), value = a.captured(2);
                    leftover.remove(a.captured(0));
                    if (name == "href") {
                        QVERIFY2(tag == "a", qPrintable(out));
                        QVERIFY2(value.startsWith("http://") || value.startsWith("https://"), qPrintable(value + " in " + out + " from " + in));
                        QVERIFY2(!value.contains('"') && !value.contains('<') && !value.contains('>'), qPrintable(value));
                    } else {
                        QVERIFY2(name == "colspan" || name == "rowspan" || name == "start", qPrintable(name + " in " + out + " from " + in));
                        bool numeric = false;
                        value.toInt(&numeric);
                        QVERIFY2(numeric, qPrintable(value));
                    }
                }
                QVERIFY2(leftover.trimmed().isEmpty() || leftover.trimmed() == "/", qPrintable("leftover [" + leftover + "] in " + out));
            }
            QVERIFY2(!out.contains("<!--") && !out.contains("<![CDATA["), qPrintable(out));
            QVERIFY2(!out.contains("alert(1)") || !out.contains("<script"), qPrintable(out));
            QVERIFY2(!out.contains(QChar(0)), qPrintable(out));
        }
    }
    void survivesRandomGarbage() {
        QRandomGenerator rng(12345);
        const QString alphabet = QStringLiteral("<>/=\"' abpscriptaimghrefon&#;!-x\n");
        for (int round = 0; round < 3000; ++round) {
            QString in;
            const int len = rng.bounded(1, 200);
            for (int i = 0; i < len; ++i) in += alphabet.at(rng.bounded(alphabet.size()));
            const auto page = sanitizeHtml(in, "https://e.com/", round % 2 == 0);
            QVERIFY2(!page.html.contains("<script", Qt::CaseInsensitive), qPrintable(in));
            QVERIFY2(!page.html.contains("<img"), qPrintable(in));
            QVERIFY2(!page.html.contains("javascript:", Qt::CaseInsensitive), qPrintable(in));
            QVERIFY2(balanced(page.html), qPrintable(in + " -> " + page.html));
        }
    }
};
}

QTEST_GUILESS_MAIN(fortiq::BrowserTest)
#include "BrowserTest.moc"
