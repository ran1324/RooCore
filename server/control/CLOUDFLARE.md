# Cloudflare deployment — 2026-11-14

`roocore.saymour.co.il` is published by the `roocore-web` Cloudflare Tunnel to
`http://127.0.0.1:8780`; the fallback ingress returns 404. API and MQTT listeners
remain on loopback. No MQTT route, public device socket or router port was added.

The original owner-only Cloudflare Access application and its tunnel-side
Access JWT requirement were removed together **after native password + email
verification was deployed and source readiness checked**. The reusable owner
policy was left unchanged. The tunnel remains authenticated using its private
systemd credential; removing the redundant web login did not expose an
unauthenticated application API.

Zone settings: Always Use HTTPS **on**, minimum TLS **1.2**, SSL mode **strict**.
The application also redirects `X-Forwarded-Proto: http` to the fixed public
HTTPS origin, never to a request-supplied Host. Native cookies are Secure,
HttpOnly and host-only. CSP remains self-only; analytics beacon failures are
not a reason to permit third-party scripts.

Public checks after cutover: HTTP `/` redirects to HTTPS; HTTPS `/` redirects
to `/login`; `/login` returns 200. Anonymous `/api/me`, `/api/users` and robot
map API return 401, without a Basic authentication challenge. A public native
email-code verification created an owner session; status, CSRF logout, revoked
session rejection and one-use code rejection were verified. A real browser
also displayed the native login page without a redirect loop.

See [AUTHENTICATION.md](AUTHENTICATION.md) and
[AUTH_VALIDATION.md](AUTH_VALIDATION.md) for the tested authentication boundary.
