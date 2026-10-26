#!/bin/bash
# Prepares the DISPOSABLE test Debian (never a real server) for the Browser gateway live tests (GatewayLiveTest).
# Needs tool/setup_browser_policy_fixture.sh to have been run (loopback aliases 93.184.216.34/.35 and their names in /etc/hosts).
# Creates a throw-away self-signed certificate (names: public-fixture.test, localhost, 127.0.0.1, 93.184.216.34) under /tmp/fortiq-gw-tls
# and starts tests/fixtures/tls_web_fixture.py on 0.0.0.0:8443 as the test user. Run as root.
# Usage: setup_gateway_fixture.sh <path to tls_web_fixture.py> <test user>   |   setup_gateway_fixture.sh stop
set -e
if [ "${1:-}" = "stop" ]; then
  pkill -f "python3 /tmp/fortiq-gw-tls/tls_web_fixture.py" 2>/dev/null || true
  rm -rf /tmp/fortiq-gw-tls
  echo stopped
  exit 0
fi
FIXTURE=${1:?path to tls_web_fixture.py}
TESTUSER=${2:-fqshell}
DIR=/tmp/fortiq-gw-tls
pkill -f "python3 $DIR/tls_web_fixture.py" 2>/dev/null || true
rm -rf "$DIR"; mkdir -p "$DIR"
openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj "/CN=public-fixture.test" \
  -addext "subjectAltName=DNS:public-fixture.test,DNS:localhost,IP:127.0.0.1,IP:93.184.216.34" \
  -keyout "$DIR/key.pem" -out "$DIR/cert.pem" >/dev/null 2>&1
install -m 644 "$FIXTURE" "$DIR/tls_web_fixture.py"
chown -R "$TESTUSER" "$DIR"; chmod 600 "$DIR/key.pem"
su "$TESTUSER" -s /bin/bash -c "setsid nohup python3 $DIR/tls_web_fixture.py 8443 $DIR/cert.pem $DIR/key.pem >/dev/null 2>&1 </dev/null &"
sleep 1
ss -ltn | grep -q ':8443 ' || { echo "TLS web fixture not listening" >&2; exit 1; }
echo ready
