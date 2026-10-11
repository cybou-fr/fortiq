#!/bin/sh
# Provisions the throwaway Debian used by the integration tests (WSL distro
# "fortiq-test"). Run as root inside that distro:
#   wsl -d fortiq-test -u root -- sh /mnt/c/<path>/tool/test_server/provision.sh <pubkey-file>
#
# It installs only what a fresh VPS has (an SSH server and sudo). tmux, ufw,
# fail2ban, unattended-upgrades and iproute2 are left out on purpose: Fortiq's
# setup has to install them.
set -eu

PUBKEY_FILE="$1"
export DEBIAN_FRONTEND=noninteractive

if ! command -v systemctl >/dev/null 2>&1 || ! grep -q 'systemd=true' /etc/wsl.conf 2>/dev/null; then
  # The slim image has no init system; sshd, fail2ban and ufw need systemd.
  apt-get update -qq
  apt-get install -y -qq systemd systemd-sysv dbus > /dev/null
  printf '[boot]\nsystemd=true\n' > /etc/wsl.conf
  echo "systemd installed: restart the distro (wsl --terminate fortiq-test) and run again"
  exit 3
fi

apt-get update -qq
apt-get install -y -qq openssh-server sudo ca-certificates curl python3 procps > /dev/null

# Same port as the tests expect. Debian's sshd_config includes sshd_config.d.
mkdir -p /etc/ssh/sshd_config.d
printf 'Port 2222\n' > /etc/ssh/sshd_config.d/10-fortiq-test-port.conf

KEY="$(cat "$PUBKEY_FILE")"
cp "$PUBKEY_FILE" /root/fortiq-test.pub   # the tests restore this key between runs
add_user() {
  user="$1"
  id "$user" >/dev/null 2>&1 || useradd -m -s /bin/bash "$user"
  mkdir -p "/home/$user/.ssh"
  printf '%s\n' "$KEY" > "/home/$user/.ssh/authorized_keys"
  chown -R "$user:$user" "/home/$user/.ssh"
  chmod 700 "/home/$user/.ssh"
  chmod 600 "/home/$user/.ssh/authorized_keys"
}
add_user fquser
add_user fqplain
printf 'fquser ALL=(ALL) NOPASSWD:ALL\n' > /etc/sudoers.d/fquser
chmod 440 /etc/sudoers.d/fquser

mkdir -p /root/.ssh
printf '%s\n' "$KEY" > /root/.ssh/authorized_keys
chmod 700 /root/.ssh
chmod 600 /root/.ssh/authorized_keys

systemctl enable --now ssh
sleep 2
# iproute2 (ss) is deliberately not installed, so look at the port with sshd itself
if systemctl is-active --quiet ssh && /usr/sbin/sshd -T | grep -q '^port 2222'; then
  echo "sshd is running on port 2222"
else
  echo "sshd is not running correctly" >&2
  exit 1
fi
