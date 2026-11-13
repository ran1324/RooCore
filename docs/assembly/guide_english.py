# coding: utf-8
"""English text only. Connectivity and drawing geometry come from the Hebrew source."""
from copy import deepcopy
import guide_content as source

REVISION = '2026-11-12 • electronics + esp32-development • Revision 7 — user-reported body and brush dimensions'
SOURCES = deepcopy(source.SOURCES)
source_titles = [
    'Pololu D36V28F5 — model and pins', 'Pololu #2810 — operation and schematic',
    'User purchase link: 4project 5274 = POL-2810', 'Original switch schematic',
    'Seeed XIAO ESP32S3 — pads and power', 'NOYITO B07GMF3BWR — product photographs',
    'TI TXB0104 — IC datasheet only', 'YDLIDAR — manufacturer document, Figure 3 and Tables 2–4',
    'EGSCST GH1.25 — 28AWG leads', 'Yiqigou XT30U — 18AWG leads',
    'BOJACK — purchased transistor assortment', 'Supplied R3 schematic — draft, not a physical pad map',
    'Electrical review — findings, pin maps and checks', 'Espressif — ESP32-S3 v2.2, pins and logic levels',
    'onsemi 2N3904 — reference pinout; not a BOJACK specification',
    'electronics — skill used for the review', 'esp32-development — skill used for the review',
    'Espressif — GPIO configuration and internal pulls',
    'Espressif Arduino 3.3.12 — UART detachment and RX pull configuration',
    'Integration in the full RooCoreXiao firmware', 'Permanent UART wiring and power sequence — code and qualification',
    'Body and brush dimensions — user reports and integration status'
]
assert len(source_titles) == len(SOURCES)
for key, title in zip(SOURCES, source_titles):
    SOURCES[key] = (title, SOURCES[key][1])

