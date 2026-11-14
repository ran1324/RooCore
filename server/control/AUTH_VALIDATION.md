# Authentication, mail and publication validation — 2026-11-14

## Deployed artifacts

- Native application: `/opt/roocore/releases/20261114-auth-mail-v2`, active via
  `/opt/roocore/current`. Only RooCore was restarted; no host reboot or broker
  restart. The additive migration preserved existing telemetry and initialized
  native users/MFA state. The owner's existing password was not changed.
- Consistent pre-migration SQLite backup:
  `/var/backups/roocore/20261114T141153163735Z.sqlite3`, integrity check passed.
- Independent mail/health component:
  `/opt/roocore-mail/releases/20261114-mail-html-v2`; the existing enabled timer
  now uses the same HTML/plain-text layout for alerts. Its real health check passed.
- `auth_secret` and `mailgun_sending_key`: `roocore:roocore`, mode **0600**,
  outside the checkout. Root emergency recovery helper installed.

## Automated and browser checks

- Mac: **44 passed, 10 skipped**. Skips are real-broker tests because native
  Mosquitto is absent locally. Both owning navigation-contract parity tests passed.
- Linux release: **52 passed, 2 skipped**. Real isolated MQTT broker/ACL tests
  passed; the two parity checks skip because the standalone release excludes
  the owning navigation sources. `pip check` passed.
- Tests include password-only denial, cookie-bound codes, five-guess persistence,
  expiry, resend throttling and revocation, provider failure with no session,
  reset revoking pending codes/sessions, disabled users, concurrent one-use
  consumption, legacy-session revocation, Origin/CSRF checks and Host-safe redirects.
- Real local browser: password → email-code form → authenticated console;
  forgot password → emailed test link → new password → fresh code prompt.
  Test credentials/mailbox were isolated from production. No bypass flag exists.
- Hebrew forms and email previews were visually checked at 390px. The final
  code form measured 390px document width with a 390px viewport, without
  horizontal overflow. Paste of a grouped eight-digit code worked.

## Real provider and public checks

- The production login endpoint accepted the existing owner password and issued
  only the pending challenge cookie; anonymous data access remained HTTP 401.
  Its real email arrived in Gmail INBOX as multipart HTML/plain text with
  DMARC passing. No code or cookie was printed or committed.
- A separate temporary database on the server exercised the real Mailgun
  transport, code verification, session creation and replay denial. It was removed.
  Message-ID: `<20261114141559.2cd9b33840c2477f@saymour.co.il>`; Gmail INBOX confirmed.
- After the coordinated Access/Tunnel cutover, a legitimate fresh challenge
  was minted through the same `Accounts.login` password-verification service,
  sent through real Mailgun, and kept only in the private test process.
  **Public HTTPS** `/api/auth/verify` created a session; `/api/me` and status
  worked; CSRF logout returned 200; code replay and the logged-out session
  returned 401. Message-ID: `<20261114141835.6ac956254283528a@saymour.co.il>`;
  Gmail INBOX confirmed. No owner password change or extra production account.
- Public `/login`: 200; anonymous `/api/me`, `/api/users` and robot maps: 401.
  A real Chromium browser displayed the native login without an Access loop.
- A new styled connection-test message was accepted as
  `<20261114141605.4d4fff1d9c8d0930@saymour.co.il>`.
- A real public forgot-password request sent the styled recovery message to
  the owner's Gmail INBOX at **16:20:07 Israel time**. That production link was
  not consumed, so the owner's password remained unchanged; full reset
  consumption was exercised in the isolated browser/API tests.

## Secrets and dependency review

- **Gitleaks 8.30.1**, official release checksum verified: all **26 existing
  commits across all local refs** scanned, no findings. This is a non-shallow
  checkout fetched from the remote before inspection.
- The new server directory and an exported snapshot of the staged task files
  were separately scanned with redacted reports: no findings.
- On the server, seven actual private credential values were compared with
  the exact staged file contents without disclosing those values: **zero matches**.
- Runtime secret/database paths are ignored. The staged file list was reviewed;
  no password files, Mailgun keys, SSH identities, cookies, reset links, real
  OTPs, databases, local build output or unrelated firmware changes were staged.
- Independent `pip-audit` of the **24 locked dependencies** reported zero known
  vulnerabilities and zero skipped packages. Known-CVE scanning is not a proof
  that all application behavior is secure.
- GitHub CI runs server/broker tests and Gitleaks on this source. Publication
  status and final check results are reported with the PR, not inferred here.

Historical deployment records describe their original releases. This record
supersedes their Basic-auth/Cloudflare-Access and unimplemented-email-auth claims.
No hardware was contacted, flashed or operated during this work.
