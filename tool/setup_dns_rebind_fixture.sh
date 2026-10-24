#!/bin/bash
# Prepares the DISPOSABLE test Debian (never a real server) for the Browser DNS-rebinding live test (S-9).
# Needs tool/setup_browser_policy_fixture.sh to have been run (loopback alias 93.184.216.34, web fixture on 8099).
# Starts a background loop that rewrites the /etc/hosts line of flip-fixture.test every ~15 ms between a PUBLIC address
# (93.184.216.34) and an INTERNAL one (127.0.0.1): two consecutive lookups of the name can disagree, exactly what a rebinding
# attacker's DNS server does. "stop" removes the loop and the line. Run as root.
# Usage: setup_dns_rebind_fixture.sh start|stop
set -e
case "${1:-start}" in
  start)
    pkill -f fortiq-flip-loop 2>/dev/null || true
    cat > /usr/local/bin/fortiq-flip-loop <<'EOF'
#!/bin/bash
while true; do
  for ip in 93.184.216.34 127.0.0.1; do
    sed -i '/# fortiq-flip$/d' /etc/hosts
    echo "$ip flip-fixture.test # fortiq-flip" >> /etc/hosts
    sleep 0.015
  done
done
EOF
    chmod 755 /usr/local/bin/fortiq-flip-loop
    setsid nohup /usr/local/bin/fortiq-flip-loop >/dev/null 2>&1 </dev/null &
    sleep 1
    for i in 1 2 3 4 5 6 7 8; do getent hosts flip-fixture.test | awk '{printf "%s ", $1}'; sleep 0.02; done; echo
    echo ready ;;
  stop)
    pkill -f fortiq-flip-loop 2>/dev/null || true
    sed -i '/# fortiq-flip$/d' /etc/hosts
    rm -f /usr/local/bin/fortiq-flip-loop
    echo stopped ;;
esac
