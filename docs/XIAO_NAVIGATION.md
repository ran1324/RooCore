# RooCore: real firmware and hybrid navigation

The executable firmware is `firmware/xiao/RooCoreXiao/RooCoreXiao.ino` for the **plain XIAO ESP32S3**, not the legacy ESP-01S sketch. It talks to the real Roomba OI UART and T-mini Plus UART. It has not been flashed or run on the physical robot.

## Responsibilities

| Layer | Runs on | Implementation |
| --- | --- | --- |
| Motor ownership, sensor checks, command expiry | XIAO | `ControlArbiter.h` and the firmware control task |
| Daily/deep cleaning, coverage, charging/resume, finite recovery | XIAO | `RooNavigation.h`, fixed-buffer C++11 |
| Encoder odometry and local scan-to-map alignment | XIAO | `Odometry`, `alignToMap`; limited local estimator, not global SLAM |
| T-mini Plus packet decoding | XIAO | `TminiPlus.h`, manufacturer protocol, model 151 |
| Global mapping/navigation with a computer | Linux / ROS 2 Jazzy | SLAM Toolbox + Nav2 through `ros2/roocore_bridge` |
| Browser behavior prototype | Mac/Linux | `navigation/`; the C++ Navigator via ctypes, with simulated ideal pose |
| Tests of actual embedded planning code | Mac/Linux/CI | `tests/navigation_native_test.cpp`; independent physics and obstacle geometry |

The browser executes the navigation library, not the complete ESP32 application; screenshots are not hardware validation. The native tests directly include the firmware's C++ navigation library. Neither simulator proves wheel calibration, localization performance or docking on a Roomba 620.

## Loss of server

1. Commands carry the current MCU session epoch, a strictly increasing sequence and an expiry in the **MCU clock**. Maximum accepted lifetime is 250 ms; the bridge requests 200 ms. Duplicate, out-of-order, wrong-session and expired commands cannot refresh the lease.
2. After expiry, remote commands become zero and the epoch changes. The robot is held stopped for at least 500 ms.
3. If local localization, battery, sensors and mission remain valid, local planning resumes. At 25% simulated/estimated charge it prefers returning to the dock. A known route is still required.
4. Critical invalid sensing leaves control held/stopped. A lost global pose or route can start an isolated local mission and request assistance, as described in [coverage and recovery](COVERAGE_AND_RECOVERY.md). A server reconnect cannot automatically re-arm remote ownership. A new explicit takeover synchronizes state and receives a new epoch.

The MCU updates its own map and coverage during remote driving. Nav2 receives the MCU coverage target in the `odom` frame. Only the arbiter selects motor output; there are never two writers to the Roomba UART. Motion also has a 300 ms OI library lease, checked by the control task. A total MCU/transport failure is **not** covered by a hardware motor watchdog: the lease is software, not a Roomba command timeout.

## Cleaning and finite decisions

- Daily: nominal 0.18 m/s and cleaning PWM 85, one pass. Deep: 0.12 m/s and PWM 127, two distinct passes. These are commissioning defaults, not measured suction/cleaning performance.
- Dirt packet 15 is observation-only. The former uncalibrated threshold-90 motor response has been removed. Timestamped capture, optional consecutive-reading detection, bounded advisory plans and passive revisit comparisons are described in [DIRT_DETECT.md](DIRT_DETECT.md). No automatic dirt cleaning is enabled or implemented before physical calibration.
- Coverage follows the oriented central brushes, with separate side-brush credit and two independent crosshatch passes. See [coverage and recovery](COVERAGE_AND_RECOVERY.md) for provisional geometry, room partitions, perimeter control and server assistance.
- A blocked target is excluded, rather than endlessly retried. Eight target failures without meaningful new brush coverage end the mission in a fault. Nearby goals can be retried once after observed obstacle clearance; each goal is excluded after two failures. Lack of progress toward a waypoint is limited to 8 seconds; a target is limited to 120 seconds; a mission to 2 hours. These conservative defaults may need adjustment after measurements.
- Cliff/virtual-wall, overcurrent, invalid pose and stale telemetry stop immediately. No `Full` mode is used.
- A wheel-drop or bumper event stops first. A drop does not establish that there is a cliff. If the event persists for 5 seconds, stop finally. If it clears for 1 second, a **single** retreat may be attempted only with valid rear LiDAR clearance **and** a recently traversed floor corridor. Retreat is at most 5 cm / 1.5 seconds at 0.04 m/s, brushes off. Another event or failed retreat ends the mission; no automatic repeated escape.
- Native docking is requested once, then monitored for actual dock contact and charging. Timeout: 60 seconds. Stopping native docking requires Safe takeover; sending zero wheel bytes in Passive alone is insufficient.
- Charge to 80%, then return toward the saved interruption point and resume the remaining coverage. Docking/undocking, charger behavior, battery thresholds and reliable pose on the actual base still require physical validation. Do not treat a successful simulated charge as proof.

## Map/localization limitations

The onboard map is 12.8 × 12.8 m centered on the initial dock anchor, at 10 cm navigation / 5 cm coverage-sample resolution, with robot footprint inflation. Cells outside the map cannot be driven into. Occupied cells block immediately. Reopening requires at least 20 successive fresh scans with free evidence covering four inner-cell probes, and 2.5 seconds without a hit. Bumper-only cells stay blocked unless laser returns corroborated the obstacle before clearance. It is a conservative local fallback, not whole-home loop-closure SLAM.

