# Guide and firmware validation

## Revision 10 — battery monitoring and learning, 14 November 2026

`BatteryLearning.h` and `BatteryLearningStore.h` are integrated in the RooCoreXiao control task: current/energy integration, 32 cycle summaries and 256 traces, OI-gauge-referenced capacity, full-charge anchoring and return reserve. Alternating LittleFS snapshots use CRC/readback; writes require qualified stationary Passive power and at least five minutes between checkpoints. Authenticated read/pagination APIs and explicit boot/history-scoped reset are implemented. Unreliable localization, gaps or a missing anchor block reserve use. Learned return can only begin earlier; fallback approval and hardware EN remain independent.

All `tests/run.sh` checks passed with ASan/UBSan; the latest battery tests also ran separately after adding restored-model and return-frame cases. Coverage includes integration, sign crossings, gaps, clock wrap, invalid gauges, temperature domain, OI-dependent capacity, bounded reserve, detour/unreachable routes, preservation of the active path, ring retention and torn-write/reboot recovery. HTTP on a real controller and physical Flash writes were not tested.

Both XIAO builds passed `--warnings all` with no warnings in the log: **621247 bytes Flash / 172484 static RAM** for defaults; **653711 / 174556** for the synthetic qualification fixture. The fixture enables learning/storage branches for compilation only. BatteryMonitor and LinkSetup also built. Source defaults retain empty battery/calibration IDs, false verification/adaptive/storage flags and zero measured values. No flashing, wiring or hardware operation occurred.

[Behavior, limits and API](../../BATTERY_LEARNING.md) · [Host checks](../sources/battery-learning-host-tests.txt) · [Build](../sources/battery-learning-build.txt) · [Source hashes/results](../firmware-integration.json).


## Revision 9 — power management and approved handoff, 14 November 2026

The 13.8/13.5/13.0/12.5V policy is integrated in the full firmware. ASan/UBSan policy tests cover thresholds/timing, boot/mission approval scope, replay/expiry, revocation, safety after approval, missing data, stop/storage/Dock failures, pause during arming, charging, a second mission and millis rollover. Planner handoff cannot bypass approval. Transport checks verify the stop sequence through the actual OI library.

The complete `tests/run.sh` suite passed: 707 OI checks, protocol map, LiDAR, legacy shutdown, Dirt/Stasis, carpet, eight navigation scenarios, zones, dock guard, charging and native capture. Concurrent recording/OTA changes were preserved and included in the integration build; OTA was not physically tested. Explicit function declarations allow both Arduino configurations to build from matching source.

Both XIAO configurations compiled without warnings using Arduino CLI 1.5.1 / core 3.3.12: defaults **587419 Flash / 154804 static RAM**; synthetic qualified fixture **619071 / 156876**. Fixture values are not physical calibration or flashing instructions. [Host tests](../sources/power-management-host-tests.txt) · [Build log](../sources/firmware-integration-build.txt) · [Behavior/API](../../POWER_MANAGEMENT.md). New hardware flags remain false and measured values zero. No firmware was flashed and no hardware operated.


## Revision 7 — brush geometry and route fix, 2026-11-12

All **101 Python tests** passed in an isolated environment with the CI-declared NumPy dependency. All C++ checks passed with ASan/UBSan, including eight navigation scenarios, the furniture-corner regression with zero and updated offsets, rearward brush coverage and a return-to-dock route from the previously stuck pose. The focused corner test fails when old recentering behavior is restored and passes with the fix. Clearance checks and retry limits were not weakened.

The combined zones/charging simulation completed both passes, retaining extra-carpet coverage through charging, with zero collisions and zero brush contacts with the dock. Reported geometry propagation, absent geometry in older telemetry and JavaScript syntax were checked. This revision makes no claim of visual browser inspection or hardware validation.

Both firmware configurations and both examples compiled without warnings. Defaults: 539463 bytes Flash / 151708 static RAM; enabled fixture: 547895 / 151716. [Python log](../sources/geometry-python-tests.txt) · [C++ log](../sources/geometry-native-tests.txt) · [Build log](../sources/geometry-firmware-build.txt) · [Measurements and analysis](ROBOT_GEOMETRY.md). Guides, source snapshots, tables and portable kit were rebuilt and checked with `build_guide.py` and `check_artifacts.py`. Calibration and activation flags remain disabled; no firmware was flashed.

