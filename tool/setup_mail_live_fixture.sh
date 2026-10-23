#!/bin/bash
# Prepares the DISPOSABLE test Debian (never a real server) for the live mail test
# `mailSendAndWizard`: a local IMAP server (dovecot, plain text on 127.0.0.1:1143), a mailbox user
# `fqmail` with a tricky throwaway password, one message in its mailbox, and an SMTP sink on
# 127.0.0.1:2525 (tests/fixtures/mail/smtp_sink.py, CRAM-MD5 with the same user and password).
# Run as root. Usage: setup_mail_live_fixture.sh <path to smtp_sink.py> <sink user>
set -e
export LC_ALL=C.UTF-8 DEBIAN_FRONTEND=noninteractive
SINK=${1:?path to smtp_sink.py}
SINKUSER=${2:-fqshell}
PW='Zq"7 #x\y$z;w'
command -v dovecot >/dev/null || apt-get install -y --no-install-recommends dovecot-imapd >/dev/null
command -v mbsync >/dev/null || apt-get install -y --no-install-recommends isync >/dev/null
command -v msmtp >/dev/null || apt-get install -y --no-install-recommends msmtp >/dev/null
command -v notmuch >/dev/null || apt-get install -y --no-install-recommends notmuch >/dev/null
id fqmail >/dev/null 2>&1 || useradd -m -s /usr/sbin/nologin fqmail
printf 'fqmail:%s\n' "$PW" | chpasswd
cat > /etc/dovecot/local.conf <<'EOF'
ssl = no
auth_allow_cleartext = yes
mail_driver = maildir
mail_path = %{home}/ImapStore
mail_inbox_path = %{home}/ImapStore
service imap-login {
  inet_listener imap {
    port = 1143
  }
  inet_listener imaps {
    port = 0
  }
}
EOF
if ! ss -ltn | grep -q ':1143 '; then dovecot >/dev/null 2>&1 </dev/null; fi
rm -rf /home/fqmail/ImapStore
install -d -o fqmail -m 700 /home/fqmail/ImapStore /home/fqmail/ImapStore/cur /home/fqmail/ImapStore/new /home/fqmail/ImapStore/tmp
printf 'From: Alice <alice@example.org>\nTo: me@example.net\nSubject: Message from IMAP\nMessage-ID: <imap1@example.org>\nDate: Thu, 22 Oct 2026 09:00:00 +0000\n\nhello from the imap server\n' \
  | install -o fqmail -m 600 /dev/stdin /home/fqmail/ImapStore/new/1.eml
pkill -f "python3 /tmp/smtp_sink.py" 2>/dev/null || true; sleep 1
install -m 755 "$SINK" /tmp/smtp_sink.py
rm -rf "/home/$SINKUSER/sinkout"
su "$SINKUSER" -s /bin/bash -c "setsid nohup python3 /tmp/smtp_sink.py 2525 /home/$SINKUSER/sinkout fqmail '$PW' >/dev/null 2>&1 </dev/null &"
sleep 1
ss -ltn | grep -E ':1143 |:2525 ' || { echo "services not listening" >&2; exit 1; }
echo ready
