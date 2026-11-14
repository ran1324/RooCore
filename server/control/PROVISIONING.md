# Runtime provisioning record — 2026-11-13

Host discovery was read-only first. The SSH connector had no usable agent credentials.
The existing encrypted local key and macOS Keychain route authenticated successfully with
strict verification against the already known host key. No keys, SSH passwords, authorized
keys or host-key trust records were created or weakened.

Initial host: Ubuntu 20.04.6, x86_64, kernel 5.4.0-216, 6 logical CPUs, 11 GiB RAM,
about 10 GiB available and 135 GiB free on root. MySQL was already bound to loopback.
Docker, Mosquitto and the RooCore services were absent. Existing application data and
services were preserved. Initial application provisioning did not replace the OS.
Subsequent authorized maintenance upgraded the host to **Ubuntu 24.04.5 LTS**,
kernel **6.8.0-149-generic**, with a verified reboot and root SSH recovery;
see [OS_UPGRADE.md](OS_UPGRADE.md). RooCore still uses its separate Python runtime.

The user changed the deployment preference to native services. A partially downloaded
Docker runtime was removed before any Docker daemon or container ran. No Docker service,
Compose deployment, container network or container volume was created.

## Installed software

- Mosquitto **2.1.2-0mosquitto1~noble1**, with matching `mosquitto-clients` and
  `libmosquitto1`, from the project's documented `mosquitto-dev/mosquitto-ppa`.
  This replaced the initially provisioned 2.0.22 Focal build after the OS upgrade.
- Signed apt source: `/etc/apt/sources.list.d/roocore-mosquitto.list` with a dedicated
  `signed-by=/etc/apt/keyrings/mosquitto.asc`. Signing fingerprint verified against Launchpad:
  `7B2FBD7C964368452615DB74E633D0D8216D1222`.
- CPython **3.13.18**, Astral standalone build **20261110**, x86_64 GNU/Linux install-only.
  Installed under `/opt/roocore-runtime/python`. Bundled SQLite **3.53.1**.
- Download:
  `https://github.com/astral-sh/python-build-standalone/releases/download/20261110/cpython-3.13.18%2B20261110-x86_64-unknown-linux-gnu-install_only.tar.gz`
- Download SHA256 was checked before extraction:
  `50c774f6674740739f791c05a9177ed03e2fe02abebda9bca3512eb7309b37cc`.
- FastAPI **0.162.5**, Uvicorn **0.54.0**, Paho MQTT **2.2.0**, SQLAlchemy **2.1.8**.
  The entire tested Python environment is pinned in `requirements.lock`.

`bootstrap.py` provisioned a dedicated `roocore` system account, random credentials and
narrow MQTT permissions. Password values never entered source files or command arguments.
Mosquitto hashes are owner-only; application credentials are service-account-only. Source
and Python environments are root-owned and not writable by the application account.

This remains a private pilot on the existing host. OS release upgrades and actual
reboots are recorded separately; routine application updates restart RooCore only.
Ongoing runtime/security maintenance, a scheduled off-host backup and a protected
controller network path remain operational work. A verified one-time off-host
service/database backup was taken before the OS upgrade.
