# Native accounts and cloud home planning

The server adds SQLite tables for users, opaque sessions, recovery tokens, audit
records, maps and draft mission plans. Existing telemetry tables are preserved.
Back up the entire database before migration; the old application ignores these
additive tables but cannot manage native accounts. Do not delete them on rollback.

The first start with an empty users table imports the existing `admin` bootstrap
password as a scrypt hash (N=131072, r=8, p=1, random salt). Subsequent starts do
not re-import it. The old plaintext bootstrap file is not an active credential
store after initialization. Changing it no longer changes a user's password.
The root recovery utility updates the owner hash and revokes sessions/tokens.

The sole owner can create up to 30 named accounts. Per robot, grant `view`,
`control` (currently draft planning only), and `map_edit`. Both extra permissions
require `view`. New accounts receive a one-use activation link. There is no
public sign-up and no endpoint for family members to grant themselves access.
Changing access or disabling a user revokes that user's sessions and outstanding
recovery links. Robot data, maps, plans and commands enforce these permissions
on the server; hiding a UI control is not authorization.

Sessions are random 256-bit opaque tokens with only SHA256 digests stored in the
database. The cookie is Secure, HttpOnly, host-only, SameSite=Lax. Session limits:
eight hours absolute and 30 minutes idle. Writes require the fixed public Origin;
authenticated writes also require a session-bound CSRF header. Password changes
and recovery revoke all sessions. Authentication attempts have persisted global
and account rate limits; password hashing is serialized to bound peak memory.
Bodies are bounded to 1 MiB, including chunked uploads. No Basic auth fallback.

Maps use the existing `navigation/zones.py` rectangle/wall validation contract,
with a test for parity. Occupancy values remain 0 unknown, -1 observed free and
1 occupied. Source, source map identity, geometry, cloud identity and revision
are retained. Imported maps are unverified annotations. New planning canvases
are explicitly not measurements of the home. Saving uses revision checks,
with at most 20 maps per robot and 32 zones per map. Zones can be drawn or edited
by their coordinates. Cloud saves are included in SQLite backups.

Cleaning a room and going to a target create **draft plans only** bound to a map
revision. Nothing queues a hardware command, publishes MQTT, starts a drive or
activates map barriers. The existing command endpoint remains unconditionally
locked after permission checks. A future hardware path must independently validate
localization, current map identity/revision, robot footprint, exclusions and
controller-local safety, and receive an explicit policy acknowledgement.

The companion auth/mail work owns the final 2FA and styled recovery mail flow;
see its authentication validation document for the final deployment results.
