# Transactional email — saymour.co.il

Mailgun US is the outbound provider for RooCore system mail. The sending domain
`saymour.co.il` was verified on 2026-11-14: SPF verified, 2048-bit DKIM active,
both Mailgun MX records and `email.saymour.co.il` tracking CNAME verified.
DMARC is published as `v=DMARC1; p=none;` with no third-party report destinations.
No receiving route/mailbox is configured. MX records alone do not forward mail.

The mail-only release and monitor are installed on the Linux server, the dedicated
sending key is provisioned, and the timer is enabled. A real test message reached
the owner's Gmail inbox with SPF, DKIM and DMARC passing. See
[MAIL_VALIDATION.md](MAIL_VALIDATION.md) for the evidence and remaining scope.

## Transport and credential

`app/mailer.py` supplies `MailgunMailer.send(recipient, subject, text, category=...)`.
It uses the US HTTPS API, requires recipient-side TLS with certificate validation,
signs DKIM and disables open/click tracking, including for authentication mail.
Categories are `system`, `alert`, `auth`, and `test`. The sender defaults to
`RooCore <no-reply@saymour.co.il>` and operator alerts go to the existing owner's
`thfcxv@gmail.com`. Each call accepts one bare recipient, validates headers and
limits message size. There is no public send-email endpoint.

Use a **Domain Sending Key for saymour.co.il**, not an account-wide API key.
Store the value only in `/etc/roocore/secrets/mailgun_sending_key`, a regular
file owned by `roocore:roocore`, mode `0600`, inside the existing private secret
directory. Never put it in Git, command arguments, email, UI code, or logs.
Non-secret configuration is `/etc/roocore/mail.json` (`root:roocore`, `0640`).

The send result means **Mailgun accepted/queued the message**. Confirm delivery
through Mailgun Logs and the destination inbox. Network timeouts have ambiguous
acceptance; the transport does not retry automatically. Authentication now uses
this transport for login codes, recovery/activation links and password-change
notices. Every message includes an RTL HTML layout and a plain-text alternative;
tracking remains disabled. See [AUTHENTICATION.md](AUTHENTICATION.md).

## Independent health monitor

`roocore-mail-monitor.timer` checks every minute after a three-minute startup
delay. A separate process can report an application crash even when the web
application is unavailable. It checks loopback database/MQTT readiness, active
RooCore/Mosquitto/Cloudflare tunnel services, a failed database-backup service,
and free space on the data volume (under 2 GiB or 5%). It does not inspect robot
telemetry, operate hardware, or email logs/exception bodies.

An alert requires three consecutive failed checks. Persistent incidents repeat
at most hourly; send attempts are at least 15 minutes apart. One recovery mail
is sent after an accepted incident alert, subject to that same attempt cooldown.
Private state survives timer processes in `/var/lib/roocore-mail/monitor.json`.
No hardware-offline alarm is enabled before controller integration.

This host-local monitor cannot send during host power loss or complete network
loss. External uptime monitoring remains separate. Backup checks detect a failed
backup unit, not a backup job that never ran. Application exceptions that do not
affect readiness are not individually emailed by this initial monitor.

## Install, verify, enable

Upload the mail-only files to a new `/opt/roocore-mail/releases/<release>`:
`app/__init__.py`, `app/mailer.py`, `app/email_templates.py`, `scripts/mail_send.py`, `scripts/mail_monitor.py`,
`scripts/install_mail.sh`, `tests/test_mail.py`, both monitor units and
`mail.example.json`. Run `sh scripts/install_mail.sh` as root from that release.
This tests the isolated transport, validates the systemd units and installs an
independent `/opt/roocore-mail/current` link. It does not restart the web server,
replace its current release, change passwords or enable the monitor yet.

After securely provisioning the sending key:

```sh
cd /opt/roocore-mail/current
sudo -u roocore /opt/roocore-runtime/python/bin/python3 scripts/mail_send.py --test
systemctl start roocore-mail-monitor.service
systemctl enable --now roocore-mail-monitor.timer
systemctl status roocore-mail-monitor.timer --no-pager
journalctl -u roocore-mail-monitor.service -n 10 --no-pager
```

Tests use a fake provider and simulated incidents; no email or hardware activity.
To disable notifications: `systemctl disable --now roocore-mail-monitor.timer`.
Revoke the dedicated sending key in Mailgun if it is compromised. Existing
domain/application mail is unaffected by disabling this timer.

Sources: [Mailgun Domain Sending Keys](https://documentation.mailgun.com/docs/mailgun/user-manual/api-key-mgmt/rbac-mgmt),
[message API and TLS/tracking options](https://documentation.mailgun.com/docs/mailgun/api-reference/send/mailgun/messages).
