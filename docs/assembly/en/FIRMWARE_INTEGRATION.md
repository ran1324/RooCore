# Integrated LiDAR power sequencing

## Revision 11 — HTTPS stream and local map policy

The firmware adds a separate network task for sensor/LiDAR/map upload, verified
TLS and a device-only key, map-bound zone validation and dual checkpoint policy
persistence (format 6), with readback before acknowledgement. Missing/stale
required policy holds motion. Healthy local navigation does not depend on WAN;
native docking/fallback is blocked with map barriers. Checkpoint scratch storage
now uses PSRAM. No new cloud actuator path is enabled. Public examples have no
secrets and hardware flags stay disabled. Nothing was flashed or physically
validated. See [the contract and test plan](../../CLOUD_STREAM.md).


## Revision 10 — battery monitoring and learning, 14 November 2026

`BatteryLearning.h` and `BatteryLearningStore.h` are integrated in the RooCoreXiao control task: current/energy integration, 32 cycle summaries and 256 traces, OI-gauge-referenced capacity, full-charge anchoring and return reserve. Alternating LittleFS snapshots use CRC/readback; writes require qualified stationary Passive power and at least five minutes between checkpoints. Authenticated read/pagination APIs and explicit boot/history-scoped reset are implemented. Unreliable localization, gaps or a missing anchor block reserve use. Learned return can only begin earlier; fallback approval and hardware EN remain independent.

All `tests/run.sh` checks passed with ASan/UBSan; the latest battery tests also ran separately after adding restored-model and return-frame cases. Coverage includes integration, sign crossings, gaps, clock wrap, invalid gauges, temperature domain, OI-dependent capacity, bounded reserve, detour/unreachable routes, preservation of the active path, ring retention and torn-write/reboot recovery. HTTP on a real controller and physical Flash writes were not tested.

Both XIAO builds passed `--warnings all` with no warnings in the log: **621247 bytes Flash / 172484 static RAM** for defaults; **653711 / 174556** for the synthetic qualification fixture. The fixture enables learning/storage branches for compilation only. BatteryMonitor and LinkSetup also built. Source defaults retain empty battery/calibration IDs, false verification/adaptive/storage flags and zero measured values. No flashing, wiring or hardware operation occurred.

[Behavior, limits and API](../../BATTERY_LEARNING.md) · [Host checks](../sources/battery-learning-host-tests.txt) · [Build](../sources/battery-learning-build.txt) · [Source hashes/results](../firmware-integration.json).


## Power management and explicit native handoff — 14 November 2026

`PowerManagement.h` is integrated in RooCoreXiao: 13.8V warning, 13.5V return after 5 seconds, 13.0V urgent return and 12.5V stop. A marker is saved before a mission; approval is boot/mission-scoped and also required at the final dock approach. Local safety requires a dock corridor qualified for the add-on height/envelope, fresh OI/pose/IR and no hazard. New height sensors are not integrated. D10 is unused; hardware EN remains independent.

Hardware activation stays disabled with false verification flags and zero measured fields. [Behavior and API](../../POWER_MANAGEMENT.md) · [Policy source snapshot](../firmware-source/PowerManagement.h). Current build sizes and results are in the validation record; older numbers below describe historical builds. No flashing or physical tests were performed.


## Brush geometry and navigation fix — 2026-11-12

Integrated a 160×70 mm rectangle, estimated −35 mm centre offset and 70 mm side radius. Removed the redundant start-cell recentering that caused repeated waits/skips near a furniture corner until `NoProgress`. Cleaning targets now place the brush centre over the floor sample instead of the axle. Clearance, time and retry limits remain in force. See [measurements, assumptions and failure analysis](ROBOT_GEOMETRY.md).

Both full-firmware configurations compiled with `--warnings all` and no warnings in the log: defaults 539463 bytes Flash / 151708 static RAM; enabled fixture 547895 / 151716. BatteryMonitor and LinkSetup also built. [Current build log](../sources/geometry-firmware-build.txt) · [C++ ASan/UBSan tests](../sources/geometry-native-tests.txt). The enabled configuration is synthetic only; nothing was flashed.

## Charging, dock protection and progress dashboard — revision 6

Firmware now has a local charge supervisor, active-time accounting excluding charging/pauses, explicit return causes, reusable occupancy and coherent `/progress` snapshots. Dock protection checks the swept body/side-brush envelope with qualified approach/departure corridors. IR is not a distance measurement. Unknown-frame motion stays held even if a provisional local map exists.

LiDAR preparation remains latched through warm-up. Invalid charger telemetry, waiting/reconditioning and faults cannot be overridden by a high percentage. The native charger owns charging. Automatic BRC wake is disabled without verified interface/behavior. New user configuration stays unqualified/off. No wire or pin mapping changed.

All 92 Python tests and C++ ASan/UBSan checks passed, including a three-hour simulated charging dwell, charge/resume, pause during departure and a moved dock. Full firmware compiled in both configurations below. The enabled compiler fixture uses synthetic dock dimensions and temperatures only.

[Dashboard](../../DASHBOARD.md) · [Charge/resume](../../CHARGE_RESUME_PLAN.md) · [Dock guard](../../DOCK_GUARD.md). No flashing, hardware operation or measurements were performed.

2026-11-10 · Revision 6. The sequence is part of the full `RooCoreXiao.ino` application, not just the OffByDefault example. No firmware has been flashed and no robot has been operated for this validation.

