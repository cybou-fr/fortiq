#!/bin/bash
# Adds REAL TLS to the disposable mail fixture of tool/setup_mail_live_fixture.sh, for the live test `mailTlsConnections`:
#   IMAP  127.0.0.1:1993  implicit TLS (dovecot, imaps)        IMAP  :1143  also offers STARTTLS (ssl = yes)
#   SMTP  127.0.0.1:2465  implicit TLS   SMTP :2587  STARTTLS   (tests/fixtures/mail/smtp_sink.py --tls ...)
# A throw-away CA-less certificate valid ONLY for the name "localhost" (no IP address in it) is created and installed in this
# machine's trust store, so mbsync and msmtp verify it like any other certificate: the host name "localhost" must work and the
# address 127.0.0.1 must be refused (a mismatch), which the test checks.
# Run as root on the DISPOSABLE Debian, AFTER setup_mail_live_fixture.sh (re-running that script turns TLS off again):
#   setup_mail_tls_fixture.sh <path to smtp_sink.py> <sink user>
set -e
export LC_ALL=C.UTF-8 DEBIAN_FRONTEND=noninteractive
SINK=${1:?path to smtp_sink.py}
SINKUSER=${2:-fqshell}
PW='Zq"7 #x\y$z;w'
DIR=/etc/fortiq-fixture-tls
install -d -m 755 "$DIR"
if [ ! -f "$DIR/cert.pem" ]; then
    openssl req -x509 -newkey rsa:2048 -nodes -days 30 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost" \
        -keyout "$DIR/key.pem" -out "$DIR/cert.pem" >/dev/null 2>&1
fi
chmod 644 "$DIR/cert.pem" "$DIR/key.pem"   # the sink runs as an ordinary user; the key is a throw-away
install -m 644 "$DIR/cert.pem" /usr/local/share/ca-certificates/fortiq-fixture.crt
update-ca-certificates >/dev/null 2>&1

# dovecot 2.4: the whole configuration is rewritten (same as setup_mail_live_fixture.sh, plus TLS and the imaps listener on
# 1993); the plain listener 1143 keeps working and also offers STARTTLS. Idempotent.
cat > /etc/dovecot/local.conf <<CONF
ssl = yes
ssl_server_cert_file = $DIR/cert.pem
ssl_server_key_file = $DIR/key.pem
auth_allow_cleartext = yes
mail_driver = maildir
mail_path = %{home}/ImapStore
mail_inbox_path = %{home}/ImapStore
service imap-login {
  inet_listener imap {
    port = 1143
  }
  inet_listener imaps {
    port = 1993
    ssl = yes
  }
}
CONF
dovecot stop >/dev/null 2>&1 || true
sleep 1
dovecot >/dev/null 2>&1 </dev/null
sleep 1

pkill -f "python3 /tmp/smtp_sink.py 24" 2>/dev/null || true
pkill -f "python3 /tmp/smtp_sink_tls.py" 2>/dev/null || true
sleep 1
install -m 755 "$SINK" /tmp/smtp_sink_tls.py
for spec in "2465 implicit" "2587 starttls"; do
    set -- $spec
    su "$SINKUSER" -s /bin/bash -c "setsid nohup python3 /tmp/smtp_sink_tls.py $1 /home/$SINKUSER/sinkout-tls-$2 fqmail '$PW' --tls $2 --cert $DIR/cert.pem --key $DIR/key.pem >/dev/null 2>&1 </dev/null &"
done
sleep 1
ss -ltn | grep -E ':1993 |:2465 |:2587 ' || { echo "TLS services not listening" >&2; exit 1; }
echo ready