STEPS = deepcopy(source.STEPS)
texts = {
 'prep': dict(title='1 · Lay out and label the parts', kind='Unpowered preparation',
    take='D36V28F5 regulator, Pololu #2810 switch, plain XIAO ESP32S3, an XT30 pair, wires and insulation.',
    gate='Disconnect every part from the battery, charger and USB. Remove the fuse; no power source may be connected to the harness.',
    actions=[
      'Label the regulator U3, the switch S1 and the controller XIAO. One NOYITO remains planned for the Roomba after its supply routing is identified; the other is spare and is not needed for LiDAR. Neither belongs to these LiDAR harness stages.',
      'Prepare two separate insulated distribution junctions: 5V BUS and GND BUS. Each bus joins only wires bearing that same net name.',
      'On a perforated solder board, check pad connectivity while the board is empty. Adjacent pads and pads on opposite faces are not necessarily connected.',
      'Use the purchased 18AWG wire for the power harness. Do not carry load power through Dupont leads or a solderless breadboard.',
      'Do not connect resistors or transistors yet. A reference 2N3904 pinout has been found; matching the supplied parts and completing the cutoff/BRC circuits are covered in the electrical review.'
    ], after='The parts are identified and the two distribution junctions are separate. Nothing is connected to the robot.'),
 'power': dict(title='2 · Prepare U3 and the XT30 load side', kind='Unpowered wiring',
    take='U3, the load half of the XT30 pair, 18AWG wire, and the 5V BUS / GND BUS junctions.',
    gate='Confirm D36V28F5. Leave the XT30 source half disconnected and insulated. Verify +/− by markings and continuity, not wire color alone.',
    actions=[
      'In the Pololu photograph, the five outer holes on the left are EN, VIN, GND, GND, VOUT from top to bottom. PG is the inner hole beside EN.',
      'Choose the connector halves so the future live source side has recessed metal contacts. Do not connect the source side to the battery.',
      'LOAD + and LOAD − are before voltage conversion. They will carry the battery branch voltage after the selected fuse; they are not 5V wires. The robot power-return pad has not yet been physically identified.',
      'Connect the four wires in the table one at a time. Check the pad marking before each solder joint.',
      'Leave EN and PG unconnected during this preparation. There is no calibrated battery discharge protection in this state.'
    ], image_note='Manufacturer pad map. The pad names are documented; compare your board with this viewing direction.',
    after='U3 is connected only to the load harness. No battery or Roomba board is attached; EN and PG are open.'),
 'switch': dict(title='3 · Add the LiDAR power switch', kind='Unpowered wiring',
    take='S1 — the switch purchased through 4project/5274, identified as POL-2810.',
    gate='The regulator and harness are unpowered. This is the #2810 slide-switch model, not a pushbutton model.',
    actions=[
      'Set the small switch on S1 to OFF using the manufacturer map at the end of this stage. The LED is lit in the original demonstration photograph; that is not our assembly state.',
      'Connect VIN and GND using the table. S1 receives regulated 5V, not the battery branch voltage.',
      'Leave ON open until stage 5 and VOUT open until stage 6. Leave SW and the external-switch holes open.',
      'Do not add R8 or a resistor in series with ON. This switch already has its control resistors. The photograph does not instruct you to add an external switch.'
    ], image_note='Pololu map: the external switch and red arrows in the source show an option we do not use. Our connections use VIN, VOUT, GND and ON only.',
    after='S1 has its 5V input and ground wired in the disconnected harness. Its switch is OFF and SW has no wire.'),
 'xiao': dict(title='4 · Connect controller power', kind='After checking U3',
    take='Plain XIAO ESP32S3, its antenna and two power leads.',
    gate='First test U3 alone as described in the checks section. Then disconnect the supply again. USB and all external signal wires must be disconnected from XIAO.',
    actions=[
      'Gently attach the antenna to the unpowered controller antenna socket.',
      'Connect GND, then 5V, using the table. The manufacturer photograph calls the 5V pin VBUS.',
      'Leave 3V3 and BAT open. Do not connect 3V3 to NOYITO or use it to supply the LiDAR.',
      'In the chosen arrangement without an added diode, remove XIAO from the entire external harness before every USB connection: power and signals. EN LOW or a disabled supply does not disconnect the wires.'
    ], image_note='Original Seeed pin map, USB at the top. The 3.3V-OUT pin is an output and stays open here.',
    after='Two power leads supply the controller from one source. All signal pins are still open.'),
 'control': dict(title='5 · Connect the switch control wire', kind='After firmware preparation',
    take='One short 22AWG signal wire between XIAO and S1.',
    gate='Both boards must be unpowered. Energizing the harness requires firmware configured and checked to hold D3/GPIO4 LOW at startup. Factory firmware does not establish this.',
    actions=[
      'Connect XIAO D3 to S1 ON. Viewed from the component side with USB at the top, D3 is the fourth pad down the left edge. It is GPIO4 in software, not IC package pin 4.',
      'There is no external resistor in this connection. Keep the mechanical switch OFF.',
      'Leave the other D pins open at this stage. D4 and D5 connect to UART in stage 7; BRC and EN cutoff are not wired here.'
    ], after='One control wire is connected. The full firmware now starts with LiDAR off and implements the power/signal sequence; flashing and physical GPIO checks have not been performed.'),
 'lidar': dict(title='6 · Prepare the LiDAR power leads', kind='After connector mapping',
    take='T-mini Plus and a four-position GH1.25 plug from kit B0H3Q98C58.',
    gate='Disconnect both LiDAR and the harness. Match the plug to the socket and map every contact before wiring power. Do not connect it if the viewing direction is unclear.',
    actions=[
      'Look into the socket on the sensor as shown in the manufacturer figure. In that view the order is VCC, TX, GND, RX. Do not copy that order onto the wire-entry view of the cable plug.',
      'Map and label each lead by its contact in the disconnected plug. Wire colors are your choice and do not prove function. Check that contacts lock in place and adjacent contacts are not shorted.',
      'Connect only the power pair in the table. The GH kit has 28AWG tails; do not force incompatible 18AWG wire or contacts into it.',
      'Insulate TX and RX separately and leave them unconnected during the power-only test. These are the two 3.3V UART contacts in the GH connector. They become permanent connections in the finished system; this is not an instruction to unplug them on every shutdown.',
      'Keep the GH plug disconnected from the sensor until the harness passes its checks. Later, the LiDAR contacts must stay within 4.8–5.2V under load and during startup, accounting for instrument accuracy. A no-load 5V reading at U3 does not prove this.',
      'U3 is also specified at 4.8–5.2V. At its low limit there is no voltage-drop margin for wires and the switch. Do not attach the sensor if the cable-end measurement fails.'
    ], image_note='Original manufacturer page: Figure 3 looks into the sensor socket. Map the disconnected cable plug against that view.',
    after='The power harness is prepared but disconnected from LiDAR. TX/RX are added in stage 7. This is not yet a system ready for battery connection.'),
 'uart': dict(title='7 · Add the two permanent UART wires', kind='Permanent wiring with software shutdown',
    take='The two signal tails of the same GH cable. This connection needs no extra NOYITO, resistor, capacitor or transistor.',
    gate='Everything is unpowered, USB is disconnected and the GH contacts have been mapped. The full firmware includes the shutdown sequence; before operation, flash the correct version and perform the assembly checks in LIDAR_POWER_UART.md. Enable settings remain false/0 until physical qualification.',
    actions=[
      'Viewed from the XIAO component side with USB at the top, D4 is the fifth pad down the left edge and D5 is the sixth. They are GPIO5 and GPIO6 in software. The SDA/SCL labels do not force I²C operation; these pins are reserved for LiDAR UART in this configuration.',
      'Wire 12: connect D4/GPIO5 to the LiDAR cable lead labeled RX. This carries controller transmit to the LiDAR receive input.',
      'Wire 13: connect the LiDAR cable lead labeled TX to D5/GPIO6. This carries LiDAR transmit to the controller receive input. Do not connect TX to TX.',
      'Both communication signals use 3.3V levels. Do not connect them to 5V BUS or switch VOUT. Wires 7 and 11 already provide the common ground; no extra ground wire is needed.',
      'Solder and insulate each connection separately. Keep the supplied 15cm GH tails and do not extend them before signal qualification. There is no series resistor or capacitor in either wire.',
      'On a normal shutdown, software stops accepting writes, finishes or cancels transmission, and detaches UART from the pins. D4 then drives LOW and D5 becomes an input with an internal pulldown. Only then does D3 go LOW to turn S1 off. No wire is unplugged.',
      'At startup, software holds the signals in their off state, raises D3 and continues polling robot sensors during rail settling. Only after the configured interval does it enable UART2 at 230400, 8N1, without an RX pullup. ROO_LIDAR_POWER_VERIFIED=false and ROO_LIDAR_RISE_MS=0 prevent operation before hardware qualification. The settling interval has not been measured.',
      'The same sequence handles normal shutdown, charging policy and battery cutoff. D8/OE control has been removed. Failure to open UART or send the scan command turns LiDAR off and latches a fault; there are no automatic repeated power-on attempts.',
      'During charging, the local controller requires valid charger state before preparing LiDAR for departure. Preparation stays latched during warm-up; battery percentage alone is not a departure command. Charging does not consume the active cleaning budget. Dock geometry, departure and temperature remain unqualified until physical calibration; the server receives read-only progress reports.',
      'This handles intentional LiDAR shutdown while XIAO remains powered. It does not isolate signals during an unexpected rail collapse, switch failure or total controller shutdown. Reset and whole-system power-loss qualification remain separate requirements before robot use.'
    ], image_note='Seeed map: D4/GPIO5 and D5/GPIO6 are fifth and sixth down the left edge. Software routes UART to these pins, not D6/D7 printed TX/RX.',
    after='The harness defines 13 wires. TX/RX stay connected through every ordinary LiDAR shutdown. The sequence is integrated into the full RooCoreXiao application; it remains disabled by default pending hardware qualification. No flashing has been performed.')
}
row_text = [
 ('18AWG','Continuity only between connector + and VIN; the other connector half is disconnected.'),
 ('18AWG','Identify GND from the photograph and marking. Both U3 GND pads share the same return.'),
 ('18AWG','VOUT is the bottom outer hole. No wire connects VIN to 5V BUS.'),
 ('18AWG','Keep this ground junction separate from 5V.'),
 ('18AWG','VIN is the input side. S1 receives only regulated 5V from U3.'),
 ('18AWG','Use a marked GND pad. Never connect GND to SW.'),
 ('Short 22AWG','Component side, USB at top: GND is the second pad down the right edge.'),
 ('Short 22AWG','Top-right pad. USB disconnected; do not use 3V3 or BAT.'),
 ('Short 22AWG','Continuity from D3 to ON only; no connection to SW.'),
 ('28AWG GH tail','The contact mapped as VCC using the sensor-socket view, not wire color.'),
 ('28AWG GH tail','The mapped GND contact. Insulate TX and RX separately.'),
 ('Short 28AWG GH tail','D4 to RX only. RX is rightmost in the sensor-socket figure; do not rely on color.'),
 ('Short 28AWG GH tail','TX is second from the left in the sensor-socket figure; connect it to D5 only.')
]
for step in STEPS:
    replacement = texts[step['id']]
    assert len(step['actions']) == len(replacement['actions']), step['id']
    step.update(replacement)
    for row in step['rows']:
        row['gauge'], row['check'] = row_text[row['id']-1]

