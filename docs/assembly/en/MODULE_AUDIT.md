# Purchased-module audit

2026-11-10 · Revision 5. Based on purchase records, product images, schematics and manufacturer documents. No supplied unit was physically measured. This review does not qualify the assembly for battery connection.

[Electrical review and calculations](ELECTRICAL_REVIEW.md) · [Current inventory](HARDWARE_INVENTORY.md) · [Assembly guide](RooCore-assembly.html) · [Hebrew audit](../MODULE_AUDIT.md)

| Purchased part | Verified from available records | Correction / remaining evidence |
|---|---|---|
| Plain Seeed XIAO ESP32S3, B0DJ6NQFKX | D0–D10, 5V and 3V3 output in Seeed pin map | Removed 3V3→NOYITO. BAT open; disconnect the entire external harness before USB, preserving the no-added-diode choice |
| Pololu D36V28F5 #3782 | VIN/VOUT/two GND/EN/PG; fixed 5V; internal EN-to-VIN 100k | 5V ±4%=4.8–5.2V leaves no guaranteed cable-drop margin at the low corner. PG is not a 5V supply. EN is neither isolation nor calibrated battery protection |
| Pololu #2810 | User link identifies 4P-5274/POL-2810; schematic has 10k control R4/R5 | Removed external R8. Use ON, not SW. Mechanical switch OFF; physical operation remains untested |
| NOYITO TXB0104 pair, B07GMF3BWR / WCMCU-401 | VCC/3V3/GND/A1–A4/B1–B4 visible; no labeled OE; three fitted capacitors; central IC has 12 contacts | IC VCCA/VCCB/OE names are not a module map. Supply direction and OE routing remain unknown; do not parallel regulator outputs. RUT is a conditional reference, not proof of full IC identity |
| Second NOYITO formerly proposed for LiDAR isolation | TI describes IC powered-off behavior; seller photos do not prove the module supply/OE implementation | Direct 3.3V LiDAR UART now uses the integrated shutdown sequence. Second module is spare. One remains planned for Roomba after mapping |
| YDLIDAR T-mini Plus | Sensor-socket VCC/TX/GND/RX map, 4.8–5.2V supply and 3.3V UART | Map the actual GH plug and qualify loaded supply, signal levels and shutdown; no extra level shifter in this path |
| BOJACK 2N3904 assortment | Purchased kit includes 2N3904 NPN; onsemi reference is E–B–C facing flat side, leads down | Match supplied markings/package or identify E/B/C. Two base-diode readings do not reliably separate E/C. Q1/Q2 circuits remain outside the harness |
| ELEGOO 525 resistor kit | Purchased assortment recorded | No mandatory 11-resistor assembly list. R1–R7 belong to pending EN/BRC circuits. Measure isolated resistors; 470Ω differs from 470kΩ by 1000× |
| QBGTFAK 5×20 fuse kit | Already purchased; recorded 16AWG holders and 250V markings | Positive branch near source, before XT30/U3. DC interrupt and time-current data remain missing. 2A is a candidate; 13A is not the selected project fuse |
| Roomba 620 power return | R3 draft separates J14 from SYS_RTN by a shunt; OI grounds are on SYS_RTN | Removed unconditional battery-negative-to-common-ground wiring. No physical pad selected; a beep cannot distinguish shunt sides |
| Low-profile OI harness | User selected direct wires retained with adhesive | No Mini-DIN 7 cable purchase. Preserve socket mapping/insulation; pins 1/2 do not supply this project |
| Solder board/wires/insulation | Purchased materials recorded | No invented hole map. Check empty-board connectivity; keep 5V and GND junctions separate; no load power through Dupont/breadboard |
| ESP-01S and spare 3.3V boards | Earlier purchases/spares | Not connected here. Camera and height sensors are not additions to this assembly |

Q1 and Q2 are **two copies of the same 2N3904 type**, planned for EN and BRC. The missing evidence is a match to the supplied parts, not the absence of any online 2N3904 pinout. Similarly, the NOYITO gap is board routing, not a generic TXB0104 datasheet.

The electronics and esp32-development skills were read along with relevant references. Checks covered component identity, connector views, supply/drop/startup current, ON drive, return paths, USB/UART while off, EN tolerance and consistency between tables and diagrams. [Recorded skill sources](../sources/skill-sources.json).

Nominal EN divider calculations are not measured battery thresholds or a latching cutoff. Unit thresholds, Q1 identity, battery rebound, fuse selection and actual source/return pads remain unresolved. Generic service voltages do not replace measurements of this harness. A current-limited bench supply was not found in the reviewed purchase record; do not use the battery as the first-test substitute.

The active guide, Markdown, tables, SVG and draw.io share the same 13-wire list. Unidentified connections are not executable rows. Earlier contradictory drawings were retired; the historical archive is separate from the active kit. Firmware integration is complete in software, while flashing and electrical validation remain pending.

Artifact checks verify 13 consistent wires, seven separate nets, XML/cell references, matching diagram endpoints, local links and ZIP parity. Browser checks cover desktop/mobile, image loading and route selection. These are not KiCad ERC/DRC, physical short checks, voltage measurements or a robot test. See [current validation](VALIDATION.md).

## Sources

- [Seeed XIAO ESP32S3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/).
- [Pololu D36V28F5](https://www.pololu.com/product/3782), [user purchase link](https://www.4project.co.il/product/5274), [Pololu #2810](https://www.pololu.com/product/2810), [switch schematic](../sources/Pololu-2810-schematic.pdf).
- [Purchased NOYITO](https://www.amazon.com/dp/B07GMF3BWR), [TI TXB0104](../sources/TI-TXB0104-SCES650K.pdf).
- [YDLIDAR manufacturer document](../sources/YDLIDAR-T-mini-Plus-v1.1.pdf), [GH kit](https://www.amazon.com/dp/B0H3Q98C58), [XT30 pairs](https://www.amazon.com/dp/B0CFJKK6BX).
- [BOJACK kit](https://www.amazon.com/dp/B07T61SY9Y), [onsemi reference](../sources/onsemi-2N3904-Rev9.pdf), [ELEGOO kit](https://us.elegoo.com/products/elegoo-resistor-kit).
- [Fuse purchase listing](https://www.amazon.com/dp/B07BC8DW3L), whose direct page did not load during the review; this is not a DC-rating source.
- [Supplied R3 drawing](../sources/R3.MainPCA.Pwr.n.Gnd.pdf), [iRobot OI](../sources/iRobot-Roomba-600-OI.pdf), [ESP32-S3 v2.2](../sources/Espressif-ESP32-S3-v2.2.pdf).
