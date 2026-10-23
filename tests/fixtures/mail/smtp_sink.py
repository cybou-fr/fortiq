#!/usr/bin/env python3
"""Disposable SMTP sink for the live mail tests: accepts mail on 127.0.0.1 and stores it.

Usage: smtp_sink.py <port> <outdir> <user> <password>
Authentication is CRAM-MD5 only (msmtp allows it without TLS), checked against <user>/<password>.
Every accepted message is written to <outdir>/<n>.eml and its envelope to <outdir>/<n>.env
(lines: "from <addr>", "rcpt <addr>"). Refused logins are counted in <outdir>/auth_failed.
Never run this on a real server: it has no TLS and keeps everything it receives.
"""
import base64, hashlib, hmac, os, socketserver, sys, time

PORT, OUT, USER, PASSWORD = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
os.makedirs(OUT, exist_ok=True)
counter = [len([f for f in os.listdir(OUT) if f.endswith(".eml")])]


class Handler(socketserver.StreamRequestHandler):
    def send(self, text):
        self.wfile.write((text + "\r\n").encode())
        self.wfile.flush()

    def handle(self):
        self.send("220 sink ESMTP")
        authed, sender, rcpts = False, None, []
        while True:
            line = self.rfile.readline()
            if not line:
                return
            cmd = line.decode("utf-8", "replace").strip()
            up = cmd.upper()
            if up.startswith("EHLO") or up.startswith("HELO"):
                self.wfile.write(b"250-sink\r\n250-8BITMIME\r\n250 AUTH CRAM-MD5\r\n")
                self.wfile.flush()
            elif up.startswith("AUTH CRAM-MD5"):
                challenge = "<%d.%d@sink>" % (os.getpid(), time.time_ns())
                self.send("334 " + base64.b64encode(challenge.encode()).decode())
                reply = base64.b64decode(self.rfile.readline().strip()).decode()
                name, _, digest = reply.partition(" ")
                expected = hmac.new(PASSWORD.encode(), challenge.encode(), hashlib.md5).hexdigest()
                if name == USER and hmac.compare_digest(digest, expected):
                    authed = True
                    self.send("235 ok")
                else:
                    open(os.path.join(OUT, "auth_failed"), "a").write("x")
                    self.send("535 authentication failed")
            elif up.startswith("MAIL FROM:"):
                if not authed:
                    self.send("530 authentication required")
                    continue
                sender, rcpts = cmd[10:].strip().strip("<>").split(">")[0].strip("<"), []
                self.send("250 ok")
            elif up.startswith("RCPT TO:"):
                rcpts.append(cmd[8:].strip().strip("<>").split(">")[0].strip("<"))
                self.send("250 ok")
            elif up == "DATA":
                self.send("354 go")
                body = []
                while True:
                    l = self.rfile.readline()
                    if l in (b".\r\n", b".\n", b""):
                        break
                    body.append(l[1:] if l.startswith(b"..") else l)
                n = counter[0] = counter[0] + 1
                open(os.path.join(OUT, "%d.eml" % n), "wb").write(b"".join(body))
                open(os.path.join(OUT, "%d.env" % n), "w").write("from %s\n" % sender + "".join("rcpt %s\n" % r for r in rcpts))
                self.send("250 queued")
            elif up == "RSET":
                sender, rcpts = None, []
                self.send("250 ok")
            elif up == "NOOP":
                self.send("250 ok")
            elif up == "QUIT":
                self.send("221 bye")
                return
            else:
                self.send("502 not implemented")


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


Server(("127.0.0.1", PORT), Handler).serve_forever()
