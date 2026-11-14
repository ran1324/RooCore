# Mail deployment evidence — 2026-11-14

- Independent release: `/opt/roocore-mail/releases/20261114-mailgun-v1`;
  active through `/opt/roocore-mail/current`.
- Seven isolated transport/monitor tests passed both on the owner's Mac and
  the server's CPython 3.13 runtime. Tests cover UTF-8 and TLS/tracking request
  settings, recipient/header rejection, key permissions/symlinks, sanitized
  failures, non-retry of ambiguous acceptance, provider-response validation,
  incident debouncing, persisted state, cooldown and failed recovery retry.
- `systemd-analyze verify` passed for the monitor service and timer.
- The actual host health probe returned no issues. RooCore remained active and
  its readiness endpoint returned HTTP 200 with `status: ready`.
- A dedicated **Domain Sending Key** named
  `RooCore transactional mail - Linux server` was created for `saymour.co.il`
  after the owner's explicit confirmation. The value is in the private server
  secret file, not source code or this record.
- The explicit smoke test ran as the unprivileged `roocore` account. Mailgun
  accepted message `<20261114134855.12bc573afe21b64d@saymour.co.il>` at
  **15:48:55 Israel time** (13:48:55 UTC).
- The Gmail connector found that exact Message-ID in `thfcxv@gmail.com`, labelled
  **INBOX**, from `RooCore <no-reply@saymour.co.il>`. Google's received headers
  report **SPF pass, DKIM pass for saymour.co.il, and DMARC pass**. The message
  arrived over TLS. Mailgun's received headers confirm that open/click tracking
  was disabled and TLS was required.
- `roocore-mail-monitor.service` completed a real healthy check, and
  `roocore-mail-monitor.timer` was enabled and started at **15:48:56 Israel time**.
  Incident/recovery logic was tested with fake sends; no production service was
  stopped or disk fault induced to generate a real incident alert.

Only the mail component was installed: the existing RooCore web release was
not replaced or restarted. No authentication password changed, no reset/OTP
endpoint was added, and no robot was contacted. Password recovery and
in-application 2FA remain separate product work. Current Cloudflare Access
codes are still sent by Cloudflare, not by the RooCore Mailgun transport.