PENDING = [
 ('Body and brush dimensions — approximate measurements', 'User report: body about 33 cm, main brushes 16×7 cm, side radius 7 cm and up to 5 cm protrusion beyond the body. Code now uses 160×70 mm, an estimated 35 mm rearward offset and a 70 mm side radius. Planning no longer recentres at the start cell and stalls at a furniture corner; cleaning targets now account for brush offset. Clearance checks were not weakened. The collision envelope remains 340 mm; side pivot and the 17×8 frame discrepancy remain uncalibrated. Calibration stays false. See ROBOT_GEOMETRY.md.'),
 ('One NOYITO for Roomba; the other is spare', 'The Roomba UART uses 5V levels and the controller uses 3.3V, so one module remains planned for that interface. Its VCC/3V3 and OE routing must be established before wiring. LiDAR already uses 3.3V UART with software power sequencing, so the second module is unnecessary. Do not add R9/R10/R11 or C1–C4.'),
 ('UART is integrated; hardware qualification remains', 'Wires 12–13 connect D4→RX and TX→D5. RooCoreXiao uses UART2 and a shared shutdown path; configure rail settling only after measurement. Flashing, signal levels, reset and total power loss remain untested. This is not hardware fault isolation; see LIDAR_POWER_UART.md.'),
 ('Q1/Q2 — reference pinout found', 'These are two 2N3904 transistors, not two new models. The onsemi TO-92 reference is E–B–C left to right with the flat face toward you and leads down. No matching BOJACK specification was found for the supplied parts. Match markings/manufacturer or identify E/B/C appropriately; a two-diode test identifies the base only. Q1 for EN and Q2 for BRC are not wired yet.'),
 ('Fuse already purchased — placement known; rating pending', 'Place the fuse holder in series with the positive project branch, near the battery source and before XT30 LOAD + and U3. No duplicate purchase is requested. Select the fuse from the kit using operating/startup current and DC interrupt capability; 2A remains a candidate only. A 250V marking and a 13A listing claim do not establish those properties.'),
 ('Roomba power connection points', 'No physical positive or power-return pad has been identified on your board. The R3 schematic separates J14 and SYS_RTN with a shunt; do not bridge them. A continuity beep cannot distinguish its two sides.'),
 ('Low-profile OI harness — no plug body', 'The user chose direct wires retained with adhesive to close the original cover. A Mini-DIN 7 cable has been removed from missing purchases. Retain the Roomba socket contact map for identification; verify each lead and insulate adjacent contacts before retention. Pins 1/2 do not power the project.'),
 ('Assembly measurements are not unidentified pins', 'U3/S1/XIAO and LiDAR pin maps are documented. Cable-end LiDAR voltage, controller current, EN thresholds and battery rebound still require measurements. 12.81/14.41V are nominal calculations; external resistor tolerance alone changes them. Test points and acceptance limits are in the electrical review.')
]
MODULES = [
 ('Plain XIAO ESP32S3','B0DJ6NQFKX • pack of 3','5V/GND power; D3=GPIO4 control; D4=GPIO5 transmits and D5=GPIO6 receives LiDAR UART. Requires software power sequencing.'),
 ('Pololu D36V28F5','#3782 • purchased locally','Documented pins and 5V ±4% output. Check cable-end voltage drop. EN is not calibrated battery protection.'),
 ('Pololu Mini MOSFET Slide Switch LV','4project 5274 / POL-2810','Identified by the user purchase link. No external R8 is needed.'),
 ('NOYITO TXB0104 / WCMCU-401','B07GMF3BWR • pair','One planned for Roomba after supply mapping; the other is spare, not required for LiDAR. Capacitors are already fitted.'),
 ('YDLIDAR T-mini Plus','B0BJ2LWDM9','Switched 5V supply; permanent two-wire 3.3V UART. No level shifter in this LiDAR path.'),
 ('EGSCST GH1.25','B0H3Q98C58','Four-position plug; 28AWG, 15cm tails. Map before use.'),
 ('Yiqigou/Amass XT30U','B0CFJKK6BX • two pairs','18AWG. Source side disconnected; verify +/− by continuity.'),
 ('BOJACK 2N3904','From B07T61SY9Y','onsemi reference: E–B–C facing the flat side. Supplied-part confirmation remains; no Q1/Q2 wiring yet.'),
 ('ELEGOO 525 resistors','B072BL2VX1','No added resistors in these preparation stages. Q1/Q2 circuits remain unreleased.'),
 ('QBGTFAK 5×20','B07BC8DW3L','Already purchased; series in the positive branch near its source. Fuse value and DC rating remain pending.'),
 ('Wire, solder board, insulation and retention','October purchases','Required for harness preparation. No hole-by-hole map without identifying the actual board.'),
 ('ESP-01S and extra 3.3V converters','Earlier purchases / spares','Not connected in this configuration.')
]
ENDPOINTS = deepcopy(source.ENDPOINTS)
locations = [
 'Marked + on the connector half; lead identified by continuity',
 'Marked − on the connector half; future branch return, not an identified robot pad',
 'Second hole down the outer row in the Pololu photograph',
 'Third hole down; suffix 1 distinguishes two common GND holes',
 'Fourth hole down; suffix 2 distinguishes two common GND holes',
 'Bottom hole in the outer row in the Pololu photograph',
 'Insulated distribution junction after the regulator',
 'Distribution junction separate from 5V',
 'VIN pad identified in the #2810 map', 'GND pad identified in the #2810 map',
 'Marked ON pad, not SW', 'Marked VOUT pad, switched output',
 'USB at top, component side: second pad down the right edge',
 'USB at top, component side: top-right pad',
 'USB at top, component side: fourth pad down the left edge',
 'USB at top, component side: fifth pad down the left edge',
 'USB at top, component side: sixth pad down the left edge',
 'Mates with leftmost contact in the sensor-socket figure; not plug-cavity numbering',
 'Mates with third contact from left in sensor-socket figure; not by color',
 'Mates with rightmost contact in sensor-socket figure; not the plug wire-entry view',
 'Mates with second contact from left in sensor-socket figure; not by color'
]
assert len(locations) == len(ENDPOINTS)
for key, location in zip(ENDPOINTS, locations):
    a,b,_,net = ENDPOINTS[key]
    ENDPOINTS[key] = (a,b,location,net)