[Assembly guide](RooCore-assembly.html) · [Hebrew record](../FIRMWARE_INTEGRATION.md) · [Firmware source snapshot](../firmware-source/RooCoreXiao.ino)

The change was prepared on `codex/lidar-power-sequence`, based on `033b90050f3deb9c03d79670bc8a90fa2b6590de`. After PR #1 merged, the firmware and guides are available on `main` in the primary checkout. Continue editing `firmware/xiao/RooCoreXiao/RooCoreXiao.ino` there. Existing local changes were preserved during the update.

The source snapshot in the guide kit is for review, not a standalone flashable sketch. Build the application from a complete repository checkout with its RoombaOI and RooNavigation libraries.

## Map zones — additional integration verification

Rectangle areas and virtual walls were added to the shared planner and visual map editor. A complete simulation with an exclusion and carpet covered charging during an extra carpet pass, departure and resumption in that same pass with prior coverage retained, through completion without collisions. All 100 Python tests passed after integration, alongside C++ ASan/UBSan checks. Zone policy was included in both firmware builds. XIAO does not yet accept or acknowledge zone uploads; saving in robot mode stores annotations only and does not activate physical protection. Wiring and enablement settings are unchanged. [Behavior and limits](../../MAP_ZONES.md).

## Application behavior

- `setup()` calls `lidarPort.begin()` before networking or tasks: detach UART, drive D4 LOW, make D5 an input with pulldown, then drive D3 LOW.
- `lidarPower(true)` requests power without blocking. `serviceLidar(now)` waits for the configured settling interval, opens UART and sends `A5 60` once.
- `lidarPower(false)` detaches UART, cancels any remaining transmission and prepares both signal pins before lowering D3. Normal shutdown, charging policy and `FirmwarePowerPort::quiesce()` use this same path.
- UART initialization or scan-command failure leaves power off with a latched fault. Repeated requests do not repeatedly cycle power. Shutdown invalidates old receive data and scan validity.
- UART2 belongs to LiDAR; UART1 remains assigned to Roomba. **D4/GPIO5 → LiDAR RX; LiDAR TX → D5/GPIO6; D3/GPIO4 → S1 ON.** The former GPIO43/44 LiDAR mapping and GPIO7/D8/OE control have been removed.
- After setup, only the control task owns this interface. HTTP requests enter its existing command queue.

The defaults remain `ROO_LIDAR_POWER_VERIFIED=false` and `ROO_LIDAR_RISE_MS=0`. Integration is complete, but physical qualification has not supplied a measured settling interval. An older configuration missing these values also stays disabled. The old fixed 500ms startup delay has been removed.

`/status` reports `lidar_power_state` (`off/warming/ready/fault`), `lidar_power_configured`, `lidar_uart_ready` and `lidar_rise_ms`. UART readiness does not establish valid scan data; freshness checks remain separate.

## Software validation

`sh tests/run.sh` passed under ASan/UBSan: 707 OI checks, protocol inventory, battery cutoff, Dirt/Stasis, carpet and navigation checks, plus the new LiDAR sequence test. Its 100 modeled cycles include canceled startup, `millis()` wrap, writes refused while not ready, stale RX removal, busy-TX cancellation and partial UART initialization failure.

`sh tests/build_xiao.sh` passed with Arduino CLI 1.5.1 / Arduino-ESP32 3.3.12, using `--warnings all` with no warnings in the recorded log:

| Target | Flash bytes | Static RAM bytes |
|---|---:|---:|
| BatteryMonitor | 289795 | 23724 |
| LinkSetup | 291239 | 23740 |
| Full RooCoreXiao, defaults | 538031 | 151708 |
| Full RooCoreXiao, enabled test fixture | 546471 | 151716 |

The temporary enabled fixture uses synthetic calibration, including **25ms solely for the build fixture**. It is not a robot setting or a flashing instruction. Compilation does not validate electrical behavior.

[Build log](../sources/firmware-integration-build.txt) · [Source hashes](../firmware-integration.json) · [Portable source snapshot](../firmware-source/RooCoreXiao.ino) · [Configuration snapshot](../firmware-source/config.example.h).

## Hardware decisions preserved

- No Mini-DIN 7 cable/plug body is required. The user selected direct low-profile wires retained with adhesive so the original cover can close. Retain contact mapping, insulation and strain relief checks.
- The fuse is already purchased. Its holder belongs in series with the positive project branch, near its source and before XT30/U3. Final value and DC interrupt capability remain separate qualification questions.
- One NOYITO is planned for Roomba after board supply mapping. The second is spare; direct 3.3V LiDAR UART does not use it.

[Power/UART sequence and pending hardware tests](LIDAR_POWER_UART.md) · [Artifact validation](VALIDATION.md).


## Native-cleaning capture and wireless XIAO updates

RooCoreXiao now provides record-start/record-stop, computer-side OI sensor logging and energy estimates. Extended data require opt-in; internal PWM and separate vacuum current are not reported. Password-protected OTA requires stopped service hold, fresh telemetry and a measured voltage floor; it defaults off. Initial USB installation and physical qualification remain required. See NATIVE_RECORDING.md. [Commands and checks](NATIVE_RECORDING.md).


## Private Wi-Fi setup — 14 November 2026

Added protected AP/network selection, NVS commit/readback after connection and stopped BOOT re-entry. Internet loss never opens the AP or erases zones. A unique AP password is provisioned outside the repository; no secret in the template, no new component or wire. Software/compiler checks only; not flashed or physically tested. See `docs/WIFI_SETUP.md` at project root for conditions and limitations.
