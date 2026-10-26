#!/usr/bin/env python3
"""Disposable HTTPS server for the gateway live tests (run on the TEST Linux server only).

    python3 tls_web_fixture.py <port> <cert.pem> <key.pem>     # binds 0.0.0.0 (also the loopback aliases of tool/setup_browser_policy_fixture.sh)

GET /        200 "tls ok" (the body the test compares)
GET /sni     200 "sni=<server name the client asked for>" (proves the TLS handshake was end to end with the real server)
GET /big     4 MiB of 't'
"""
import ssl
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    server_version = "FixtureTls/1"
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path.startswith("/big"):
            body = b"t" * (4 * 1024 * 1024)
        elif self.path.startswith("/sni"):
            body = ("sni=%s" % getattr(self.connection, "server_name_seen", "")).encode()
        else:
            body = b"tls ok"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    port, cert, key = int(sys.argv[1]), sys.argv[2], sys.argv[3]
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)

    def sni(sock, name, _ctx):
        sock.server_name_seen = name or ""
    ctx.sni_callback = sni
    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
