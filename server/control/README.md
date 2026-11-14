# RooCore Server

Native Linux services for Roomba 620 / plain XIAO ESP32S3. **Telemetry only; no hardware commands.**
The user selected a normal installation instead of Docker on 2026-11-13.

## What runs

- `roocore.service`: Python FastAPI, authenticated Hebrew management UI, receive-only MQTT client.
- `mosquitto.service`: authenticated MQTT broker, identity-specific topic permissions.
- SQLite database in `/var/lib/roocore/roocore.sqlite3`, WAL mode and full synchronous writes.
- `roocore-backup.timer`: daily consistent SQLite backup, integrity check, 14 local copies.

The API listens on **127.0.0.1:8780**, MQTT on **127.0.0.1:18883**. Neither is public.
The console is published through Cloudflare Tunnel at `https://roocore.saymour.co.il/`.
Native accounts require a password followed by a one-use email code. The origin
remains private. Never change
these bind addresses to `0.0.0.0` without first deploying verified TLS and reviewing authentication.
There are no containers and no need to shut down the Linux host during application updates.

## Layout and operations

| Path / service | Purpose |
|---|---|
| `/opt/roocore/releases/<release>` | Immutable application code and its Python environment |
| `/opt/roocore/current` | Active release symlink |
| `/opt/roocore-runtime/python` | Separate Python runtime; does not replace Ubuntu's Python |
| `/etc/roocore/secrets` | Administrator and receive-only MQTT credentials; service account only |
| `/root/roocore-access` | Device and simulator provisioning passwords; root only |
| `/etc/mosquitto/roocore-passwd` | Hashed broker passwords |
| `/var/lib/roocore` | Database, owned by dedicated unprivileged account |
| `/var/backups/roocore` | Local backups, root only; not an off-host backup |

Run on the server as its administrator:

```sh
systemctl status roocore mosquitto
journalctl -u roocore -n 50 --no-pager
systemctl restart roocore
systemctl start roocore-backup.service
```

For an update, upload this directory to a **new** `/opt/roocore/releases/<release>`, then run:

```sh
sh /opt/roocore/releases/<release>/scripts/deploy.sh
```

The script installs locked dependencies, tests the release (including an isolated real MQTT
broker), verifies dependencies, backs up the live database, switches the active release,
restarts **RooCore only**, and checks readiness. It restores the previous code on readiness
failure. It does not restart Mosquitto, reboot the host or erase the database. Account/map tables are additive. The MFA policy migration revokes pre-upgrade
password-only sessions once. Backups include the entire database; retain the
new tables on rollback. Restore an external access gate before rolling back to
a release without native MFA.

The source is not writable by the service account. Secrets and data survive release changes.
The Python runtime is a separately managed, SHA256-verified Astral CPython standalone build;
Mosquitto is a signed apt package from its project's Ubuntu PPA. Runtime and operating-system
security updates remain operator work; they are not performed by the application deploy script.

## Access and authentication

Open `https://roocore.saymour.co.il/` and sign in with your username and password.
Then enter the eight-digit code sent to your registered email. Password-only
requests never receive an application session. Email codes expire in ten minutes,
are single-use, allow five guesses and are bound to a private browser cookie.
The database stores HMAC verifiers using a separate private server secret.

Use “Forgot password” to receive a personal reset link. It expires in 30 minutes,
can be consumed once, and revokes sessions and pending login codes. The response
never discloses whether an address is registered. New passwords require 15–128
characters. Changed passwords are stored with scrypt; the old bootstrap file is
only imported when the account database is empty and is no longer the active
password store after that. There is no HTTP Basic fallback.

Only the owner can invite users and assign per-robot permissions. See
[ACCOUNTS_MAPS.md](ACCOUNTS_MAPS.md), [AUTHENTICATION.md](AUTHENTICATION.md),
[MAIL.md](MAIL.md), and [CLOUDFLARE.md](CLOUDFLARE.md).

Emergency owner recovery remains available to a root SSH operator:
`python3 /usr/local/sbin/roocore-reset-admin-password`. It changes the owner's
database password, revokes sessions, reset links and login challenges, and prints
the replacement once in that terminal. It does not disable email verification.

## Device contract and the SCI/OI gate

The **existing XIAO firmware does not implement this MQTT protocol yet**. This server does not
poll that firmware's HTTP endpoints, connect to the legacy TCP recorder, open a serial port,
or bridge to a robot. No firmware was flashed or hardware operated.

Two identities are provisioned, with separate credentials and data provenance:

| MQTT username | Publish permissions | Source |
|---|---|---|
| `roomba-620` | `roocore/v1/robots/roomba-620/{telemetry,availability}` | Hardware report (not independently verified) |
| `sim-620` | `roocore/v1/robots/sim-620/{telemetry,availability}` | Synthetic test |
| `api-reader` | No writes; reads the four explicit topics above | Server |

