# RoombaOI validation — 2026-11-07

Library version: **0.2.0**. Target: **plain Seeed Studio XIAO ESP32S3**, 8 MB flash / 8 MB OPI PSRAM. No board was attached and nothing was flashed.

## Review 1: protocol coverage

Compared the implementation with iRobot's 2021-04-13 Create 2 / Roomba 600 OI specification, including the rendered baud/group/bit tables and firmware errata. The machine-readable inventory is [oi-map.json](oi-map.json).

- 30 command opcodes accounted for, including the 130 alias implemented through 131.
- All 52 individual packet IDs (7–58), including reserved fields, and all 11 defined groups covered.
- Checked big-endian order, signed values, right-before-left wire order, baud codes, group lengths and capability gates.
- Implemented opcode 129 with TX completion followed by a full 100 ms guard before reconfiguring the host UART.
- Kept a single wake pulse separate from the three-pulse 19200-baud sequence.
- Identified documented ambiguities: song slots, encoder representation and the numerical error in the PDF stream example. These choices are explained in OPEN_INTERFACE.md.

## Review 2: state transitions and failures

Re-read the receiver and maintenance paths independently of the protocol inventory. Added regression cases for stale buffered replies, unexpected mode responses, recoverability after failed verification, host baud failure after robot acceptance, TX drain failure, truncated writes, time wraparound, paused streams and a delayed BRC release.

A maintenance operation returns `Ok` only after a new Passive-mode reply. A pulse is not an acknowledgement. Failed verification blocks normal command transmission until recovery. No automatic baud rollback, wake retry train or switch to Full is attempted. TX drain failure is treated as uncertain transmission, not successful delivery.

The host suite passes **707 checks** under AddressSanitizer and UndefinedBehaviorSanitizer. It includes 10,000 generated noise bytes, split frames, corrupt checksums, invalid packets, all 52 sensor widths/signs, all 12 baud rates and wheel-command expiration. The separate inventory audit passes. Counts are assertions in deterministic host tests, not physical test cases.

## Actual target builds

Tools used locally:

- Arduino CLI **1.5.1**, macOS arm64.
- Espressif Arduino core **esp32:esp32 3.3.12**.
- FQBN: `esp32:esp32:XIAO_ESP32S3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB`.
- `--warnings all`; no compiler warnings in these build logs.

| Sketch | Build | Program storage | Static RAM |
|---|---|---:|---:|
| BatteryMonitor | passed | 289,651 bytes / 3,342,336 | 23,716 bytes / 327,680 |
| LinkSetup | passed | 291,091 bytes / 3,342,336 | 23,732 bytes / 327,680 |

These are ESP32-S3 target builds, not only host syntax checks. Flash partitioning reserves application slots; OTA logic is not included. The static RAM report is internal RAM use, not a measurement of PSRAM availability or runtime stack peaks.

Reproduce:

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
sh tests/run.sh
sh tests/build_xiao.sh
```

`ROOCORE_BUILD_DIR` can select the build output directory; otherwise the script creates a temporary directory and prints it. No upload command is run. GitHub Actions repeats host tests, the original recorder tests and the two target builds.

## Still requires the delivered hardware

Actual Roomba firmware/baud, waking on the dock, NPN pinout/polarity, UART electrical quality, sensor behavior, charging, supply thresholds, real motor stop timing and USB/source isolation. Older firmware may not wake through BRC on the dock. The library reports missing responses; compiling cannot repair or prove that firmware behavior.

The existing ESP-01S recorder remains a separate legacy application. It has not been silently migrated to XIAO, and the library examples are not a complete Wi-Fi/OTA/navigation firmware.

## Cleaning firmware, coverage and assistance — 2026-11-08

`firmware/xiao/RooCoreXiao` and the `lib/RooNavigation` C++ library build for the same XIAO target in telemetry-only and commissioned configurations. The latter uses 512,515 bytes (15%) application flash and 148,284 bytes (45%) static internal RAM; the large navigation maps and temporary checkpoint buffer are allocated in the board's 8 MB PSRAM. Runtime PSRAM availability, task timing, stack/heap peaks, Wi-Fi, flash persistence and UART/motor behavior still need hardware tests.

Validation:

- 707 OI checks under AddressSanitizer/UndefinedBehaviorSanitizer and the opcode/sensor inventory audit.
- Native C++ navigation scenarios: open room, furnished room, perpendicular deep passes, charge/resume, two spaces connected by a door, optional bounded bumper contact, and a temporary obstacle that moves away. No unexpected collision assertion fired. The contact scenario generated four intentional low-speed contacts and completed the perimeter.
- Crosshatch measurement in the furnished deep scenario: pass 1 travelled about 112 m horizontally / 16 m vertically; pass 2 about 11 m horizontally / 119 m vertically. The second pass starts from the current position. These are simulated travel components, not a claim of optimal paths.
- Dynamic-obstacle checks verify repeated free-ray evidence, bumper-only persistence, bounded waiting with immediate safety override, and revisiting/cleaning a former obstacle position without collisions. Occupied-cell removals count cells, not tracked objects.
- Regression checks for central brush versus body footprint, independent pass masks, side-only credit, stopped motors, pose jumps, fresh-sensor requirements, no-progress limits, bounded contact/recovery, assistance IDs/frames/deadlines, three-scan confirmation, critical-stop policy and unanchored native docking timeout.
- 38 Python tests: historical reference regressions, actual C++ browser-adapter tests, HTTP lease behavior and global matcher tests. A symmetric rectangle and an unrelated scan are rejected; a distinctive furnished scene produces a candidate near its true pose. These synthetic cases do not establish a measured real-world false-match rate.
- The ROS bridge suppresses global scans/odometry for provisional or unresolved frames. ROS 2 Jazzy CI builds/imports the bridge, action client and assistance worker. It is not an end-to-end Nav2 or robot test.

The browser now executes the production Navigator rather than the old Python planner. In an independently measured furnished-room run it finished in 2,379.4 simulated seconds, travelled about 157 m and recorded zero collisions. Whole-floor truth estimates were **74.1% central-brush coverage and 94.8% combined central/side coverage**, using provisional brush geometry. The map's current-pass central metric was 84.1% of known free floor. The remaining floor was not counted clean; one perimeter attempt was incomplete and 2.35 square metres of known floor lacked a central-brush pass. **This is not complete floor coverage or evidence that every corner is reachable.** Local outputs are stored under ignored `output/`; the geometry/method/limitations are documented in [COVERAGE_AND_RECOVERY.md](COVERAGE_AND_RECOVERY.md).

Browser interactions are checked in Hebrew RTL at desktop and phone widths, including start/pause and injected LiDAR loss. The browser does not execute the full firmware assistance/UART/persistence orchestration. No robot or physical LiDAR was connected or flashed.
