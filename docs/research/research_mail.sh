#!/bin/bash
export LC_ALL=C.UTF-8 LANG=C.UTF-8 DEBIAN_FRONTEND=noninteractive
apt-get install -y --no-install-recommends notmuch isync msmtp >/tmp/apt3.log 2>&1; echo apt-exit=$?
notmuch --version; mbsync --version; msmtp --version | head -1
su fqshell -c '
set -e
export LC_ALL=C.UTF-8
rm -rf ~/mailtest; mkdir -p ~/mailtest/Maildir/{cur,new,tmp}
cd ~/mailtest
cat > Maildir/new/1.eml <<MAIL
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
cat > Maildir/new/2.eml <<MAIL
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
cat > nm.conf <<CONF
[database]
path=$HOME/mailtest
[user]
name=Me
primary_email=me@example.net
CONF
export NOTMUCH_CONFIG=$HOME/mailtest/nm.conf
notmuch new 2>&1 | tail -2
echo "-- search json:"; notmuch search --format=json --limit=5 "*" | head -c 900; echo
echo "-- show json (thread, body):"; notmuch show --format=json --body=true --entire-thread=true "thread:{from:zoe}" | head -c 1400; echo
echo "-- tag:"; notmuch tag +important -- "from:zoe"; notmuch search --format=text --output=tags "*"
echo "-- count:"; notmuch count "*"
echo "-- search injection safety: query is data:"; notmuch search --format=json "subject:\"x\" and not;rm" 2>&1 | head -c 200; echo
'
echo "-- mbsync config check (no server): "; echo "IMAPStore x
Host 127.0.0.1" > /tmp/mbsyncrc; mbsync -c /tmp/mbsyncrc -l x 2>&1 | head -2
echo "-- msmtp dry run options:"; msmtp --help 2>&1 | grep -iE "serverinfo|dry|--passwordeval|--file" | head -5
