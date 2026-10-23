#!/bin/bash
# Prepares the DISPOSABLE test Debian (never a real server) for the Browser address-policy live tests.
# It adds two PUBLIC-looking addresses (93.184.216.34 / .35) as aliases of the loopback interface and host names for them in
# /etc/hosts, so that "public", "mixed public + internal", "metadata" and "private" names exist without any internet access,
# then (re)starts tests/fixtures/web_fixture.py on 0.0.0.0:8099 as the test user. WSL regenerates /etc/hosts at every start of
# the distro: run this script again afterwards.
# Run as root. Usage: setup_browser_policy_fixture.sh <path to web_fixture.py> <test user>
set -e
FIXTURE=${1:?path to web_fixture.py}
TESTUSER=${2:-fqshell}
for ip in 93.184.216.34 93.184.216.35; do
  ip addr show dev lo | grep -q "inet $ip/" || ip addr add "$ip/32" dev lo
done
sed -i '/# fortiq-policy$/d' /etc/hosts
cat >> /etc/hosts <<'EOF'
93.184.216.34 public-fixture.test # fortiq-policy
93.184.216.34 mixed-fixture.test # fortiq-policy
127.0.0.1 mixed-fixture.test # fortiq-policy
93.184.216.34 multi-fixture.test # fortiq-policy
93.184.216.35 multi-fixture.test # fortiq-policy
169.254.169.254 meta-fixture.test # fortiq-policy
127.0.0.1 internal-fixture.test # fortiq-policy
10.255.255.1 private-fixture.test # fortiq-policy
EOF
pkill -f "python3 /tmp/web_fixture.py" 2>/dev/null || true
sleep 1
install -m 755 "$FIXTURE" /tmp/web_fixture.py
su "$TESTUSER" -s /bin/bash -c "setsid nohup python3 /tmp/web_fixture.py 8099 0.0.0.0 >/dev/null 2>&1 </dev/null &"
sleep 1
ss -ltn | grep -q ':8099 ' || { echo "web fixture not listening" >&2; exit 1; }
getent ahosts mixed-fixture.test | awk '{print $1}' | sort -u | tr '\n' ' '; echo
echo ready
