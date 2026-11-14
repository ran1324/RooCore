# XIAO outbound cloud stream and offline map policy

Target: existing XIAO ESP32S3 / Arduino-ESP32 3.3.12. This is a source/build and
server integration record. No firmware was flashed and no robot was operated.

## Data path

The firmware has a separate core-0, priority-1 HTTPS task. The existing control
task stays on core 1 at priority 3. It copies the existing published snapshot
under a short mutex and serializes outside the control task. The control-side
capture uses a zero-wait mutex. There is one pending policy slot and no telemetry
backlog. A blocked DNS/TLS/server request cannot wait on a control-owned queue.

The fixed destination is `https://roocore.saymour.co.il/api/device/v1/snapshot`.
The client validates the hostname and the public Google GTS Root R4 CA; no
`setInsecure`, certificate skip, redirect or arbitrary destination is supported.
Time is synchronized through SNTP before TLS use. CA renewal/issuer changes may
require a trust-anchor update; authentication failure leaves local policy intact.
The CA is public, not a secret. HTTP and browser sessions are not device auth.

Each robot has an independent random device token. Only its hash is in SQLite;
revocation/rotation affects only that identity. Provisioning uses the root-only
`server/control/scripts/provision_device.py`. The token cannot read user APIs,
change accounts, deploy a map policy or operate a robot. User authentication and
email verification remain mandatory for the management console.

Initial software transport budgets are 1 second between successful latest-state
uploads and 10 seconds between full map/coverage uploads. These are not measured
sensor rates or controller deadlines. Every state includes 180 robot-relative
LiDAR bins, each valid OI sample available in packets 7–58 with acquisition time
and generation, safety validity flags, pose and map identity. Missing values are
omitted/null, never fabricated as zero measurements. This is a sampled/coalesced
stream, not a lossless recording of every OI frame or LiDAR revolution.

A full map is at most 16,384 cells; grid values remain -1 free, 0 unknown, 1
occupied. Main/side/excluded coverage arrays share the same frame. The firmware
bounds JSON to 300,000 bytes; its smaller snapshot reserve is 36,000 bytes. With
one full and nine small snapshots per ten seconds, these budgets imply a steady-frame ceiling around 62.4 kB/s before HTTP/TLS
overhead. Startup, retries or a changed frame can require additional full uploads;
the absolute configured ceiling is 300 kB/s at the one-second minimum interval. Actual payload size, Wi-Fi power,
CPU time, heap and control-loop jitter still need measurement on the board.
Backoff grows to 60 seconds after failures, without blocking local cleaning.

The server checks device credentials before decoding JSON. Public authentication
bodies are capped at 16 KiB; authenticated upload bodies at 1 MiB. It rejects
malformed data, old sequences, retired boots, stale snapshots,
wrong geometry and excessive rates. It stores the latest sensor state, persistent
occupancy/coverage maps and boot cursors. It does not archive every raw frame.
Historical recording/processing pipelines can consume this authenticated stream;
no cloud SLAM or new motor-command dispatcher is claimed by this change.

## Local policy and acknowledgement

Only a map actually observed from that device can be selected for deployment.
The owner/editor saves the map revision, then explicitly sends its zones. The
response is a bounded `RCZ1` envelope: cloud-map ID, source-map ID, geometry,
monotonic generation, revision and up to 32 zones. TLS authenticates it; the FNV
checksum detects corruption and is not a cryptographic signature. Python and C++
share a checked binary fixture. A lower/equal generation cannot replace policy.

The control task applies a candidate only while idle, paused or complete, in the
primary frame. It also requires fresh qualified encoders reporting unchanged wheel
counts over the existing two-second software hold window, no commanded cleaning,
fresh safe sensors and battery voltage above the qualified flash-save floor. This
is an observation gate, not independent proof of physical standstill. It rejects geometry/frame mismatch and an exclusion overlapping
a localized robot or anchored dock clearance. It stops, writes a sticky required-
policy intent, stores the policy and carpet-pass coverage in both alternating
mission checkpoints and reads back a verified checkpoint before acknowledgement.
No receipt, restart, policy application or network reconnect starts a mission.
A changed map remains pending in the console until the exact generation, revision
and checksum are reported by the controller. Acknowledgement is a device report,
not independent physical verification.

`ROO_CLOUD_REQUIRE_POLICY` is mandatory when cloud integration is enabled. A
missing, corrupt or stale checkpoint/intent holds motion instead of dropping
barriers. Checkpoint format 6 cannot restore format-5 missions; first setup needs
a new explicit, physically anchored mission. Formatting/map-clear and native
recording mode are blocked while the sticky policy requirement is in force.
Stock native Seek Dock/fallback is blocked when map barriers exist, because that
controller cannot enforce this map. This may hold at the dock approach pending a
separately qualified bounded docking path; it must not silently waive exclusions.
Physical stock Clean-button operation, power-off or a disconnected harness is not
covered by the add-on's planner; do not claim those paths enforce cloud zones.

## Internet loss

The active zone set and map frame stay in local memory and LittleFS. Healthy local
navigation continues during WAN/server loss using that accepted policy; network
availability is not a prerequisite in `healthy()`. Existing cliff, wheel-drop,
overcurrent, localization, docking, battery and command-expiry checks remain.
Loss of valid local localization/sensors or required policy causes a hold.
Restart restores paused and requires physical reanchor/explicit resume, including
when no Internet is available. A new server restriction is pending until received
and durably applied; an offline robot cannot know a restriction edited remotely.

## Configuration and commissioning

Copy the example to the ignored `config.local.h`, supply private Wi-Fi credentials,
a separate LAN API token and the device-only cloud token, and enable cloud mode.
All physical commissioning/calibration flags stay false until their owning tests
are performed. No USB/serial port is opened by the software checks in this task.
The checkpoint scratch area now resides in PSRAM to preserve internal heap for
network/control stacks. PSRAM/TLS allocation failure must never remove the policy
gate. Do not store device secrets in the portable guide or Git.

Before real operation: inspect the actual target and electrical assembly; test
sensor validity, policy persistence across restart and a torn write; measure the
control-loop timing with stalled DNS/TLS/upload and reconnect storms; unplug only
the Internet path during a qualified local mission and confirm zone sweeps and
fault stops independently. Test a pending exclusion, wrong map ID, corrupt policy,
low voltage and native-dock rejection. No physical pass is claimed here.

Sources: [Arduino-ESP32 3.3.12 HTTPClient](https://github.com/espressif/arduino-esp32/blob/3.3.12/libraries/HTTPClient/src/HTTPClient.h),
[NetworkClientSecure](https://github.com/espressif/arduino-esp32/blob/3.3.12/libraries/NetworkClientSecure/src/NetworkClientSecure.h),
[GTS Root R4](https://pki.goog/repo/certs/gtsr4.pem),
[ESP32-S3 flash concurrency](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/spi_flash/spi_flash_concurrency.html).
Flash writes can suspend other tasks; policy writes are confined to an already
stopped controller, rather than assumed safe merely because they use another core.