No account has command-topic permission. The API's command endpoint always returns **423**
after authentication; no environment variable, database row or incoming message enables it.
No command publisher or SCI encoder exists in this service. The management UI has no actuator
controls. Do not grant broad `#` ACLs to make an integration work.

Example telemetry (schema v1; timestamps below are illustrative and must be current):

```json
{"schema_version":1,"boot_id":"661573ec-71f2-4d5c-ad3e-2acc97241b09","seq":1,
 "sent_at":"2026-11-13T10:00:00Z","uptime_ms":5000,"battery_percent":72.0,
 "battery_mv":14500,"state":"held","firmware":"bench-1","local_safety":"unverified"}
```

Use MQTT QoS 1 without retained messages or offline sessions. A new boot uses a new UUID;
sequence and uptime increase within that boot. Synchronize the device's clock before publishing.
Use a non-retained Last Will of `offline` on the identity's availability topic. A graceful
disconnect publishes the same `offline` value. Availability never makes a device online;
only newly accepted telemetry can do that. Retained messages are disabled at the broker and
also rejected by the application.

The UI marks data stale after **15 seconds**, selected as three missed 5-second reporting
intervals for this server protocol. This is a display rule, **not a verified physical stopping
time**. Messages older than 15 seconds or more than 5 seconds in the future are rejected.
The future allowance is only a software clock-skew policy. Payloads over 3072 bytes, unknown
fields, invalid values, old sequences, retired boots and more than one telemetry sample per
second per identity are rejected. History is kept 30 days; sequence cursors are retained.
After broker loss or server restart, persisted data stays historical until a new sample arrives.

## Communication-loss safety boundary

This server can detect missing/stale data; it cannot stop a robot over a broken connection.
The current native pilot has no actuator path, so a restart or reconnect cannot replay motion.
Physical stopping still requires verified firmware and hardware.

The repository's existing `lib/RooNavigation/src/ControlArbiter.h` uses controller-local command
expiry, epoch/sequence validation and a stop transition. It can subsequently return to local
cleaning if its health/mission conditions allow it. This is **existing source behavior, not a
physical verification and not a guarantee that all cleaning stays stopped on server loss**.
This new MQTT service is not connected to that arbiter. Before enabling any commands, establish
the desired local loss policy, validate the actual Roomba 620 SCI/OI subset, clock/expiry handling,
stop latency, reconnect/reboot behavior, local sensor faults and physical commissioning.
Do not substitute Create 2 compatibility for evidence about this particular 620.

To reach this loopback-only broker from the future XIAO, provide an authenticated tunnel/VPN
gateway on its home network, or separately deploy TLS with a trusted server identity and
per-device credentials. No such home gateway or controller integration is claimed here.

## Test and recover

```sh
cd /opt/roocore/current
venv/bin/python -m pytest -q -p no:cacheprovider tests
venv/bin/python scripts/simulate.py
```

The simulator is locked to `sim-620`, marks itself offline when finished and never contacts
hardware. Isolated broker ACL tests use throwaway credentials and an ephemeral loopback port.
Backups use SQLite's online backup API (not a raw copy of a potentially active WAL database).
To restore: stop `roocore`, preserve the current database and its WAL/SHM files in a recovery
directory, install a verified backup as `roocore.sqlite3` owned by `roocore` with mode 0600,
then start the service and check readiness. Do not restore over a running database.

See [deployment evidence](DEPLOYMENT.md) for what was actually exercised on the server.

Sources consulted: [Mosquitto configuration and ACL](https://mosquitto.org/man/mosquitto-conf-5.html),
[official installation routes](https://mosquitto.org/download/),
[FastAPI authentication](https://fastapi.tiangolo.com/advanced/security/http-basic-auth/),
[SQLite WAL behavior](https://sqlite.org/wal.html). These are software references, not robot tests.

## Native firmware HTTPS channel

The additional `/api/device/v1/snapshot` endpoint uses a device-only credential;
it never accepts a web session as device authentication. Root provisioning:
`venv/bin/python scripts/provision_device.py roomba-620` (or `sim-620` for tests).
The generated token is written to `/root/roocore-access/<identity>-https-token`
without printing it; only its hash is stored in the application database. Use
`--rotate` to invalidate the old token. No home address or inbound home port is
needed. Keep the existing MQTT broker private; this channel does not open it.

The map editor can request a policy only for the current device-observed map.
Changes stay pending until the exact revision is reported as durably stored.
The native firmware holds on missing/corrupt required policy and cannot use
stock docking/fallback to bypass barriers. No cloud motor dispatcher was added.
Server streaming state/map data are included in daily SQLite backups. See
[firmware contract and physical verification limits](../../docs/CLOUD_STREAM.md).
