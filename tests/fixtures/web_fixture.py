#!/usr/bin/env python3
"""Disposable web server for the Browser live tests (run on the TEST Linux server only).

    python3 web_fixture.py 8099            # 127.0.0.1 only
    python3 web_fixture.py 8099 0.0.0.0    # also on the loopback aliases used by the address-policy tests
                                            # (tool/setup_browser_policy_fixture.sh)

Paths:
  /page        HTML with <main>, a script, an iframe, an image, links and a meta refresh
  /redir       302 -> /page
  /redir-file  302 -> file:///etc/passwd   (must be refused by curl's --proto-redir)
  /loop        302 -> /loop                (redirect limit)
  /json        application/json
  /latin1      text/plain; charset=ISO-8859-1 with an accented letter
  /nojs        a JavaScript-only page (almost no text)
  /big         6 MiB (above the 5 MiB limit)
  /slow        answers after 8 seconds
  /binary      application/octet-stream
  /missing     404 with an HTML body
  /to-loopback 302 -> http://127.0.0.1:<port>/page            (public host redirecting to an internal address)
  /to-metadata 302 -> http://169.254.169.254/latest/meta-data/ (cloud metadata: never fetched)
  /to-odd      302 -> http://2130706433:<port>/page            (decimal spelling of 127.0.0.1)
  /to-public   302 -> http://public-fixture.test:<port>/page
  /to-flip     302 -> http://flip-fixture.test:<port>/echo      (a name whose address flips: tool/setup_dns_rebind_fixture.sh)
  /to-other-port 302 -> http://127.0.0.1:8098/page             (another internal host:port)
  /echo        200: Host header and the local address the connection arrived on
  /js          HTML whose text exists only after JavaScript ran (title js-done)
  /canary      HTML + JavaScript that tries every route out of a browser (tests/EngineLiveTest.cpp); ?t=1 puts the results in the title (Android)
  /webrtc      HTML + JavaScript that makes an RTCPeerConnection through an about:blank iframe (the gap of the user script)
  /_hits       200: JSON {path: number of requests}, to prove that nothing was fetched before a confirmation
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PAGE = b"""<!doctype html><html><head><title>Fixture page</title><meta charset="utf-8">
<meta http-equiv="refresh" content="0; url=/page2"><style>p{color:red}</style></head>
<body><nav><a href="/">Home</a></nav>
<main><h1>Hello fixture</h1><p>First paragraph with a <a href="/page2">link</a> and <a href="javascript:alert(1)">bad link</a>.</p>
<img src="http://127.0.0.1:1/track.gif" alt="Tracking pixel"><iframe src="/page2">frame text</iframe>
<script>document.write('executed')</script></main><footer>Footer text</footer></body></html>"""


# Browser engine pages (tests/EngineLiveTest.cpp): a page that needs JavaScript to produce its text, and the leak canary.
JS_PAGE = b"""<!doctype html><html><head><meta charset="utf-8"><title>js-running</title></head><body><div id="out">js-not-run</div>
<script>
let sum = 0; for (let i = 1; i <= 1000; i++) sum += i;
fetch('/echo').then(r => r.text()).then(t => { document.getElementById('out').textContent = 'JS-OK sum=' + sum + ' echo=' + t; document.title = 'js-done'; });
</script></body></html>"""
CANARY_PAGE = b"""<!doctype html><html><head><meta charset="utf-8"><title>canary-running</title>
<link rel="dns-prefetch" href="//dnsprefetch.canary.invalid">
<link rel="preconnect" href="https://preconnect.canary.invalid">
<link rel="prefetch" href="http://prefetch.canary.invalid/x">
<link rel="prerender" href="http://prerender.canary.invalid/x">
<link rel="stylesheet" href="http://css.canary.invalid/x.css">
<style>body{background:url(http://cssimg.canary.invalid/b.png)}</style>
</head><body><div id="out">running</div>
<a id="ping" href="/echo" ping="http://ping.canary.invalid/p">ping link</a>
<iframe src="http://iframe.canary.invalid/f" style="width:1px;height:1px"></iframe>
<img src="http://img.canary.invalid/i.png" width="1" height="1">
<img src="file:///C:/Windows/win.ini" width="1" height="1">
<script src="http://script.canary.invalid/x.js"></script>
<script>
const qs = new URLSearchParams(location.search);
const cp = qs.get('cp') || '9';
const skip = (qs.get('skip') || '').split(',');   // diagnostics: skip=rtc,webtransport
const R = {};
const lim = (p, ms) => Promise.race([p, new Promise(r => setTimeout(() => r('timeout'), ms))]);
const attempt = (name, fn) => lim((async () => { try { return String(await fn()); } catch (e) { return 'err:' + (e && e.name); } })(), 9000).then(v => { R[name] = v; });
const jobs = [
  attempt('fetch-http', () => fetch('http://canary-fetch.invalid/x', { mode: 'no-cors' }).then(r => r.type)),
  attempt('fetch-https', () => fetch('https://canary-https.invalid/x', { mode: 'no-cors' }).then(r => r.type)),
  attempt('fetch-client-127', () => fetch('http://127.0.0.1:' + cp + '/c', { mode: 'no-cors' }).then(r => r.type)),
  attempt('fetch-client-localhost', () => fetch('http://localhost:' + cp + '/c', { mode: 'no-cors' }).then(r => r.type)),
  attempt('fetch-client-ipv6', () => fetch('http://[::1]:' + cp + '/c', { mode: 'no-cors' }).then(r => r.type)),
  attempt('fetch-public-ip', () => fetch('http://203.0.113.9/x', { mode: 'no-cors' }).then(r => r.type)),
  attempt('xhr', () => new Promise((ok, ko) => { const x = new XMLHttpRequest(); x.open('GET', 'http://xhr.canary.invalid/x'); x.onerror = () => ko({ name: 'xhr-error' }); x.onload = () => ok('loaded'); x.send(); })),
  attempt('websocket', () => new Promise((ok, ko) => { const w = new WebSocket('ws://canary-ws.invalid/s'); w.onerror = () => ko({ name: 'ws-error' }); w.onopen = () => ok('open'); })),
  attempt('eventsource', () => new Promise((ok, ko) => { const e = new EventSource('http://es.canary.invalid/e'); e.onerror = () => { e.close(); ko({ name: 'es-error' }); }; })),
  skip.includes('webtransport') ? Promise.resolve() : attempt('webtransport', () => typeof WebTransport === 'undefined' ? 'absent' : new WebTransport('https://wt.canary.invalid/').ready.then(() => 'ready')),
  attempt('beacon', () => navigator.sendBeacon('http://beacon.canary.invalid/b', 'x')),
  attempt('geolocation', () => new Promise(ok => navigator.geolocation ? navigator.geolocation.getCurrentPosition(() => ok('granted'), e => ok('denied-' + e.code)) : ok('absent'))),
  attempt('notification', () => typeof Notification === 'undefined' ? 'absent' : Notification.requestPermission()),
  attempt('rtc-frames', () => new Promise(ok => {
    const out = { main: typeof RTCPeerConnection, webkit: typeof webkitRTCPeerConnection };
    const b = document.createElement('iframe'); document.body.appendChild(b);
    out.blank = typeof b.contentWindow.RTCPeerConnection;
    const s = document.createElement('iframe'); s.srcdoc = '<p>x</p>';
    s.onload = () => { out.srcdoc = typeof s.contentWindow.RTCPeerConnection; ok(JSON.stringify(out)); };
    document.body.appendChild(s);
  })),
  skip.includes('rtc') ? Promise.resolve() : attempt('rtc', () => new Promise(ok => {
    const kinds = [];
    const pc = new RTCPeerConnection({ iceServers: [{ urls: 'stun:203.0.113.20:3478' }, { urls: 'stun:stun.l.google.com:19302' }] });
    pc.createDataChannel('c');
    pc.onicecandidate = e => { if (e.candidate) kinds.push(e.candidate.type + ':' + e.candidate.protocol); };
    pc.createOffer().then(o => pc.setLocalDescription(o));
    setTimeout(() => { pc.close(); ok('candidates=' + JSON.stringify(kinds)); }, 5000);
  })),
];
Promise.all(jobs).then(() => { document.getElementById('out').textContent = JSON.stringify(R); document.title = qs.get('t') ? 'canary-done:' + JSON.stringify(R) : 'canary-done'; });
</script></body></html>
"""

# The known gap of the injected WebRTC removal: an about:blank iframe made by a script is not scripted by the engine's user script, so its
# RTCPeerConnection exists. The engine's forced policy (disable_non_proxied_udp) must still stop every packet: this page uses the gap with
# STUN servers on the client machine (?udp=<port>&ip=<address of this computer as the SERVER sees... any>), tests/EngineLiveTest.cpp listens there.
WEBRTC_PAGE = b"""<!doctype html><html><head><meta charset="utf-8"><title>webrtc-running</title></head><body><div id="out">running</div>
<script>
const qs = new URLSearchParams(location.search);
const udp = qs.get('udp') || '9';
const ips = (qs.get('ips') || '127.0.0.1').split(',');
const f = document.createElement('iframe'); document.body.appendChild(f);
const via = qs.get('via') || 'contentWindow';   // 'frames': window.frames[n], the path no script can hook
const C = (via === 'frames' ? window.frames[window.frames.length - 1] : f.contentWindow).RTCPeerConnection;
const res = { gap: typeof C, candidates: [], error: '' };
function finish() { document.getElementById('out').textContent = JSON.stringify(res); document.title = qs.get('t') ? 'webrtc-done:' + JSON.stringify(res) : 'webrtc-done'; }
if (typeof C !== 'function') { finish(); } else {
  try {
    const servers = ips.map(ip => ({ urls: 'stun:' + ip + ':' + udp })).concat([{ urls: 'stun:203.0.113.30:3478' }, { urls: 'stun:stun.example.invalid:3478' }]);
    const pc = new C({ iceServers: servers });
    pc.createDataChannel('c');
    pc.onicecandidate = e => { if (e.candidate) res.candidates.push(e.candidate.type + ':' + e.candidate.protocol); };
    pc.createOffer().then(o => pc.setLocalDescription(o)).catch(e => { res.error = String(e); });
    setTimeout(() => { res.state = pc.iceGatheringState; pc.close(); finish(); }, 7000);
  } catch (e) { res.error = String(e); finish(); }
}
</script></body></html>"""

HITS = {}


class Handler(BaseHTTPRequestHandler):
    server_version = "FixtureWeb/1"

    def log_message(self, *args):
        pass

    def reply(self, code, body, ctype, extra=()):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path != "/_hits":
            HITS[path] = HITS.get(path, 0) + 1
        port = self.server.server_address[1]
        if path == "/_hits":
            self.reply(200, json.dumps(HITS).encode(), "application/json")
        elif path == "/to-loopback":
            self.reply(302, b"", "text/plain", [("Location", "http://127.0.0.1:%d/page" % port)])
        elif path == "/to-metadata":
            self.reply(302, b"", "text/plain", [("Location", "http://169.254.169.254/latest/meta-data/")])
        elif path == "/to-odd":
            self.reply(302, b"", "text/plain", [("Location", "http://2130706433:%d/page" % port)])
        elif path == "/to-public":
            self.reply(302, b"", "text/plain", [("Location", "http://public-fixture.test:%d/page" % port)])
        elif path == "/to-flip":
            self.reply(302, b"", "text/plain", [("Location", "http://flip-fixture.test:%d/echo" % port)])
        elif path == "/to-other-port":
            self.reply(302, b"", "text/plain", [("Location", "http://127.0.0.1:8098/page")])
        elif path == "/echo":
            self.reply(200, ("host=%s local=%s" % (self.headers.get("Host"), self.connection.getsockname()[0])).encode(), "text/plain")
        elif path == "/js":
            self.reply(200, JS_PAGE, "text/html; charset=utf-8")
        elif path == "/webrtc":
            self.reply(200, WEBRTC_PAGE, "text/html; charset=utf-8")
        elif path == "/canary":
            self.reply(200, CANARY_PAGE, "text/html; charset=utf-8")
        elif path == "/page":
            self.reply(200, PAGE, "text/html; charset=utf-8")
        elif path == "/searchform":
            self.reply(200, b'<html><title>Search</title><body><form action="/found" method="get"><input type="hidden" name="ie" value="UTF-8">'
                            b'<input name="q" placeholder="Rechercher"><input type="submit" value="Chercher"></form><p>Search page</p></body></html>', "text/html")
        elif path == "/found":
            from urllib.parse import parse_qs, urlsplit
            query = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            body = "<html><title>Found</title><body><p>Found: q=[%s] ie=[%s] keys=%s</p></body></html>" % (
                query.get("q", [""])[0], query.get("ie", [""])[0], ",".join(sorted(query)))
            self.reply(200, body.encode("utf-8"), "text/html; charset=utf-8")
        elif path == "/page2":
            self.reply(200, b"<html><title>Second</title><body><p>Second page</p></body></html>", "text/html")
        elif path == "/redir":
            self.reply(302, b"", "text/plain", [("Location", "/page")])
        elif path == "/redir-file":
            self.reply(302, b"", "text/plain", [("Location", "file:///etc/passwd")])
        elif path == "/loop":
            self.reply(302, b"", "text/plain", [("Location", "/loop")])
        elif path == "/json":
            self.reply(200, b'{"name":"fortiq","items":[1,2,3],"nested":{"ok":true}}', "application/json")
        elif path == "/latin1":
            self.reply(200, "café crème".encode("latin-1"), "text/plain; charset=ISO-8859-1")
        elif path == "/nojs":
            self.reply(200, b"<html><body><div id=app></div><script src=/app.js></script><script>boot()</script></body></html>", "text/html")
        elif path == "/big":
            body = b"x" * (6 * 1024 * 1024)
            self.reply(200, body, "text/plain")
        elif path == "/slow":
            time.sleep(8)
            self.reply(200, b"late", "text/plain")
        elif path == "/binary":
            self.reply(200, bytes(range(256)) * 4, "application/octet-stream")
        else:
            self.reply(404, b"<html><title>Not here</title><body><p>Nothing at this address</p></body></html>", "text/html")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
    bind = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
    ThreadingHTTPServer((bind, port), Handler).serve_forever()
