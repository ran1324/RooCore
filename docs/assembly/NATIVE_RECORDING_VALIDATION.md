# Native recorder / OTA integration validation — 2026-11-14

Integrated into the primary XIAO checkout; existing local power/navigation changes preserved. No firmware was flashed, no robot was operated and no physical measurements/OTA transactions were performed.

| Check | Result |
|---|---|
| C++ native suite (`sh tests/run.sh`) | Passed with ASan/UBSan, including atomic base/extended frame capture, corrupt-frame rejection, ring overwrite, passive-only UART writes and stationary-update gate |
| Python suite | 108 tests passed in an isolated Python environment with numpy; includes capture start/stop/drain, existing-session fetch, identity separation, missing values, energy/sign crossings, loss/time rollover, redirect credential protection and image manifest checks |
| Arduino-ESP32 3.3.12 / Arduino CLI 1.5.1 | BatteryMonitor, LinkSetup, default RooCoreXiao and synthetic qualified recorder/OTA paths compiled successfully with `--warnings all` |
| Default full firmware | 587419 bytes program / 154804 bytes static RAM |
| Synthetic qualified fixture | 619071 bytes program / 156876 bytes static RAM |
| Target / partition layout | XIAO ESP32S3, OPI PSRAM, 8MB flash, default_8MB; image header chip ID 9; app slot 0x330000 bytes |
| Physical validation | Not performed: UART compatibility, extended sensors, power readings, native Clean equivalence, stop behavior, Wi-Fi/OTA, low-power interruption and recovery remain unverified |

The initial system Python run could not import numpy for the existing relocalization tests. The full suite was rerun successfully in `/tmp/roocore-native-python`; the collector itself uses only Python's standard library. The full run includes the final collector tests.

[Build log](sources/native-recorder-build.txt) · [C++ log](sources/native-recorder-host-tests.txt) · [Python log](sources/native-recorder-python-tests.txt) · [Source digests and build metadata](firmware-integration.json).

The enabled compiler fixture uses synthetic power thresholds, credentials and calibration flags and must never be flashed. OTA is off in the delivered example configuration. Firmware updates are password-authenticated application transfers on a trusted LAN; no signed-image or post-boot automatic rollback assurance is claimed. A successful reboot report is not a remote binary-digest check or robot functional validation.

Observation mode suspends RooCore's navigation/power-policy actuator decisions and retains native iRobot control. Native protections and the independent external hardware cutoff retain their respective roles; no software timeout can guarantee stopping after total MCU/UART failure. Recording stop/disconnect does not stop native cleaning. The RAM history is finite and power loss clears it; the computer's files are the persistent capture.
