# Integrated LiDAR power sequencing

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
