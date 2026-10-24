#!/bin/bash
# Prepares the DISPOSABLE test Debian (never a real server) for the SSH algorithm-policy live test (S-1).
# Starts extra sshd instances, each offering ONE obsolete algorithm family, so that a client which accepts them can be
# told apart from one that refuses:
#   2231 key exchange diffie-hellman-group14-sha1 only      2232 cipher aes128-cbc only
#   2233 MAC hmac-sha1 only                                 2234 host key ssh-rsa (SHA-1 signatures) only
#   2235 control: modern algorithms only (curve25519, AES-GCM, hmac-sha2-256-etm, rsa-sha2-512 host key)
# Run as root. Usage: setup_weak_ssh_fixture.sh start|stop
set -e
case "${1:-start}" in
  start)
    mkdir -p /run/sshd
    ssh-keygen -A >/dev/null
    i=0
    while IFS='|' read -r port opts; do
      conf=/etc/ssh/sshd_fortiq_weak_$port.conf
      {
        echo "Port $port"; echo "ListenAddress 127.0.0.1"; echo "PidFile /run/sshd_fortiq_weak_$port.pid"
        echo "HostKey /etc/ssh/ssh_host_rsa_key"; echo "HostKey /etc/ssh/ssh_host_ecdsa_key"; echo "HostKey /etc/ssh/ssh_host_ed25519_key"
        echo "UsePAM no"; echo "PasswordAuthentication yes"
        echo "$opts" | tr ';' '\n'
      } > "$conf"
      [ -f /run/sshd_fortiq_weak_$port.pid ] && kill "$(cat /run/sshd_fortiq_weak_$port.pid)" 2>/dev/null || true
      /usr/sbin/sshd -f "$conf" || { echo "sshd on $port failed to start (algorithm not compiled in?)" >&2; /usr/sbin/sshd -t -f "$conf" >&2 || true; }
    done <<'EOF'
2231|KexAlgorithms diffie-hellman-group14-sha1
2232|Ciphers aes128-cbc
2233|MACs hmac-sha1
2234|HostKeyAlgorithms ssh-rsa;HostKey /etc/ssh/ssh_host_rsa_key
2235|KexAlgorithms curve25519-sha256;Ciphers aes256-gcm@openssh.com;MACs hmac-sha2-256-etm@openssh.com;HostKeyAlgorithms rsa-sha2-512
EOF
    sleep 1
    ss -ltn | grep -E ':223[1-5] ' | awk '{print $4}' | tr '\n' ' '; echo; echo ready ;;
  stop)
    for f in /run/sshd_fortiq_weak_*.pid; do [ -f "$f" ] && kill "$(cat "$f")" 2>/dev/null || true; done
    rm -f /etc/ssh/sshd_fortiq_weak_*.conf /run/sshd_fortiq_weak_*.pid
    echo stopped ;;
esac