The local scan alignment uses a bounded search around wheel odometry. Low agreement stops navigation. It does not reliably identify every ambiguous room or a robot picked up and relocated. Dock re-anchoring after a reboot requires physical placement at the same recorded dock orientation. Power-loss recovery still requires a dock reanchor. During an active mission, the optional assistance worker can propose a global scan match; it is separately verified and may remain ambiguous. Never resume using the saved pose as if it were a new measurement.

The browser prototype's former furniture loop came from occupied cells flickering between free/occupied and changing the target. Conservative multi-scan clearance evidence and finite progress limits address that failure; the C++ implementation additionally excludes failed targets with a finite budget. See [moving obstacles](COVERAGE_AND_RECOVERY.md#moving-obstacles-and-contact-memory).

## Build and first hardware session

```sh
sh tests/run.sh
python3 -m unittest discover -s tests -v
sh tests/build_xiao.sh
```

Arduino ESP32 **3.3.12**, FQBN `esp32:esp32:XIAO_ESP32S3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB`. The build script compiles the OI examples and both the default telemetry-only and commissioned actuator paths. Nothing is flashed by these commands.

1. Copy `config.example.h` to the ignored `config.local.h`; set the existing Wi-Fi network and a random API token (at least 24 characters). Keep it on a trusted LAN. The current HTTP transport is not encrypted and must not be port-forwarded.
2. Configure and validate the measured `ROO_POWER_*` block in [power shutdown](POWER_SHUTDOWN.md); missing calibration holds motion and D10 LOW. Verify the agreed power wiring, UART levels, LiDAR power switch/OE polarity and BRC transistor first. D0/GPIO1 = Roomba TX, D1/GPIO2 = Roomba RX; D6/GPIO43 = LiDAR TX, D7/GPIO44 = LiDAR RX; D3/GPIO4 = LiDAR power, D8/GPIO7 = LiDAR OE, D9/GPIO8 = BRC transistor, D10/GPIO9 = power-off transistor (automatic sequenced cutoff after measured calibration; see [power shutdown](POWER_SHUTDOWN.md)). No USB with the external XIAO 5V feed connected in the no-diode build.
3. Use `BatteryMonitor` / `LinkSetup` to validate OI data, actual baud and BRC. Establish that encoder packets 43/44 work on this robot before setting `ROO_ENCODERS_VERIFIED=true`. Firmware compatibility has not been inferred from the model name.
4. Check T-mini model **151**, 230400 8N1, packet checksums, measured distances, raw scan direction, mounting yaw and X/Y offsets. Model **152 SH** uses a different payload and is not supported by this decoder. Invalid/partial scans do not authorize driving.
5. Calibrate meters per encoder tick and wheel separation. Verify the footprint, local scan alignment, stop latency, wheel direction and all safety inputs under controlled conditions before `ROO_COMMISSIONED=true`.
6. Measure/verify the level, clear corridor behind the dock before `ROO_DOCK_EXIT_VERIFIED=true`; set `ROO_DOCK_EXIT_METERS` (nominal 0.60 m). Exit runs backward at 0.06 m/s, with live rear clearance, a 3-second no-progress limit and 15-second overall limit. This is separately bounded from obstacle recovery.
7. Initialize the LittleFS mission store explicitly (`init-storage`). This formats its filesystem and deletes its old checkpoints; normal boot never formats automatically. Configure a physical dock anchor with `anchor`. Default builds refuse all motion commands.
8. Start a small supervised `daily` mission. Test stop, missing sensor frames, blocked path, lost server, return, real charging and departure in separate steps. Enable unattended use only after those measurements support it.

Authenticated API: `GET /status`, `POST /command` with URL-encoded `action`. Supported actions: `init-storage`, `anchor`, `daily`, `deep`, `pause`, `resume`, `home`, `local`, `remote`, `velocity`, `relocate`, and the frame-bound `help-continue`, `help-hold`, `help-relocalize` protocol. HTTP 202 means **queued**, not executed; inspect state/reason/error in `/status`. `anchor` requires a stopped robot with dock contact/charging. A resume may first need to warm the LiDAR; retry only after status shows valid observations. There is no public unauthenticated motor endpoint.

Checkpoints use two alternating slots with schema/version and integrity checks. Boot loads the newest valid compatible snapshot and stays paused; it never blindly drives from a persisted pose. A new mission intentionally clears prior coverage. File writes stop motor control first. The map, pass, coverage, skipped cells and interruption point survive; this does not prove persistence on the untested physical flash.

## Linux navigation

See [ROS 2 setup](ROS2_NAVIGATION.md). Nav2 is optional: local cleaning and fallback do not need a computer. The bridge/coverage action client are real ROS nodes, but their runtime, Nav2 tuning and physical network latency require integration testing with the robot. The ROS CI check builds/imports the nodes; it is not a navigation acceptance test.

## Carpet option (disabled pending measurements)

The optional current-based floor estimate and independent vacuum boost are described in [Coverage and recovery](COVERAGE_AND_RECOVERY.md#optional-carpet-estimate-and-vacuum-boost). Verify packet 56 separately, capture hard-floor/carpet current at both brush PWM settings and across the intended battery voltage range, then configure the `ROO_CARPET_*` block. Zero/default calibration cannot activate boost. Copy the entire new block when updating an older `config.local.h`; older configurations without that block remain disabled. The synthetic values in `tests/build_xiao.sh` are compilation fixtures and must never be flashed as calibration.
