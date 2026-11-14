#!/bin/sh
# Deploy an already uploaded release; no host reboot, broker restart or data deletion.
set -eu
release=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case "$release" in /opt/roocore/releases/*) ;; *) echo 'Upload under /opt/roocore/releases first' >&2; exit 1;; esac
test "$(id -u)" = 0
python=/opt/roocore-runtime/python/bin/python3
"$python" -m venv "$release/venv"
"$release/venv/bin/pip" install -r "$release/requirements.lock"
(cd "$release" && "$release/venv/bin/python" -m pytest -q -p no:cacheprovider tests)
"$release/venv/bin/pip" check
# Source may arrive from a removable macOS volume with owner-only modes.
chmod -R a+rX "$release"
chown -R root:root "$release"
previous=$(readlink -f /opt/roocore/current || true)
install -m 0644 "$release/roocore.service" /etc/systemd/system/roocore.service
install -m 0644 "$release/roocore-backup.service" /etc/systemd/system/roocore-backup.service
install -m 0644 "$release/roocore-backup.timer" /etc/systemd/system/roocore-backup.timer
install -d -m 0700 /var/backups/roocore
if test -f /var/lib/roocore/roocore.sqlite3; then "$python" "$release/scripts/backup.py"; fi
"$python" "$release/scripts/provision_auth_secret.py"
install -o root -g root -m 0700 "$release/scripts/reset_admin_password.py" /usr/local/sbin/roocore-reset-admin-password
ln -s "$release" /opt/roocore/current.next
mv -Tf /opt/roocore/current.next /opt/roocore/current
systemctl daemon-reload
systemctl enable roocore roocore-backup.timer
systemctl restart roocore
systemctl start roocore-backup.timer
for attempt in $(seq 1 20); do
  if curl -fsS http://127.0.0.1:8780/health/ready >/dev/null; then
    echo "RooCore ready: $release"
    exit 0
  fi
  sleep 1
done
echo 'New release failed readiness; restoring previous code if available' >&2
if test -n "$previous" && test -d "$previous" && test "$previous" != /opt/roocore/current; then
  ln -s "$previous" /opt/roocore/current.rollback
  mv -Tf /opt/roocore/current.rollback /opt/roocore/current
  systemctl restart roocore
else
  systemctl stop roocore
fi
exit 1
