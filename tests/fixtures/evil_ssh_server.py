#!/usr/bin/env python3
"""A hostile SSH "server" for the client robustness test (R14, CVE-2026-55200 class: malformed transport packets before authentication).

It speaks just enough SSH to be believed: it sends a version banner, reads the client's banner and KEXINIT, and then answers with a
malformed binary packet. Every new connection gets the next variant of a list of malformations (packet_length far too big, zero,
smaller than the padding, padding longer than the packet, a length that wraps when 4 is added, truncated packets, garbage, a
KEXINIT with absurd name-list lengths, ...), and the sequence starts again after the last one. The client must fail the connection
cleanly; it must never crash, hang, or ask the user to trust a host key.

  python3 evil_ssh_server.py <port> [variant-count-file]      # listens on 127.0.0.1 until killed; GET /count is not served here
Test machine only: never exposed beyond the loopback interface.
"""
import os
import socket
import struct
import sys
import threading
import time

BANNER = b"SSH-2.0-OpenSSH_9.9 evil\r\n"


def packet(length, padlen, payload=b"", tail=b""):
    return struct.pack(">IB", length, padlen) + payload + tail


def kexinit(name_list_len=None):
    cookie = os.urandom(16)
    lists = b"".join(struct.pack(">I", 0) for _ in range(10))
    if name_list_len is not None:
        lists = struct.pack(">I", name_list_len) + b"x" * 8 + lists
    return b"\x14" + cookie + lists + b"\x00" + struct.pack(">I", 0)


def variants():
    kx = kexinit()
    ok_len = 1 + len(kx) + 4
    yield "length-0xFFFFFFFF", packet(0xFFFFFFFF, 4, kx, b"\0" * 4)
    yield "length-0x80000000", packet(0x80000000, 4, kx, b"\0" * 4)
    yield "length-0x7FFFFFFF", packet(0x7FFFFFFF, 4, kx, b"\0" * 4)
    yield "length-0xFFFFFFFC-wraps-with-4", packet(0xFFFFFFFC, 4, kx, b"\0" * 4)
    yield "length-0xFFFFFFF0", packet(0xFFFFFFF0, 4, kx, b"\0" * 4)
    yield "length-35001", packet(35001, 4, kx, b"\0" * 4)
    yield "length-65536", packet(65536, 8, b"A" * 70000, b"")
    yield "length-0", packet(0, 0)
    yield "length-1", packet(1, 0)
    yield "length-2-padding-longer", packet(2, 200, b"x")
    yield "padding-longer-than-packet", packet(ok_len, 250, kx, b"\0" * 4)
    yield "padding-3-too-short", packet(ok_len, 3, kx, b"\0" * 3)
    yield "truncated-after-length", struct.pack(">I", ok_len)
    yield "truncated-mid-payload", packet(ok_len, 4, kx[: len(kx) // 2])
    yield "kexinit-namelist-0xFFFFFFFF", packet(1 + len(kexinit(0xFFFFFFFF)) + 4, 4, kexinit(0xFFFFFFFF), b"\0" * 4)
    yield "kexinit-namelist-0x7FFFFFFF", packet(1 + len(kexinit(0x7FFFFFFF)) + 4, 4, kexinit(0x7FFFFFFF), b"\0" * 4)
    yield "garbage-1k", os.urandom(1024)
    yield "garbage-banner-only", b""
    yield "banner-too-long", b"SSH-2.0-" + b"A" * 70000 + b"\r\n"
    yield "disconnect-huge-description", packet(1 + 1 + 4 + 4 + 4 + 4 + 4, 4, b"\x01" + struct.pack(">II", 2, 0xFFFFFFFF) + b"x" * 8, b"\0" * 4)
    yield "ignore-huge", packet(1 + 4 + 4 + 4, 4, b"\x02" + struct.pack(">I", 0xFFFFFFFF) + b"x" * 4, b"\0" * 4)
    yield "newkeys-first", packet(1 + 4, 4, b"\x15", b"\0" * 4)
    yield "kexdh-reply-huge-hostkey", packet(1 + 4 + 8, 4, b"\x1f" + struct.pack(">I", 0xFFFFFFFF) + b"y" * 8, b"\0" * 4)


ALL = list(variants())


def handle(conn, index, log):
    name, data = ALL[index % len(ALL)]
    try:
        conn.settimeout(5)
        if name != "garbage-banner-only":
            conn.sendall(BANNER if not name.startswith("banner-too-long") else data)
        try:
            conn.recv(4096)          # client banner (+ KEXINIT)
        except OSError:
            pass
        if not name.startswith("banner-too-long") and data:
            conn.sendall(data)
        time.sleep(0.3)              # let the client choke on it, then hang up (a client waiting for more bytes sees EOF)
    except OSError:
        pass
    finally:
        conn.close()
        log.write(f"{index} {name}\n")
        log.flush()


def main():
    port = int(sys.argv[1])
    log = open(sys.argv[2], "a") if len(sys.argv) > 2 else sys.stderr
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", port))
    server.listen(16)
    index = 0
    while True:
        conn, _ = server.accept()
        threading.Thread(target=handle, args=(conn, index, log), daemon=True).start()
        index += 1


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--count":
        print(len(ALL))
    else:
        main()
