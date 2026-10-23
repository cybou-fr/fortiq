#!/usr/bin/env python3
"""Disposable web server for the Browser live tests (run on the TEST Linux server only).

    python3 web_fixture.py 8099

Serves on 127.0.0.1 only. Paths:
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
"""
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PAGE = b"""<!doctype html><html><head><title>Fixture page</title><meta charset="utf-8">
<meta http-equiv="refresh" content="0; url=/page2"><style>p{color:red}</style></head>
<body><nav><a href="/">Home</a></nav>
<main><h1>Hello fixture</h1><p>First paragraph with a <a href="/page2">link</a> and <a href="javascript:alert(1)">bad link</a>.</p>
<img src="http://127.0.0.1:1/track.gif" alt="Tracking pixel"><iframe src="/page2">frame text</iframe>
<script>document.write('executed')</script></main><footer>Footer text</footer></body></html>"""


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
        if path == "/page":
            self.reply(200, PAGE, "text/html; charset=utf-8")
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
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
