# Electrical review of the purchased parts

2026-11-10 · Revision 5 · Design and document review, without energizing hardware.

Regulator, switch, XIAO and LiDAR socket pin maps were found in manufacturer documents. The remaining documentation gaps are specific: NOYITO board supply/OE routing, a datasheet matched to the supplied BOJACK transistors, and fuse DC interruption data. These gaps do not mean that every component is unidentified.

[Assembly stages](RooCore-assembly.html) · [Photo wiring](combined-wiring.html) · [Schematic](electrical-schematic.svg) · [Inventory](HARDWARE_INVENTORY.md) · [Hebrew review](../ELECTRICAL_REVIEW.md)

The [electronics](https://github.com/magnus919/agent-skills/blob/main/electronics/SKILL.md) and [esp32-development](https://github.com/magnus919/agent-skills/blob/main/esp32-development/SKILL.md) skills and relevant references were read. [Source hashes](../sources/skill-sources.json) record that review. Skills guide the process; manufacturer documents establish electrical specifications.

## Verified maps and remaining work

| Item | Established from documents | Still required |
|---|---|---|
| U3: Pololu D36V28F5 #3782 | Pads, common GND holes, fixed 5V, EN and PG | Test the wired unit and loaded cable-end voltage |
| S1: 4project 5274 / POL-2810 | Exact slide-switch model, pads, ON and internal control resistors | Mechanical switch OFF; verify actual switching |
| Plain XIAO ESP32S3 | 5V/VBUS, GND, D3/GPIO4, D4/GPIO5 and D5/GPIO6 | Full firmware is integrated and built; flashing and physical GPIO checks remain |
| T-mini Plus | 4.8–5.2V supply, up to 1A startup, socket map and 3.3V UART | Map the actual cable; qualify supply and signal levels; wires 12–13 use the integrated sequence |
| XT30 / GH cables | Purchased identities; 18AWG / 28AWG respectively | Verify actual polarity and contact mapping |
| NOYITO pair | Visible module labels, fitted capacitors and a 12-contact central IC | Map the module intended for Roomba. The second is spare |
| Q1/Q2 | Two 2N3904 NPNs; explicit onsemi E/B/C reference exists | Match supplied parts; qualify EN/BRC circuits separately |
| Former C1–C4 and R8–R11 | Removed from executable assembly instructions | Do not buy, solder or replace them with wires |
| Purchased fuse kit | Holder/kit identity and listing claims | Select fuse value and verify DC voltage, interrupt capability and time-current behavior |
| Low-profile OI wires | iRobot socket contact map; user chose direct wires retained with adhesive | Map and insulate wires; no Mini-DIN cable purchase required |
| Robot power return | Supplied R3 drawing has a shunt between J14 and SYS_RTN | Identify the actual 620 board revision and physical pads without bypassing the shunt |
| Battery cutoff | Nominal divider calculation is reproducible | Unit thresholds, tolerances, battery identity and rebound/restart behavior |

## Physical pin maps

Module pad labels, connector positions and IC package pins are different identifiers. Local positions below do not invent manufacturer cavity numbers.

| Part / view | Map | Current use |
|---|---|---|
| U3 in Pololu photograph | Left outer holes, top to bottom: EN, VIN, GND, GND, VOUT. PG is the inner hole beside EN | VIN from XT30 load side; VOUT to 5V BUS; grounds common; EN/PG open |
| S1 in #2810 map | VIN input, VOUT switched output, GND return, ON control, SW external switch | Regulated 5V input; D3→ON; SW/external-switch pads open |
| XIAO component side, USB at top | Right top down: 5V, GND, 3V3, D10, D9, D8, D7. Left: D0, D1, D2, D3, D4, D5, D6 | D3=GPIO4 control; D4=GPIO5 to sensor RX; D5=GPIO6 from sensor TX |
| Into the LiDAR sensor socket, Figure 3 | Left to right: VCC, TX, GND, RX | Map the mating plug; its wire-entry view can appear reversed |
| Top view into the robot female OI socket, per iRobot | 1/2 Vpwr; 3 RXD into robot; 4 TXD out; 5 BRC; 6/7 GND, toward the robot perimeter | No OI wires in the preparation harness; pins 1/2 are not our project power source |

Duplicate same-name S1 pads need no extra bridges. GND 1 and GND 2 only distinguish U3's two common holes.

Sources: [Pololu U3](https://www.pololu.com/product/3782), [Pololu S1](https://www.pololu.com/product/2810), [Seeed](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/), [YDLIDAR printed pages 3–4](../sources/YDLIDAR-T-mini-Plus-v1.1.pdf), [iRobot OI page 3](../sources/iRobot-Roomba-600-OI.pdf).

## Q1/Q2 supplied-part identification

The [onsemi 2N3904 Rev.9 reference](../sources/onsemi-2N3904-Rev9.pdf), TO-92 CASE 29 STYLE 1, gives **1=E, 2=B, 3=C**. For the depicted straight-lead part, flat face toward you and leads down: **E–B–C left to right**.

The purchased BOJACK B07T61SY9Y assortment also includes other transistor types. No BOJACK document was found tying the supplied 2N3904 to this exact package map; it is not established that onsemi manufactured it. Match the 2N3904 marking and package/manufacturer, or identify E/B/C with an appropriate test. Do not substitute a similar-looking 2N2222, BC337 or 2N3906.

For an isolated, unpowered NPN in diode mode, red on B and black on either other lead should show the two forward junctions; reversing polarity should not show ordinary forward conduction. This identifies the base/type, not E versus C with certainty. Do not infer E/C solely from a tiny difference in diode drops. A matching datasheet or a suitable E/B/C identification test resolves that gap; no new tester purchase is being requested.

Q1 is planned to pull EN down and Q2 to pull BRC down; they do not carry LiDAR load current. The onsemi 40V/200mA values are absolute limits, not operating targets. Neither circuit is included in the 13-wire harness.

## NOYITO: IC pins are not module connector holes

The product close-up shows 12 central IC contacts: five on each side, plus one top and bottom. This matches the geometry of TXB0104 RUT, but the marking is not sharp enough to establish the full manufacturer/part identity. Do not apply a 14-pin example to it.

[TI SCES650K, Figure 4-7 / Table 4-1, page 4](../sources/TI-TXB0104-SCES650K.pdf) gives this **reference IC map**:

| RUT pin | Name |
|---|---|
| 1 | VCCA |
| 2 / 3 / 4 / 5 | A1 / A2 / A3 / A4 |
| 6 | GND |
| 7 / 8 / 9 / 10 | B4 / B3 / B2 / B1 |
| 11 | VCCB |
| 12 | OE |

This is not a list of holes to solder. Establish pin 1 and the TI viewing direction first. The module labels are VCC, 3V3, two GND pads and A1–A4/B1–B4; no OE connector is labeled. Three capacitors and a resistor marked 103 are visible. Their appearance does not establish capacitor values. The three-lead component and routing suggest a possible regulator, but do not prove its identity or whether 3V3 is an input or output.

On the **Roomba module only**, disconnected from all sources and external wiring, establish:

| Check | Decision resolved |
|---|---|
| IC marking, contact count and pin 1 | Whether the RUT reference applies |
| Both GND pads to IC GND | Whether the pads share ground |
| VCC to VCCB; 3V3 to VCCA | Which IC supply reaches each pad |
| Each A/B connector to its corresponding IC pin | Channel-label routing |
| OE resistance to 3V3/GND and both ends of resistor 103 | Fixed, resistive or open enable routing; accessible control point |
| Three-lead component identity and 3V3 routing | Whether 3V3 is a regulator output that must not be paralleled with XIAO's output |

Look for copper-path resistance close to shorted-probe resistance, not just a beep; semiconductors and capacitors affect readings. Fine pads require magnification and suitable probes. Do not power the board, scrape tracks or solder to an IC leg just to begin mapping. Only after mapping can an isolated supply test be specified. Do not energize VCC/3V3 by guessing. Even a confirmed regulator output does not establish powered-off behavior or OE timing. The second NOYITO is spare, not a LiDAR requirement.

## Supply margin at the cable end

U3's 5V ±4% means **4.8–5.2V at U3**, exactly the LiDAR supply range. At the low corner there is no guaranteed margin for switch/contact/wire drop. This does not prove that the purchased unit will fail; it prevents an all-units guarantee from the label “5V” alone.

For S1, the reviewed Pololu specification gives about 16mΩ maximum combined on-resistance at the relevant 4.5V gate-drive condition. Using that for a conduction estimate near the 5V rail:

| LiDAR condition | Datasheet current | I×R drop | I²R dissipation |
|---|---:|---:|---:|
| Startup peak | 1.00A | 16mV | 16mW |
| Table maximum operating current | 0.48A | 7.68mV | 3.69mW |

These are calculations, not measurements, temperature guarantees or a complete transient model. Leakage and capacitor discharge still matter when off; S1 is not an isolating emergency disconnect.

If measured U3 output is 5.00V at 1A, the nominal remaining loop-resistance budget is `(5.00−4.80)/1−0.016 = 0.184Ω`, including both positive and return conductors. This is before measurement/transient margin and is not a design target at the limit. At 4.80V, no positive drop budget remains. Published wire length/gauge is not measured loop resistance.

D36V28F5 has a fixed output; do not try to adjust it upward. Measure at LiDAR VCC/GND under load. Investigate wiring/contact/regulator drop rather than adding random capacitors or resistors.

## Total load and the already-purchased fuse

`Iout = Ilidar + IXIAO + Iother`. As an **illustrative assumption only**, reserving 0.50A for controller/other loads gives 1.50A and 7.50W at 5V. At assumed 12V input and 80% efficiency, input current is approximately 0.78A before quiescent and startup details. The 0.50A is neither a measurement nor a guaranteed XIAO maximum; none of these values selects a 2A fuse or a battery threshold.

Pololu's typical current capability depends on input and cooling; the advertised 3.2A relates to 36V input conditions. Measure Wi-Fi/controller load and enclosed thermal behavior. An input-branch fuse does not automatically protect every 28AWG output lead or LiDAR connector.

The QBGTFAK fuses and holders are already in the purchase record. The holder belongs **in series with positive, near the battery branch source and before XT30 LOAD + / U3**. Still missing are the fuse-series DC voltage, interrupt rating and time-current/I²t data. “250V,” “13A” and a glass body do not establish them. Do not approve 2A merely because it exceeds an estimate, or short a battery to test a fuse. No duplicate purchase is requested.

## D3 and switch startup

The [Pololu schematic](../sources/Pololu-2810-schematic.pdf) includes a 10k series control resistor R4 and 10k base-to-ground R5. These are internal switch designators, unrelated to the old EN divider labels. A simple upper bound at 3.3V is `3.3/10000 = 0.33mA`; base-junction drop reduces it. Pololu describes activation above about 1V and OFF with ON open/LOW when the mechanical switch is OFF. D3→ON needs no extra R8. Mechanical ON overrides the software off request.

GPIO4 is not a strapping pin (GPIO0/3/45/46), USB GPIO19/20 or a memory line. ESP32-S3 also documents a brief low startup pulse there; that does not qualify subsequent firmware behavior. Preload LOW, avoid unwanted pullups and test reset/brownout. Firmware is built but unflashed; these transitions have not been measured. [ESP32-S3 Tables 2-1/2-2 and section 3](../sources/Espressif-ESP32-S3-v2.2.pdf).

## UART, USB and return paths

- XIAO GPIO uses 3.3V, not 5V. Roomba's documented 0–5V UART requires a qualified level interface. T-mini Plus uses 3.3V UART at 230400/8N1; do not raise its signals to 5V.
- YDLIDAR's HIGH range of 2.4–3.5V does not clearly separate VOH and VIH. ESP32-S3 VIH is 0.75×VDD, or 2.475V at 3.3V. If 2.4V is minimum TX output, the guaranteed margin is short by 75mV. Direct UART is conditional on signal-level qualification, not proven across all corners.
- TI specifies TXB0104 load constraints including up to 70pF, at least ±2mA external drive, and external pulls greater than 50kΩ. OI does not supply every 620 drive/load parameter. An IC name alone does not qualify a UART cable; check end-point levels and waveforms.
- Idle host TX can inject current into an unpowered receiver if its internal paths permit it. This is a design possibility, not a measured T-mini Plus fault. The implemented sequence detaches UART, drives D4 LOW and makes D5 an input with pulldown before D3 LOW. It handles intentional shutdown, not arbitrary electrical faults. No routine manual unplugging is required. See [the full sequence](LIDAR_POWER_UART.md).
- Preserve the single-source, no-added-diode choice. Disconnect XIAO from **all external power and signal wiring before USB**. Lowering EN or S1 is not physical isolation. Do not connect the Roomba battery to XIAO BAT.
- GND BUS is the project return, not building earth or permission to join every “minus.” The R3 drawing puts a total 0.05Ω shunt between J14 and SYS_RTN. Joining J14 to project GND while OI 6/7 also join it could bypass that shunt. A system-side return is the intended approach if verified on this 620 board; no physical solder pad is selected. Load current must have a proper power return, with parallel return paths reviewed.

The [supplied R3 PDF](../sources/R3.MainPCA.Pwr.n.Gnd.pdf) is a preliminary drawing, not proof that every 500/600 board revision is identical. A continuity beep cannot distinguish the sides of 0.05Ω. Do not select solder pads from that document alone or substitute charging contacts for the intended battery branch.

## EN thresholds are not installation settings

Nominally, an external 100k in parallel with the internal 100k gives 50k; 4.7k+470Ω gives 5.17k. The ratio `1+50/5.17 ≈ 10.6712` maps 1.20V/1.35V EN levels to approximately 12.805V/14.406V input.

Even allowing **only 1% tolerance of the three external resistors**, while optimistically treating internal resistance and thresholds as exact, gives about 12.633–12.981V cutoff and 14.212–14.604V restart. Internal tolerance and thresholds are not fully specified on the product page, so these are not complete worst-case bounds.

When XIAO disables U3, XIAO also loses power and Q1 stops pulling EN down. Battery rebound above the restart threshold can immediately re-enable it, causing cycling; this is not a latch. Qualify thresholds, timing, load removal and charging/docking for the actual battery. Never connect D10 directly to EN pulled toward VIN. The EN circuit remains outside assembly instructions.

## Measurement points and acceptance

No measurements below have been performed. Later tests are qualification requirements, not permission to energize the whole system now. Initial powered testing is limited to isolated U3 under the guide's conditions.

| Point/state | Instrument and connection | Expected result / acceptance |
|---|---|---|
| Isolated wire, unpowered | Resistance mode; compare with shorted-probe resistance | Near probe resistance end-to-end; no connection to a different net |
| TP1, unloaded U3 VOUT–GND | DC V, black COM/red V/Ω; isolated 12V source, initial 0.1A limit | 4.8–5.2V including uncertainty; no sustained current limiting. Investigate failure to settle |
| TP2, isolated XIAO 3V3–GND, later | DC V, one source, no NOYITO or robot | Around 3.3V. IC range 3.0–3.6V is not a regulator target or full board validation |
| TP3, S1 ON–GND, later after firmware | DC V plus reset waveform | OFF near 0V, ON near 3.3V, no unwanted startup pulse |
| TP4, LiDAR VCC/GND contacts under load, later | DC meter plus startup/shutdown/Wi-Fi waveforms | 4.8–5.2V at the sensor after rail rise and under load, including uncertainty; not merely no-load U3 voltage |
| EN/rebound, later | Variable supply and simultaneous VIN/EN/VOUT capture | Record both thresholds, rebound and shutdown/restart sequence against a separate qualification protocol |

An uncertainty interval crossing 4.8V or 5.2V is not a pass. A DC meter can miss brief dips. Attach scope reference to the chosen test GND, not the other shunt side, positive or a switching node. Do not defeat instrument earth or place an ammeter across a source. Without suitable instruments, retain the requirement as unperformed.

## Review boundaries

All 13 wire IDs, nets and endpoints match across photographs, schematic, Markdown and CSV/JSON. EN/PG, SW/external-switch pads, unused XIAO pads, USB and both NOYITO modules remain outside this preparation. One NOYITO is planned for Roomba; the other is spare.

No KiCad project/ERC/DRC, SPICE simulation, hardware measurement, flashing or robot operation was performed. The available pcb integration provides draw.io, not electrical rule checking. [Artifact/software validation](VALIDATION.md) is recorded separately.

The Seeed schematic link named v1.4 downloaded a PDF internally labeled Sense/V1.3. It was not used to establish the purchased plain board revision or regulator rating; the active pad map comes from Seeed's explicit XIAO ESP32S3 pinout. The [full firmware integration](FIRMWARE_INTEGRATION.md) uses UART2 on D4/D5 and has built in two configurations.
