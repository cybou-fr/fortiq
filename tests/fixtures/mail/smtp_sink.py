#!/usr/bin/env python3
"""Disposable SMTP sink for the live mail tests: accepts mail on 127.0.0.1 and stores it.

Usage: smtp_sink.py <port> <outdir> <user> <password> [--tls implicit|starttls --cert FILE --key FILE]
Authentication is CRAM-MD5 only (msmtp allows it without TLS), checked against <user>/<password>.
With --tls the sink speaks TLS for real (implicit TLS from the first byte, or STARTTLS after EHLO) using the given certificate; in
that mode AUTH is refused before TLS is up. The live TLS tests use a throw-away certificate valid for "localhost" only.
Every accepted message is written to <outdir>/<n>.eml and its envelope to <outdir>/<n>.env
(lines: "from <addr>", "rcpt <addr>"). Refused logins are counted in <outdir>/auth_failed.
Never run this on a real server: it has no TLS and keeps everything it receives.
"""
import base64, hashlib, hmac, os, socketserver, ssl, sys, time

PORT, OUT, USER, PASSWORD = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
OPTIONS = dict(zip(sys.argv[5::2], sys.argv[6::2]))
TLS_MODE = OPTIONS.get("--tls", "")           # "", "implicit" or "starttls"
CONTEXT = None
if TLS_MODE:
    CONTEXT = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    CONTEXT.load_cert_chain(OPTIONS["--cert"], OPTIONS["--key"])
os.makedirs(OUT, exist_ok=True)
counter = [len([f for f in os.listdir(OUT) if f.endswith(".eml")])]


class Handler(socketserver.StreamRequestHandler):
    def send(self, text):
        self.wfile.write((text + "\r\n").encode())
        self.wfile.flush()

    def handle(self):
        self.send("220 sink ESMTP")
        authed, sender, rcpts = False, None, []
        secure = TLS_MODE == "implicit"       # implicit TLS: the socket is already wrapped (Server.get_request)
        while True:
            line = self.rfile.readline()
            if not line:
                return
            cmd = line.decode("utf-8", "replace").strip()
            up = cmd.upper()
            if up.startswith("EHLO") or up.startswith("HELO"):
                lines = ["250-sink", "250-8BITMIME"]
                if TLS_MODE == "starttls" and not secure:
                    lines.append("250-STARTTLS")
                if secure or not TLS_MODE:
                    lines.append("250 AUTH CRAM-MD5")
                else:
                    lines[-1] = lines[-1].replace("250-", "250 ", 1)
                self.wfile.write(("\r\n".join(lines) + "\r\n").encode())
                self.wfile.flush()
            elif up == "STARTTLS" and TLS_MODE == "starttls" and not secure:
                self.send("220 ready to start TLS")
                wrapped = CONTEXT.wrap_socket(self.connection, server_side=True)
                self.connection, self.rfile = wrapped, wrapped.makefile("rb")
                self.wfile = wrapped.makefile("wb", 0)
                secure, authed, sender, rcpts = True, False, None, []
            elif up.startswith("AUTH CRAM-MD5") and TLS_MODE and not secure:
                self.send("530 must issue a STARTTLS command first")
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
                # Reliability tests: the message is stored, but the acknowledgement is held back (file <outdir>/delay holds the seconds).
                delay = os.path.join(OUT, "delay")
                if os.path.exists(delay):
                    time.sleep(float(open(delay).read().strip() or 0))
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

    def get_request(self):
        sock, address = super().get_request()
        if TLS_MODE == "implicit":
            try:
                sock = CONTEXT.wrap_socket(sock, server_side=True)
            except (ssl.SSLError, OSError):
                sock.close()
                raise OSError("TLS handshake failed")
        return sock, address


Server(("127.0.0.1", PORT), Handler).serve_forever()