NC = [
 ('U3','EN, PG','Open for regulator testing; no calibrated discharge protection'),
 ('S1','SW and external-switch holes','Open; onboard switch OFF'),
 ('U4 / XIAO','3V3, BAT, D0/D1/D2/D6/D7/D8/D9/D10','Unwired in this configuration; USB disconnected'),
 ('U1 / NOYITO for Roomba','All connectors at this stage','Still planned; map board supplies before OI wiring'),
 ('U2 / extra NOYITO','All connectors','Spare; not needed in the LiDAR path'),
 ('Q1, Q2, fuse and robot','Outside the preparation harness','Do not complete these connections by guessing')
]

def pairs(original, translated):
    """Keep structure and nonlocalized electrical identifiers identical."""
    if isinstance(original, str):
        yield original, translated
    elif isinstance(original, dict):
        assert original.keys() == translated.keys()
        for key in original:
            yield from pairs(original[key], translated[key])
    elif isinstance(original, (tuple,list)):
        assert len(original) == len(translated)
        for a,b in zip(original,translated):
            yield from pairs(a,b)
    else:
        assert original == translated

def translations():
    result = {}
    for name in ['REVISION','SOURCES','STEPS','PENDING','MODULES','ENDPOINTS','NC']:
        for original, translated in pairs(getattr(source,name),globals()[name]):
            if original != translated:
                if original in result:
                    assert result[original] == translated, original
                result[original] = translated
    return result
