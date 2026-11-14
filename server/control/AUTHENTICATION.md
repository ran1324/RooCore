# RooCore authentication and email verification

The application requires a password and an eight-digit code sent to the account's
registered email on every new login. There is no option to disable the email step
and no HTTP Basic or password-only fallback. Email verification depends on the
mailbox's security and is not phishing-resistant; it is not a claim of NIST AAL2.

## Login and session lifecycle

1. Passwords are verified with the existing scrypt parameters (N=131072, r=8,
   p=1, random 128-bit salt). Hashing is serialized to bound peak memory.
2. A successful password check creates a pending challenge, not a session. A
   random 256-bit challenge token is held in a Secure, HttpOnly, host-only,
   SameSite=Lax `__Host-roocore-login` cookie. Only its SHA256 digest is stored.
3. An eight-digit CSPRNG code is sent with tracking off and TLS required. Only
   after Mailgun accepts the message is the challenge usable. A delivery error
   removes it and returns 503; it never authorizes a session.
4. The database stores an HMAC-SHA256 code verifier using the separate private
   `/etc/roocore/secrets/auth_secret`, never a plaintext code. Challenges expire
   after ten minutes and after five wrong guesses. Attempt counts are committed
   even on rejection. Successful consumption and session creation are atomic.
5. Resend requires the challenge cookie, rotates the code and token, invalidates
   the previous challenge and uses the same registered mailbox. It is limited
   to one code per minute and five requests per 15 minutes. Login attempts also
   have global and per-name limits, consistently for known and unknown names.
6. The authenticated cookie `__Host-roocore` holds a new random 256-bit token;
   the database stores its digest. Expiry is eight hours absolute and 30 minutes
   idle. Authenticated writes require a session-bound CSRF token as well as the
   exact public Origin. The frontend permits paste and password managers.

Challenges are bound to the current password hash and enabled account. Resetting
a password, changing access or disabling an account revokes challenges and
sessions. On first enabling the email policy, pre-upgrade password-only sessions
are revoked once. Normal restarts retain the policy and valid MFA sessions.

## Recovery and invitations

`/api/auth/forgot` always returns the same response for registered and unknown
addresses, with global and per-address throttles. Recovery tokens contain 256
random bits, are stored as SHA256 digests, expire after 30 minutes and can be
used once. Links use the fixed HTTPS public origin, never a supplied Host header.
Their token is in the URL fragment, then removed immediately from browser history;
it is not sent in the initial HTTP request or Referer. The new password is never
emailed. Recovery does not create a session: the next login still requires its
email code. A password-change notice is sent without including credentials.

Only the owner can invite accounts and assign robot permissions. Invitation
links use the same one-use password setup flow; first login also verifies email.
An enabled root recovery utility updates the owner hash and revokes sessions,
recovery links and challenges; it does not bypass email verification.

## Operations, deployment and rollback

`scripts/provision_auth_secret.py` creates the private OTP verifier secret once
as `roocore:roocore`, mode 0600, without printing it or replacing an existing one.
Production `Settings.from_env()` requires that secret. Tests supply fake settings,
fake mail and generated test codes; there is no production test switch.

The additive tables `email_login_challenges` and `auth_policy` join the account
tables. Back up SQLite before deployment. The older application ignores these
tables, but rolling back to it removes native MFA; restore an external access
gate before such a rollback. Do not drop account/MFA tables as a routine rollback.
The original telemetry and command-lock behavior is unchanged by authentication.

Web authentication grants access to the console only. A valid password/email
session is not device authentication, a physical commissioning result, an
approval for native fallback, dock-clearance qualification or permission to
change a hardware cutoff. Controller-local power, battery-learning, map-policy
and loss-of-network safeguards remain owned by their firmware and assembly
records. Account recovery must never reset those approvals or calibration flags.

Mailgun sends multipart HTML/plain-text templates with RTL layout and no remote
images or tracking. Never log OTPs, passwords, reset URLs, cookie values or API
keys. The service disables Uvicorn access logs. Keys remain outside the checkout;
the repository ignore rules also exclude runtime credentials and databases.

Sources: [OWASP password recovery](https://cheatsheetseries.owasp.org/cheatsheets/Forgot_Password_Cheat_Sheet.html),
[OWASP OTP and email-factor guidance](https://cheatsheetseries.owasp.org/cheatsheets/Multifactor_Authentication_Cheat_Sheet.html).
