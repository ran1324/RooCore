# Guide and firmware validation

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
