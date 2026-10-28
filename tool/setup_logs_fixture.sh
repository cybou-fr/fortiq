#!/bin/sh
# Prepares the disposable fixture (WSL distro `fortiq-test`) for the live Logs tests: three `user.err` journal lines, a plain log file
# with two lines, and a failing user service that restarts and then gives up. Run it as root right before the live run (the tests look
# at the last 15 minutes) and pass the printed values to tool/run_live_tests.py (--log-tag, --crash-unit, --upload-dir).
# Usage: setup_logs_fixture.sh <unprivileged-user>
# The user must NOT be in adm or systemd-journal (the tests check the "limited access" notice).
set -eu
U="${1:?user}"
UID_="$(id -u "$U")"
if id -nG "$U" | tr ' ' '\n' | grep -qx -e adm -e systemd-journal; then echo "$U is in adm/systemd-journal: the limited-access tests would fail" >&2; exit 2; fi
loginctl enable-linger "$U"
systemctl start "user@$UID_.service"
for i in 1 2 3 4 5 6 7 8 9 10; do [ -S "/run/user/$UID_/bus" ] && break; sleep 1; done
TAG="fqlog$(date +%s)"
UNIT="fqcrash$(date +%s).service"
HOME_DIR="$(getent passwd "$U" | cut -d: -f6)"
DIR="$HOME_DIR/it"
install -d -o "$U" -g "$U" "$DIR"
printf 'first line\nsecond line\n' > "$DIR/app.log"
chown "$U:$U" "$DIR/app.log"
[ -e /var/log/fail2ban.log ] || { install -m 640 -o root -g adm /dev/null /var/log/fail2ban.log; echo "2026-10-24 fail2ban fixture line" >> /var/log/fail2ban.log; }
for ms in 5 12 31; do runuser -u "$U" -- logger -p user.err -t fqfixture "$TAG failed after $ms ms"; done
runuser -u "$U" -- env XDG_RUNTIME_DIR="/run/user/$UID_" systemd-run --user --quiet --unit="${UNIT%.service}" \
  -p SyslogIdentifier="$UNIT" -p Restart=on-failure -p RestartSec=1 -p StartLimitIntervalSec=120 -p StartLimitBurst=3 \
  sh -c "echo '<3>$TAG boom'; exit 1"
# A very short-lived writer may lose _SYSTEMD_USER_UNIT before journald resolves its PID.
# Keep the stream identifier explicit so every crash line still names this real unit.
# restarts every second: wait until the start limit is reached (three restarts, then "failed" for good)
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do
  state="$(runuser -u "$U" -- env XDG_RUNTIME_DIR="/run/user/$UID_" systemctl --user is-failed "$UNIT" 2>/dev/null || true)"
  [ "$state" = failed ] && break; sleep 1
done
sleep 2
echo "--log-tag $TAG --crash-unit $UNIT --upload-dir $DIR"
