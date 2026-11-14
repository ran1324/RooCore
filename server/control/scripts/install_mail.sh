#!/bin/sh
# Independent mail-only release: preserves the running RooCore application and credentials.
set -eu
release=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case "$release" in /opt/roocore-mail/releases/*) ;; *) echo 'Upload under /opt/roocore-mail/releases first' >&2; exit 1;; esac
test "$(id -u)" = 0
python=/opt/roocore-runtime/python/bin/python3
(cd "$release" && "$python" -m unittest discover -s tests -p test_mail.py)
chown -R root:root "$release"
chmod -R a+rX "$release"
install -d -m 0755 /etc/roocore
if ! test -e /etc/roocore/mail.json; then
    install -o root -g roocore -m 0640 "$release/mail.example.json" /etc/roocore/mail.json
fi
install -m 0644 "$release/roocore-mail-monitor.service" /etc/systemd/system/roocore-mail-monitor.service
install -m 0644 "$release/roocore-mail-monitor.timer" /etc/systemd/system/roocore-mail-monitor.timer
systemd-analyze verify /etc/systemd/system/roocore-mail-monitor.service /etc/systemd/system/roocore-mail-monitor.timer
ln -s "$release" /opt/roocore-mail/current.next
mv -Tf /opt/roocore-mail/current.next /opt/roocore-mail/current
systemctl daemon-reload
echo 'Mail transport installed. Provision a domain-only Mailgun sending key before testing/enabling the timer.'
