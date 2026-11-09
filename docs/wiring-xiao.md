# RooCore wiring — XIAO ESP32S3

Updated 2026-11-08. [Full SVG diagram](diagrams/wiring-xiao.svg) · [PNG preview](diagrams/wiring-xiao.png).

The diagram covers the purchased configuration: plain XIAO ESP32S3, D36V28F5, a MOSFET switch compatible with Pololu #2810, T-mini Plus, two TXB0104 modules, a 5×20 cartridge fuse and XT30. It replaces the ESP-01S diagram for the new installation. The old diagram is retained for documentation only. Purchase is not wiring validation; the components have not yet been connected or measured.

## Reading the diagram

1. Power: battery branch → one fuse near the source → XT30 → converter → XIAO and LiDAR switch. The XIAO regulator provides 3.3V; there is no additional 3.3V converter. The fuse does not carry the Roomba's original motor current. The branch point inside the robot has not yet been identified; do not solder to cells or an unidentified point.
2. Communication: two UART paths, with direction and pin numbers at each end. U1 translates 3.3V↔5V for the Roomba. U2 is powered at 3.3V on both sides and isolates the signals of the powered-off LiDAR.
3. Control: Q1 pulls down the converter's EN; Q2 pulls down the Roomba's BRC. Both are **NPN 2N3904**, not the PNP from the old diagram.
4. Default-state resistors and the XIAO pin map. Identical net names indicate a shared connection. All GND connections are common, including Roomba ground. LiDAR return current should flow through the power harness, not a thin UART wire.
5. Physical views of the XIAO component side and the Roomba and LiDAR sockets, following manufacturer views. Connect the supplied Wi-Fi antenna to the XIAO antenna socket.

Pin placement in the diagram blocks is chosen for readability, not board pad order. A1/B1 and A2/B2 are channel names, not IC package pin numbers. Q1/Q2 show the TO-92 front face with E–B–C following onsemi; identify the manufacturer and actual pin order of the BOJACK parts. Do not choose pins by appearance alone.

## Small components

| Reference | Value | Quantity |
|---|---|---:|
| R1, R5, R11 | 100kΩ | 3 |
| R4, R7, R8, R9, R10 | 10kΩ | 5 |
| R2, R6 | 4.7kΩ | 2 |
| R3 | 470Ω | 1 |
| Q1, Q2 | 2N3904 | 2 |
| F1 | 5×20 fuse in the purchased holder | 1 |

Resistors from the documented kit are 1%, quarter-watt. The dashed 100k resistor is already inside the converter: **do not fit another one**. The existing slide switch on the #2810 board stays OFF for control through ON; SW is unused. No external manual switch has been added.

## Connectors and limits before powering on

- **Roomba OI:** pin 3 RXD, pin 4 TXD, pin 5 BRC, pins 6/7 GND. Pins 1/2 are insulated and are not the power source in this design. The iRobot specification limits total continuous OI supply current to 200mA, so it must not power the LiDAR branch. The manufacturer diagram is a top view into the female socket on the robot; the solder side of a plug is a different view. Map the cable with continuity checks. Mini-DIN 7 was not found in the checked local purchase list; OBD2 is not a substitute.
- **LiDAR GH1.25-4P:** in the manufacturer's view into the sensor socket, with the lower notch as shown in the original diagram, the left-to-right order is VCC, TX, GND, RX. Compare the physical socket with the manufacturer's drawing rather than copying harness wire colors. Supply is 4.8–5.2V; UART is 3.3V, 230400 8N1. Measure at the cable end, including during startup.
- **XIAO without a diode:** a single external source on the 5V pin. BAT is unused; 3V3 is an output. Before USB, physically disconnect the external 5V supply conductor. Pulling EN low does not replace disconnecting the wire. This is the selected single-source configuration and does not implement Seeed's recommendation for a diode with external power.
- **TXB0104:** check that no OE jumper defeats control. Bypass capacitors are required near the translator supply rails; verify that each module has them (TI example: 100nF per rail), rather than assuming they are in the resistor kit. U1 OE with its resistor divider is the existing design; verify it rises only after the rails stabilize. Keep cables short and avoid external pull-ups on A/B. Additional power-filter capacitors have not been specified without harness and transient measurements.
- **Fuse:** 2A is only a candidate. Verify DC rating, breaking capacity, inrush and wire compatibility. A 250V marking or holder rating alone does not approve battery use. The battery connection point, chemistry and maximum charging voltage are also unverified.

## Shutdown: implemented and pending

External R1 in parallel with the internal 100k gives 50k; R2+R3 give 5.17k. Using the manufacturer's nominal EN thresholds gives cutoff around 12.81V and restart around 14.41V. **These are model values for bench-supply testing, not approved battery thresholds.** Also test voltage rebound after load removal and instability after startup. EN is not full isolation; standby current remains.

HIGH on D10/GPIO9 pulls EN to ground through Q1. If battery voltage is above the restart threshold, the system may restart immediately after the XIAO loses power; this is not a latch at every voltage. Firmware implements the full D10 shutdown sequence, including stopping, checking voltage after load removal, saving and a marker to prevent repeated cycles; it requires verified calibration. See [POWER_SHUTDOWN.md](POWER_SHUTDOWN.md). Without calibration, the default keeps D10 LOW and blocks motion. LiDAR shutdown through D3 and signal isolation through D8 are mapped in firmware but have not been tested on hardware. Hardware EN operates independently of software.

Start with a current-limited supply: converter alone, then controller, then switch and LiDAR, and finally sensor-only Roomba communication. With power off, check continuity and polarity; then check 5V and 3V3, EN cutoff/restart, absence of backfeeding and UART quality. Charging tests are performed without motion after bench tests pass.

## Sources and diagram verification

Checked on 2026-11-08:

- [Pololu D36V28F5 — VIN/VOUT/EN connections and internal 100k](https://www.pololu.com/product/3782).
- [Pololu #2810 — ON, SW and onboard switch](https://www.pololu.com/product/2810).
- [Seeed XIAO ESP32S3 — pin map and power](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/).
- [TI TXB0104 — OE, supply rails and bypass](https://www.ti.com/lit/ds/symlink/txb0104.pdf).
- [iRobot OI, p. 3 — robot connector](https://cdn.hackaday.io/files/1835247851890816/2021-04-13_iRobot_Roomba_600_Open_Interface_Spec-3.pdf).
- [YDLIDAR T-mini Plus, pp. 3–4 — socket and electrical interface](https://dedjh0j7jhutx.cloudfront.net/2036899223840006144/161db662c9e5a765435dfe03c4cd708e.pdf).
- [onsemi 2N3904 — example package and pins; not BOJACK verification](https://www.onsemi.com/pdf/datasheet/2n3903-d.pdf).

Local cross-check: `WIRING_GUIDE.md`, `POWER_SHUTDOWN.md` and `AMAZON_ORDER.md` in the original T9 copy; GPIO mapping against `firmware/xiao/RooCoreXiao/RooCoreXiao.ino`. The SVG is generated with `python3 tools/draw_wiring_xiao.py`. The diagram makes no claim of hardware testing.
