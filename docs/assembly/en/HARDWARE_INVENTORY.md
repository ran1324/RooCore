# Current assembly hardware inventory

2026-11-10 · English companion to the [complete purchase record and history](../../../HARDWARE_INVENTORY.md). Purchase records do not establish delivery or physical validation. No new purchase is requested here.

| Part | Purchased identity | Current role / limitation |
|---|---|---|
| Plain XIAO ESP32S3 | B0DJ6NQFKX, pack of 3 | 5V/GND power; D3/GPIO4 control; D4/GPIO5 TX and D5/GPIO6 RX for LiDAR |
| Pololu regulator | D36V28F5, #3782 | Fixed 5V ±4%; cable-end load/startup voltage still needs measurement |
| Pololu switch | Mini MOSFET Slide Switch LV #2810, 4project 5274 | VIN/VOUT/GND/ON; switch OFF; no extra ON resistor |
| Two NOYITO modules | TXB0104, B07GMF3BWR; WCMCU-401 on back | One planned for Roomba after supply/OE mapping; the other is spare, not needed for LiDAR |
| YDLIDAR | T-mini Plus, B0BJ2LWDM9 | Switched 5V power and two permanent 3.3V UART wires |
| GH cable kit | EGSCST GH1.25, B0H3Q98C58 | Four-position plug, 28AWG, 15cm tails; map contacts before wiring |
| XT30 pairs | Yiqigou/Amass XT30U, B0CFJKK6BX, two pairs | 18AWG; load side is before regulation; source half disconnected during preparation |
| Transistor assortment | BOJACK B07T61SY9Y | Two 2N3904 NPN parts planned for EN/BRC. Confirm supplied-part E/B/C against the reference before wiring |
| Resistor assortment | ELEGOO 525, B072BL2VX1 | No added resistor in the current 13-wire preparation harness; EN/BRC circuits remain pending |
| Fuses and holders | QBGTFAK 5×20, B07BC8DW3L; recorded 12 holders / 110 fuses | Already purchased. Series with positive project branch near the source and before XT30/U3. Value, DC rating and interrupt capability remain unqualified |
| Wire and assembly materials | Purchased 18AWG/22AWG wire, heat shrink, solder boards and retention materials | No duplicate order needed. Check the actual board's pad connectivity before assigning holes |
| Earlier ESP-01S and 3.3V converter boards | Four ESP-01S and five converter boards in October records | Spares/other applications, not connected here; the listed 23V converter limit does not establish battery cutoff |

Product links are retained in the [illustrated guide](RooCore-assembly.html#bom) and its source list.

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
