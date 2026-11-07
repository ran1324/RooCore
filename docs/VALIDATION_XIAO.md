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
