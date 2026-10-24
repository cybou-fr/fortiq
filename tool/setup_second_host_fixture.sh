#!/bin/bash
# Turns a COPY of the disposable test Debian into a second, independent "server B" for the live tests that must never
# mix two hosts (audit backlog 7, R10): its own SSH host keys, its own port, its own user and files, its own host name.
# Never run this on a real server and never on the first fixture: it removes the host keys and changes sshd.
#
# Create the copy once, on the Windows host:
#   wsl --export fortiq-test fortiq-b.tar
#   wsl --import fortiq-test-b <empty folder> fortiq-b.tar --version 2
# then, as root inside it (the fixture key of the first host is accepted for the new user):
#   bash tool/setup_second_host_fixture.sh <authorized_keys file of the first fixture user> [port, default 2223] [host name, default fqhost-b]
#
# Afterwards `wsl --terminate fortiq-test-b` and start it again (the new host name is read at start); it prints the host key
# fingerprints to give to the tests as FORTIQ_IT_FINGERPRINT_B.
set -e
KEYS=${1:?authorized_keys to install for the new user}
PORT=${2:-2223}
NAME=${3:-fqhost-b}
USERB=fqother

[ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 1; }
hostname_now=$(cat /etc/hostname 2>/dev/null || true)

# Own host keys: the copy must not present the first fixture's identity.
rm -f /etc/ssh/ssh_host_*
ssh-keygen -A >/dev/null
mkdir -p /etc/ssh/sshd_config.d
# Port: drop any earlier Port line so the new one is the only one.
sed -i '/^[[:space:]]*Port[[:space:]]/d' /etc/ssh/sshd_config
printf 'Port %s\n' "$PORT" > /etc/ssh/sshd_config.d/99-fortiq-b.conf

# The services of the first fixture must not run here (same network, same ports): only sshd.
for unit in postgresql dovecot fail2ban; do systemctl disable --now "$unit" >/dev/null 2>&1 || true; done
pkill -f "python3 /tmp/smtp_sink.py" 2>/dev/null || true
pkill -f "python3 /tmp/web_fixture.py" 2>/dev/null || true

# A different user with different files, so that data of A and B are never confused.
id "$USERB" >/dev/null 2>&1 || useradd -m -s /bin/bash "$USERB"
install -d -m 700 -o "$USERB" -g "$USERB" "/home/$USERB/.ssh"
install -m 600 -o "$USERB" -g "$USERB" "$KEYS" "/home/$USERB/.ssh/authorized_keys"
rm -rf "/home/$USERB/b-only" "/home/$USERB/host-b.marker"
install -d -m 755 -o "$USERB" -g "$USERB" "/home/$USERB/b-only"
printf 'HOST-B\n' | install -m 644 -o "$USERB" -g "$USERB" /dev/stdin "/home/$USERB/host-b.marker"
printf 'only on B\n' | install -m 644 -o "$USERB" -g "$USERB" /dev/stdin "/home/$USERB/b-only/note-b.txt"

# Host name (read by WSL at the next start).
if grep -q '^\[network\]' /etc/wsl.conf 2>/dev/null; then
    sed -i '/^hostname[[:space:]]*=/d' /etc/wsl.conf
    sed -i "/^\[network\]/a hostname=$NAME" /etc/wsl.conf
else
    printf '\n[network]\nhostname=%s\n' "$NAME" >> /etc/wsl.conf
fi

systemctl restart ssh 2>/dev/null || systemctl restart sshd 2>/dev/null || /usr/sbin/sshd
sleep 1
ss -ltn | grep -q ":$PORT " || { echo "sshd is not listening on $PORT" >&2; exit 1; }
echo "previous host name: $hostname_now; new: $NAME (after restart)"
for f in /etc/ssh/ssh_host_*_key.pub; do ssh-keygen -l -f "$f"; done
echo ready