## Previous validation record — revision 6

2026-11-10 · Revision 6. File, browser, host-model and compilation checks only. No flashing, robot operation, physical power cycles or LiDAR measurements.

The combined map-zones integration passed all 100 Python tests, C++ ASan/UBSan checks and four XIAO builds (two examples and two full-application configurations). Charging during an extra carpet pass resumed through completion with coverage retained and no collisions. The map editor was independently checked in the browser, including a 390px layout.

| Check | Scope / result |
|---|---|
| Wire ledger | 13 matching CSV/JSON/source rows; stable IDs 1–13 |
| Net separation | Seven separate nets, accounting for U3's internally common GND holes; no external S1 VIN–VOUT bypass |
| UART | Wire 12: D4/GPIO5→sensor RX; wire 13: sensor TX→D5/GPIO6; separate from power and ON |
| Drawings | Same endpoint/net metadata and routes across schematic and combined view; XML identifiers/references checked; 13 pages in the main draw.io file |
| English version | Translated text over the same source geometry and wire IDs; English HTML/MD/CSV/JSON/SVG/draw.io; reciprocal language navigation |
| Local links and kit | Link targets, ZIP CRC, current document/code entries and source snapshots checked by `check_artifacts.py` |
| Browser | Desktop/mobile layout, images and interactive route highlighting checked; wide diagrams scroll inside their own container |
| Firmware | Full XIAO defaults: 538031 Flash / 151708 static RAM bytes; enabled fixture: 546471 / 151716. BatteryMonitor and LinkSetup also built |
| Host model | ASan/UBSan tests include 100 modeled LiDAR cycles, nonblocking startup, cancellation, timer wrap, stale RX, busy TX and latched partial-UART failure |

The model enforces no positive controller signal source while LiDAR is commanded off. It does not model semiconductor leakage, capacitors, physical reset or brownout. `powerOff()` is shared by normal and battery-cutoff paths; it is not a hardware isolation guarantee.

The integrated firmware remains disabled by default with `ROO_LIDAR_POWER_VERIFIED=false` and `ROO_LIDAR_RISE_MS=0`. A measured settling interval and physical qualification are still required. OI, cutoff, Dirt/Stasis, carpet and navigation host tests passed along with the new sequence test. See the [integration record and build log](FIRMWARE_INTEGRATION.md).

No KiCad project or kicad-cli was available. No ERC/DRC was run. The pcb connector supplies draw.io. The wire-net consistency check is not electrical ERC. The [EN simulation](../../../hardware/README.md) passed 32 DC cases and enable-state checks with ngspice 47. It uses a generic transistor and nominal thresholds. No validated model of the purchased modules is available.

## Reproduce without hardware

From a complete repository checkout:

```sh
python3 docs/assembly/build_guide.py
python3 docs/assembly/check_artifacts.py
sh tests/run.sh
sh tests/build_xiao.sh
```

The portable kit includes review snapshots, not the full build dependencies. Use the repository to compile firmware. [Source hashes](../firmware-integration.json) tie the snapshots to the tested code.

The guide preserves the user corrections: no Mini-DIN cable purchase requirement; the fuse already exists and belongs in the positive branch near its source; one NOYITO is planned for Roomba, the other is spare.

[English guide](RooCore-assembly.html) · [Electrical review](ELECTRICAL_REVIEW.md) · [Hebrew validation](../VALIDATION.md).

Revision 6: all 92 Python tests, C++ ASan/UBSan checks and both full-firmware builds passed. Added charge/resume, dock guard, live map, history and maintenance tests. Earlier browser layout checks above describe the guide layout; this revision updated its content and checked artifact consistency. No hardware experiment was performed.


## Two-resistor EN appendix — 14 November 2026

Added an exact schematic, endpoint table, CSV/JSON and draw.io for EN → 10kΩ → 1kΩ → GND. This conditional design appendix is separate from the preparation harness. English was reviewed alongside Hebrew. No firmware was modified and no hardware or CAD ERC/DRC check was performed.

