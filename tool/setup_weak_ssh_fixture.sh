#!/bin/bash
# Prepares the DISPOSABLE test Debian (never a real server) for the SSH algorithm-policy live test (S-1).
# Starts extra sshd instances, each offering ONE obsolete algorithm family, so that a client which accepts them can be
# told apart from one that refuses:
#   B+0 key exchange diffie-hellman-group14-sha1 only      B+1 cipher aes128-cbc only
#   B+2 MAC hmac-sha1 only                                 B+3 host key ssh-rsa (SHA-1 signatures) only
#   B+4 control: modern algorithms only (curve25519, AES-GCM, hmac-sha2-256-etm, rsa-sha2-512 host key)
# B is the base port (default 2431). Windows reserves moving ranges of ports for Hyper-V/WinNAT (`netsh int ipv4 show excludedportrange
# protocol=tcp`): a port inside one cannot be forwarded from WSL to Windows, so tool/prepare_live_fixture.py picks a base that Windows can bind.
# Run as root. Usage: setup_weak_ssh_fixture.sh start|stop [base port]
set -e
BASE=${2:-2431}
case "${1:-start}" in
  start)
    mkdir -p /run/sshd
    ssh-keygen -A >/dev/null
    i=0
    while IFS='|' read -r offset opts; do
      port=$((BASE + ${offset#+}))
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
+0|KexAlgorithms diffie-hellman-group14-sha1
+1|Ciphers aes128-cbc
+2|MACs hmac-sha1
+3|HostKeyAlgorithms ssh-rsa;HostKey /etc/ssh/ssh_host_rsa_key
+4|KexAlgorithms curve25519-sha256;Ciphers aes256-gcm@openssh.com;MACs hmac-sha2-256-etm@openssh.com;HostKeyAlgorithms rsa-sha2-512
EOF
    sleep 1
    ss -ltn | grep -E ":($BASE|$((BASE+1))|$((BASE+2))|$((BASE+3))|$((BASE+4))) " | awk '{print $4}' | tr '\n' ' '; echo; echo ready ;;
  stop)
    for f in /run/sshd_fortiq_weak_*.pid; do [ -f "$f" ] && kill "$(cat "$f")" 2>/dev/null || true; done
    rm -f /etc/ssh/sshd_fortiq_weak_*.conf /run/sshd_fortiq_weak_*.pid
    echo stopped ;;
esac
