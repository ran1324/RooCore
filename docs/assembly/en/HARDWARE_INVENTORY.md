# Current assembly hardware inventory

2026-11-10 · English companion to the [complete purchase record and history](../../../HARDWARE_INVENTORY.md). Purchase records do not establish delivery or physical validation. No new purchase is requested here.

**New trial power decision (14 November 2026):** The [power and docking plan](POWER_DOCKING_PLAN.md) records 13.8/13.5/13.0/12.5 V software states, a nominal 11 kΩ EN-to-GND divider for calculated cutoff near 12.1 V, and native fallback only with explicit user approval and a local safety check. The thresholds, approval and safety gate are now integrated in firmware; hardware flags remain false and measured values zero. No robot connection, flashing or calibration was performed. [Implementation](../../POWER_MANAGEMENT.md). The purchased kit records 10 kΩ and 1 kΩ, but no single 11 kΩ resistor; physical availability is unconfirmed. The older EN divider and 12.81/14.41 V calculations are superseded for this trial.

| Part | Purchased identity | Current role / limitation |
|---|---|---|
| Plain XIAO ESP32S3 | B0DJ6NQFKX, pack of 3 | 5V/GND power; D3/GPIO4 control; D4/GPIO5 TX and D5/GPIO6 RX for LiDAR |
| Pololu regulator | D36V28F5, #3782 | Fixed 5V ±4%; cable-end load/startup voltage still needs measurement |
| Pololu switch | Mini MOSFET Slide Switch LV #2810, 4project 5274 | VIN/VOUT/GND/ON; switch OFF; no extra ON resistor |
| Two NOYITO modules | TXB0104, B07GMF3BWR; WCMCU-401 on back | One planned for Roomba after supply/OE mapping; the other is spare, not needed for LiDAR |
| YDLIDAR | T-mini Plus, B0BJ2LWDM9 | Switched 5V power and two permanent 3.3V UART wires |
| GH cable kit | EGSCST GH1.25, B0H3Q98C58 | Four-position plug, 28AWG, 15cm tails; map contacts before wiring |
| XT30 pairs | Yiqigou/Amass XT30U, B0CFJKK6BX, two pairs | 18AWG; load side is before regulation; source half disconnected during preparation |
| Transistor assortment | BOJACK B07T61SY9Y | Q2 is planned for BRC and requires supplied-part E/B/C identification. Legacy Q1 is not needed for the new EN divider |
| Resistor assortment | ELEGOO 525, B072BL2VX1 | No resistor in the 13-wire harness; the separate EN section uses an inline 10kΩ+1kΩ pair without PCB. Thresholds and Q2/BRC remain unverified |
| Fuses and holders | QBGTFAK 5×20, B07BC8DW3L; recorded 12 holders / 110 fuses | Already purchased. Series with positive project branch near the source and before XT30/U3. Value, DC rating and interrupt capability remain unqualified |
| Wire and assembly materials | Purchased 18AWG/22AWG wire, heat shrink, solder boards and retention materials | No duplicate order needed. Check the actual board's pad connectivity before assigning holes |
| Earlier ESP-01S and 3.3V converter boards | Four ESP-01S and five converter boards in October records | Spares/other applications, not connected here; the listed 23V converter limit does not establish battery cutoff |

