#!/bin/bash
# Builds a disposable notmuch database with varied messages (run as an ordinary user on a
# test Debian, never on a real mailbox) and prints the JSON that tests/fixtures/mail/ keeps.
# Usage: make_mail_fixture.sh <output dir>
set -e
export LC_ALL=C.UTF-8
OUT=${1:?output dir}
ROOT=$HOME/mailfix
rm -rf "$ROOT"; mkdir -p "$ROOT/Maildir/"{cur,new,tmp} "$OUT"
cd "$ROOT"
cat > Maildir/new/1.eml <<'MAIL'
From: =?UTF-8?B?Wm/DqyBNw7xsbGVy?= <zoe@example.org>
To: me@example.net
Subject: =?UTF-8?Q?Caf=C3=A9_au_lait_=E2=9C=93?=
Date: Tue, 21 Oct 2026 10:00:00 +0000
Message-ID: <m1@example.org>
MIME-Version: 1.0
Content-Type: multipart/alternative; boundary=XX

--XX
Content-Type: text/plain; charset=utf-8

Bonjour, plain text body.
--XX
Content-Type: text/html; charset=utf-8

<html><body><p>Bonjour, <b>HTML</b> body</p><script>alert(1)</script><img src="http://track.example/p.gif"></body></html>
--XX--
MAIL
cat > Maildir/new/2.eml <<'MAIL'
From: bob@example.com
To: me@example.net
Subject: Re: Café au lait ✓
In-Reply-To: <m1@example.org>
References: <m1@example.org>
Date: Tue, 21 Oct 2026 11:00:00 +0000
Message-ID: <m2@example.com>
Content-Type: text/plain; charset=utf-8

Reply with an attachment mention.
MAIL
cat > Maildir/new/3.eml <<'MAIL'
From: Alice <alice@example.org>
To: me@example.net
Subject: Rapport
Date: Wed, 22 Oct 2026 08:30:00 +0000
Message-ID: <m3@example.org>
MIME-Version: 1.0
Content-Type: multipart/mixed; boundary=YY

--YY
Content-Type: text/plain; charset=utf-8
Content-Transfer-Encoding: quoted-printable

Voir le rapport en pi=C3=A8ce jointe.
--YY
Content-Type: application/pdf; name="=?UTF-8?B?UmFwcG9ydCDDqXTDqS5wZGY=?="
Content-Disposition: attachment; filename*=UTF-8''Rapport%20%C3%A9t%C3%A9.pdf
Content-Transfer-Encoding: base64

JVBERi0xLjQKJSBmYWtlCg==
--YY--
MAIL
cat > Maildir/new/4.eml <<'MAIL'
From: "Promo" <news@shop.example>
To: me@example.net
Subject: Offre <b>speciale</b>
Date: Wed, 22 Oct 2026 09:00:00 +0000
Message-ID: <m4@shop.example>
MIME-Version: 1.0
Content-Type: text/html; charset=utf-8

<html><head><style>body{background:url(http://x.example/y.png)}</style></head><body onload="evil()"><h1>Offre</h1><p>Cliquez <a href="javascript:alert(1)">ici</a> ou <a href="https://shop.example/a?b=1">là</a>.</p><img src="http://track.example/p.gif"><script>alert(2)</script></body></html>
MAIL
printf 'From: legacy@example.org\nTo: me@example.net\nSubject: =?ISO-8859-1?Q?Caf=E9_ancien?=\nDate: Wed, 22 Oct 2026 09:30:00 +0000\nMessage-ID: <m5@example.org>\nContent-Type: text/plain; charset=iso-8859-1\nContent-Transfer-Encoding: 8bit\n\nCaf\xe9 cr\xe8me \xe0 la fran\xe7aise.\n' > Maildir/new/5.eml
cat > Maildir/new/6.eml <<'MAIL'
From: noname@example.org
To: me@example.net
Date: Wed, 22 Oct 2026 10:00:00 +0000
Message-ID: <m6@example.org>

Message sans sujet.
MAIL
cat > nm.conf <<CONF
[database]
path=$ROOT
[user]
name=Me
primary_email=me@example.net
[new]
tags=unread;inbox
CONF
export NOTMUCH_CONFIG=$ROOT/nm.conf
notmuch new >/dev/null
notmuch tag +flagged -- id:m3@example.org
notmuch tag -unread -- id:m2@example.com
notmuch search --format=json --output=summary --sort=newest-first -- '*' > "$OUT/search_all.json"
notmuch search --format=json --output=summary --sort=newest-first -- 'tag:unread' > "$OUT/search_unread.json"
notmuch search --format=json --output=summary -- 'from:nobody-here' > "$OUT/search_empty.json"
for m in m1@example.org m3@example.org m4@shop.example m5@example.org m6@example.org; do
  n=$(echo "$m" | cut -d@ -f1)
  notmuch show --format=json --body=true --include-html --entire-thread=true -- "id:$m" > "$OUT/show_$n.json"
done
notmuch search --output=tags --format=json -- '*' > "$OUT/tags.json"
notmuch count -- '*' > "$OUT/count_all.txt"
notmuch count -- 'tag:unread' > "$OUT/count_unread.txt"
notmuch --version > "$OUT/version.txt"
echo done