The current `check_artifacts.py` run stops at an existing mismatch between `firmware/xiao/RooCoreXiao/RooCoreXiao.ino` and its recorded build hash in `firmware-integration.json`. That build hash must not be refreshed without validating the corresponding build. Earlier PASS results above do not describe this run.


## General guide synchronization and compact EN pair — 14 November 2026

Audited working-tree changes against the guide: power/fallback thresholds, 10kΩ+1kΩ EN pair, ESP32/S1 LiDAR shutdown, UART sequence, recording/OTA, battery learning, brush geometry, management server/maps and Apple Home simulator. Added a visible navigable section with schematic, physical pad map, endpoint table, individual heat-shrink insulation, flexible leads, removable strain relief and an unpowered cover-clearance check. Clarified that the basic 12V U3 test precedes the EN divider. Server zones and simulator are not active robot integrations.

Corrected legacy D10/Q1 references in LiDAR and audit records, separating autonomous undervoltage protection from intentional LiDAR shutdown. Historical analysis is labeled. Standing AGENTS.md instructions now require canonical/English guide updates within every relevant task, without waiting for another request.

Bilingual build and current artifact checks passed, including existing firmware source hashes, 13 preparation wires, 7 nets, resistor appendix, links and ZIP. The early recorded firmware hash mismatch above is absent from the current check; this documentation task did not rewrite build-evidence hashes to bypass it. The Hebrew page was opened in a browser and the new section verified. No new mobile visual check, physical test, flashing or CAD ERC/DRC was performed.

Added guide-build.json source/output hashes. check_artifacts.py now also rejects stale prose/source inputs. CI checks before and after rebuilding, with triggers covering firmware, libraries, power/battery/Apple Home documents and server changes. This checks documentation consistency, not electrical or motion acceptance.


**Latest run in this audit:** After an earlier complete pass, RooCoreXiao.ino and config.example.h changed again concurrently. The bilingual guide build succeeds, but the full check_artifacts.py currently stops because those sources differ from the recorded firmware build. This does not identify a resistor-divider failure or validate the new firmware. This documentation task did not change the firmware build-evidence record.


## Cloud channel — software/build verification

The Python/C++ RCZ1 fixture passed truncation/corruption, map-identity and
offline planner barrier tests with ASan/UBSan. The final device-channel subset
passed 14 local cases; the prior whole-server run passed 57 before adding the
last restart test. Existing native host tests also passed. Arduino-ESP32 3.3.12
compiled the default and synthetic cloud/actuation configurations: 878439 /
67092 bytes and 910483 / 69012 bytes flash / static RAM respectively. Static
RAM is not a peak-heap or TLS-jitter measurement. Checkpoint scratch moved to
PSRAM; no physical timing guarantee follows.

Public HTTPS tests used sim-620 only: snapshot204, replay409, RCZ1 response200,
C++ decoding and simulated policy acknowledgement. No hardware identity was
used to create fake telemetry. No USB/serial port, flash or physical test was
performed. [Build](../sources/cloud-stream-build.txt), [host tests](../sources/cloud-stream-host-tests.txt),
[device tests](../sources/cloud-stream-server-tests.txt).


## Private Wi-Fi setup — software verification, 14 November 2026

Default XIAO firmware: **1,250,226 bytes Flash / 76,668 static RAM**; synthetic enabled configuration: **1,277,182 / 78,484**. Both full builds and BatteryMonitor/LinkSetup passed with warnings enabled and no compiler warnings. [Build](../sources/wifi-setup-build.txt).

The ASan/UBSan host suite passed; focused Wi-Fi tests cover SSID/password limits, malformed/duplicate URL fields, embedded NUL, corrupted/nonterminated storage, 10,000 randomized forms and clock wrap. [Focused result](../sources/wifi-setup-host-tests.txt). The actual setup page passed a localhost mock scan/select/submit/saved flow; hostile SSID text, cleared password and 390px viewport were checked. [Browser record](../sources/wifi-setup-browser-tests.txt).

Unique setup credentials remain outside the repository/ZIP; exact-value and redacted secret scans passed. No XIAO was identified, no serial port opened and no firmware flashed. Real RF, BOOT admission, stopped flash writes, NVS reset/power-loss recovery and simultaneous safety operation remain unverified. [Setup behavior](../../WIFI_SETUP.md).