Product links are retained in the [illustrated guide](RooCore-assembly.html#bom) and its source list.

## Robot dimensions — user report, 2026-11-12

Body diameter is about 33 cm (photographs show 32–33), and the main-brush region is about 16 cm across by 7 cm along travel. The side-brush arm is about 6 cm **from the arm start to the bristle tip**, not from the screw centre. The user then reported 7 cm from the screw centre to the tip: a 70 mm side radius and calculated 140 mm sweep diameter. The user also reports up to 5 cm protrusion beyond the body edge; pivot position is not yet calibrated to that reach. These are approximate reported/photo dimensions, not floor-contact verification. The dimensions are now integrated after fixing route planning: 160×70 mm and a 70 mm side radius, with an estimated 3.5 cm rearward main offset based on the wheel-position description. The collision envelope remains 340 mm and side pivot position remains provisional. The follow-up 17×8 cm rectangle is recorded separately because frame versus active-region boundaries remain unresolved. See the [measurement record and assumptions](ROBOT_GEOMETRY.md). No robot was operated and calibration remains disabled.

## Corrections that must be preserved

- **VCC/3V3/VCCA/VCCB are not ground.** Never connect them directly to GND. TXB0104 IC pin names are not a verified NOYITO module connector map.
- Three capacitors are already visible on each NOYITO. Their values are not established by the photograph. Do not buy or add external C1–C4, or replace an omitted capacitor with a wire.
- The XIAO 3V3→NOYITO connection and D8→OE control were removed. The module 3V3 pad could be a regulator output; do not parallel it with XIAO 3V3 without the actual board map.
- S1 already contains its control resistors. Do not add external R8 to D3→ON. Pololu's internal R4/R5 designators are not the R4/R5 in the old EN divider proposal.
- EN is not a direct GPIO input. The nominal 12.81/14.41V calculations are not calibrated thresholds or current assembly instructions.
- Preserve the user's single-source arrangement without added diodes. Remove XIAO from all external power and signal wiring before USB service.
- Power is planned from a battery branch, not OI pins 1/2. Identify physical source/return pads and preserve the Roomba current-sense shunt.
- **No Mini-DIN 7 cable or plug body is required.** Use the user's planned low-profile direct wires with adhesive retention so the original cover can close. Preserve contact identification, insulation and strain relief.
- Photographs identify product models. They do not show our assembled or tested hardware.

## Permanent LiDAR UART

Wire 12 is D4/GPIO5→LiDAR RX; wire 13 is LiDAR TX→D5/GPIO6. D3/GPIO4 controls S1 ON. Before intentional power-off, detach UART, drive D4 LOW, configure D5 as an input with pulldown and no pullup, then lower D3. No manual unplugging or extra component is required in this path.

The full firmware is on `main` and uses UART2 and the shared shutdown path; default and enabled-fixture builds passed. Defaults remain false/0 pending physical qualification. See [integration](FIRMWARE_INTEGRATION.md) and [power/UART qualification](LIDAR_POWER_UART.md).

## Remaining evidence

Identify the Roomba NOYITO supply/OE routing, confirm the supplied 2N3904 pinout, obtain fuse series/DC interrupt data, and identify the actual battery and board power pads. Measure cable-end LiDAR voltage, controller load, EN thresholds and battery rebound. A current-limited bench supply was not present in the reviewed purchases; without one, remain at unpowered checks.

Height sensors, an I²C multiplexer, camera/Sense/microSD additions and older shopping-cart suggestions are not approved additions to this harness. Historical cart entries are not purchases. The October order's delivery status was not physically verified.

The user requires relevant corrections in the canonical Hebrew HTML, the English companion, Markdown, photographs and wire tables together. Rebuild with `python3 docs/assembly/build_guide.py` and validate with `python3 docs/assembly/check_artifacts.py`.


## Capture and updates — software status

RooCoreXiao now provides record-start/record-stop, computer-side OI sensor logging and energy estimates. Extended data require opt-in; internal PWM and separate vacuum current are not reported. Password-protected OTA requires stopped service hold, fresh telemetry and a measured voltage floor; it defaults off. Initial USB installation and physical qualification remain required. See NATIVE_RECORDING.md. No extra component or wiring-map change. [Details](NATIVE_RECORDING.md).


**Battery learning:** current integration, cycle history and two-slot persistence are integrated. No new components or wiring. OI coverage of all RooCore loads, current/gauge error, Flash floor and docking margin remain unqualified; flags and measured values remain false/0. [Details](../../BATTERY_LEARNING.md).

**Two-resistor EN preparation (14 November 2026):** R_UV_A=10kΩ and R_UV_B=1kΩ, each 1%, at least 0.25W, in series: U3 EN → 10kΩ → 1kΩ → U3 GND. [Schematic, physical pad map and checks](POWER_DOCKING_PLAN.md). No Q1 or external 100kΩ is needed for this divider. Unpowered preparation only; actual thresholds and robot connection remain unverified.

**LiDAR shutdown versus EN:** Routine shutdown is decided by ESP32, driving #2810 D3→ON after UART quiescence. EN is separate undervoltage protection for the whole add-on. The EN resistor pair is inline without a PCB, with insulation, strain relief and an unpowered cover-clearance check.

## Software update — cloud stream and local zone policy

Firmware now has an outbound HTTPS device channel and locally persisted map
zones. No component or wire is added. Cloud mode requires a saved policy and
holds motion without it. Native docking/fallback cannot run with map barriers.
Hardware commissioning flags remain disabled. Nothing was flashed or tested
on the assembly. See [the integration boundary](../../CLOUD_STREAM.md).


## Private Wi-Fi setup — 14 November 2026

Added protected AP/network selection, NVS commit/readback after connection and stopped BOOT re-entry. Internet loss never opens the AP or erases zones. A unique AP password is provisioned outside the repository; no secret in the template, no new component or wire. Software/compiler checks only; not flashed or physically tested. See `docs/WIFI_SETUP.md` at project root for conditions and limitations.

The complete process is now inline in the [assembly guide — initial setup](RooCore-assembly.html#initial-setup): 11 steps for private configuration, conditional building/flashing, Wi-Fi selection, server verification and map policy, plus troubleshooting. This is a procedure to perform; hardware-validation status is unchanged.
