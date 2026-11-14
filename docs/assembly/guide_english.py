# coding: utf-8
"""English text only. Connectivity and drawing geometry come from the Hebrew source."""
from copy import deepcopy
import guide_content as source

REVISION = '2026-11-14 • electronics + esp32-development • Revision 11 — compact EN divider, controller-managed LiDAR shutdown and guide synchronization'
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
    'Trial voltage thresholds, EN divider and fallback approval',
    'Body and brush dimensions — user reports and integration status',
    'Apple Home — separate simulator firmware',
    'Management server — communication and map status'
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
      'Stages 1–7 have no resistor or transistor connections. The EN resistor pair has a separate unpowered section below, without a PCB. Q2/BRC still needs qualification; Q1 belongs to the old path.'
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


UV_SECTION = dict(
    control='Routine LiDAR shutdown is decided by ESP32 firmware: stop UART, drive D4 LOW and configure D5 as input with pull-down, then D3/GPIO4 LOW switches S1 #2810 off through ON. The mechanical switch stays OFF and ESP32 remains powered. This EN divider is separate undervoltage protection for the entire add-on; it does not decide routine LiDAR shutdown.',
    title='Hardware cutoff — two series resistors without a PCB',
    gate='Unpowered preparation only: battery, charger and USB disconnected. For Pololu D36V28F5 #3782 after matching the actual pads to the manufacturer photograph. Thresholds are calculated and unmeasured; this does not authorize robot-battery connection.',
    take='R_UV_A: 10kΩ; R_UV_B: 1kΩ. Each 1%, at least 0.25W, from the recorded assortment. No polarity. Together they provide a nominal 11kΩ.',
    route='U3 EN → short flexible wire → 10kΩ → 1kΩ → short flexible wire → U3 GND',
    orientation='In the Pololu component-side photograph, the five outer left pads top-to-bottom are P1 EN, P2 VIN, P3 GND (square), P4 GND, P5 VOUT. Inner P6 beside EN is PG. These are local positions, not IC pin numbers.',
    actions=[
        'Measure the resistors separately from U3 in ohms mode: 10kΩ within 9.90–10.10kΩ and 1kΩ within 990–1010Ω, accounting for meter accuracy. Join in series and check 10.89–11.11kΩ across the free ends before attaching to U3.',
        'Connect the free 10kΩ end to EN and the free 1kΩ end to either GND pad on the same regulator. The midpoint connects to nothing else. Resistor order can be reversed.',
        'No separate PCB is needed for this pair. Use short, flexible insulated signal wire, for example 22AWG, so the resistors do not lever the regulator pads. Solder and heat-shrink every exposed metal joint; leave no bare leads that can touch.',
        'Use removable retention and strain relief without drilling or cutting the robot. Keep clear of moving and hot parts. With all power removed, check that the original cover closes without pressing the resistors or pinching wires. Actual robot clearance has not yet been measured.',
        'PG stays unused. The schematic 100kΩ is already inside U3; do not add it. The divider has no XIAO, D10 or Q1 connection.',
    ],
    table_head=['Net', 'From', 'To'],
    rows=[['EN_SENSE','U3 P1 EN','R_UV_A 10kΩ — 1'],['UV_MID','R_UV_A — 2','R_UV_B 1kΩ — 1'],['GND','R_UV_B — 2','U3 P3/P4 GND']],
    after='Calculated cutoff near 12.11V and restart near 13.62V at VIN. This disables the add-on rail, not the Roomba motors or battery. EN is neither a charger-only latch nor USB/UART isolation. First threshold testing uses a current-limited bench supply without the robot. The basic 12V U3 check in the tests section applies before adding the EN divider; after installing the pair, use the rising/falling threshold plan and do not expect startup at 12V. Robot pads, fuse and actual thresholds still require qualification.',
    detail='Full calculation, schematic and measurement table',
)

SETUP_SECTION = {'nav': 'Initial setup',
 'title': 'Initial setup — USB, Wi-Fi selection and server connection',
 'status': 'Integrated and compiled source; the setup page was tested against a mock server. No flashing, RF test, '
           'on-board NVS write or physical robot test has been performed. These are future procedures under the stated '
           'conditions, not a record of completed hardware steps.',
 'gate': 'Prepare the software over USB before connecting the controller to the harness. Disconnect XIAO from every '
         'external wire, including power and signals, the Roomba and charger. Power it only through a USB data cable. '
         'This stage does not connect the robot battery, operate motors or change hardware-verification flags.',
 'config_title': 'Prepare the private configuration',
 'config_head': ['Field', 'Setting', 'Purpose'],
 'config_rows': [['ROO_WIFI_SSID / ROO_WIFI_PASSWORD',
                  'Leave empty to choose a network in the setup page; they may remain empty after the controller saves '
                  'the network.',
                  'Home-network connection only; these are not website account credentials.'],
                 ['ROO_SETUP_AP_PASSWORD',
                  'A unique password of 12–63 printable ASCII characters without spaces. Empty disables the setup '
                  'network.',
                  'Access to the temporary RooCore-Setup network.'],
                 ['ROO_CLOUD_ENABLED / ROO_CLOUD_REQUIRE_POLICY',
                  'true / true in the private cloud configuration.',
                  'Server streaming and a motion hold until the matching zone policy is stored.'],
                 ['ROO_CLOUD_DEVICE_TOKEN',
                  'A private server-issued key for roomba-620. Do not use a sim-620 key or a user password.',
                  'Controller authentication to the server; the Wi-Fi page cannot change it.'],
                 ['ROO_API_TOKEN / ROO_OTA_ENABLED',
                  'For this preparation, leave the LAN API token empty and OTA disabled unless separately needed.',
                  'Separate LAN/update services; outbound HTTPS streaming does not require them.'],
                 ['ROO_COMMISSIONED and all verification/calibration flags',
                  'Keep flags false and unverified measured fields at 0.',
                  'Server connectivity does not authorize hardware operation.']],
 'steps': [{'id': 'prepare',
            'title': '1 · Identify the controller and prepare the file',
            'actions': ['The target is the regular XIAO ESP32S3 with 8MB Flash and OPI PSRAM. This procedure uses the '
                        'full RooCoreXiao firmware, not RooCoreMatterSim, the LinkSetup example or the synthetic test '
                        'configuration.',
                        'On the owner workstation, the configuration file is /Users/ransaymour/Documents/Codex/RooCore '
                        'Server/XIAO-cloud-config.private.h. Keep it private; do not put passwords in chat, the guide, '
                        'Git or the assembly kit. On another workstation, start with a copy of config.example.h and '
                        'obtain the device key privately from the administrator.',
                        'Copy the private contents to firmware/xiao/RooCoreXiao/config.local.h, which Git ignores, and '
                        'check the table above. The file outside the project is not loaded automatically by the build. '
                        'Do not put secrets into public config.example.h.',
                        'The current server already has a roomba-620 key provisioned. For a new device, only the '
                        'server administrator provisions a matching key and transfers it privately; selecting Wi-Fi '
                        'neither registers a device nor creates a key.',
                        'After creating config.local.h, run the command below to restrict it to owner read/write. It '
                        'changes file permissions only, without accessing the board.'],
            'code': 'chmod 600 firmware/xiao/RooCoreXiao/config.local.h',
            'result': 'A private config.local.h contains the device key and separate AP password, with hardware '
                      'operation still disabled.'},
           {'id': 'build',
            'title': '2 · Build the firmware before flashing',
            'actions': ['Use Arduino CLI and esp32:esp32 version 3.3.12, the toolchain used for the recorded builds. '
                        'Prepare the tools first on a new workstation; do not ignore a missing package or library '
                        'error.',
                        'Run the command below from the RooCore repository root. It builds with PSRAM=opi, '
                        'FlashSize=8M and PartitionScheme=default_8MB. It does not flash or open a serial port. The '
                        'restrictive umask applies to new artifacts; after a successful build, output permissions are '
                        'restricted to the owner too.',
                        'Require a successful exit. Outputs in output/xiao-ota include the application image, '
                        'bootloader, partition table and manifest.json. Because they are built from private '
                        'configuration, these artifacts contain secrets: retain them locally and do not attach them to '
                        'Git or a public ZIP.',
                        'tests/build_xiao.sh is a compiler check and includes a synthetic configuration with enabled '
                        'flags. Never flash those test artifacts onto the robot.'],
            'code': 'umask 077\nsh tools/build_xiao_ota.sh && chmod -R go-rwx output/xiao-ota',
            'result': 'RooCoreXiao artifacts were built from the private configuration; compilation is not hardware '
                      'verification.'},
           {'id': 'identify-port',
            'title': '3 · Identify the USB port and recovery route',
            'actions': ['With the controller fully disconnected from the harness, connect it using a USB data cable. '
                        'The command below lists ports without opening them. Establish the actual board and port '
                        'identity; do not choose a debug console, Bluetooth port or speaker from the list.',
                        'If identity is unclear, stop and check the USB cable/device identification. Never guess a '
                        'port. A listed port alone proves neither XIAO identity nor download mode.',
                        'Before flashing, identify the intended image, memory size and partitions, retain a known '
                        'previous image if one exists, and establish the Seeed ROM-download procedure for USB '
                        'reflashing. Recovery has not been tested on our unit. Do not erase all Flash/NVS/LittleFS as '
                        'a general troubleshooting step.'],
            'code': 'arduino-cli board list',
            'result': 'The actual XIAO port and a board-specific flashing/recovery plan are known. Do not flash if '
                      'either is missing.'},
           {'id': 'flash',
            'title': '4 · Flash only after identification and isolation',
            'actions': ['The command below is conditional on every previous prerequisite being satisfied. Set the '
                        'shell variable ROO_XIAO_PORT to the port actually verified; this guide has no fixed port '
                        'value. The first line refuses to continue if the variable is missing.',
                        'This writes Flash, replacing firmware and the boot/partition components in the artifacts. The '
                        'Espressif recipe resets before flashing and hard-resets afterwards; serial tools may also '
                        'reset through DTR/RTS. No robot or harness is connected.',
                        'Use the freshly built artifacts and the same FQBN. Do not guess Flash offsets or substitute a '
                        'Matter/simulator image for RooCoreXiao. If upload fails, remain USB-only and return to '
                        'identification/recovery; do not connect the robot to bypass the failure.'],
            'code': ': "${ROO_XIAO_PORT:?Set the confirmed USB-only XIAO port first}"\n'
                    'arduino-cli upload --fqbn '
                    "'esp32:esp32:XIAO_ESP32S3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB' \\\n"
                    '  --port "$ROO_XIAO_PORT" --input-dir output/xiao-ota \\\n'
                    '  firmware/xiao/RooCoreXiao',
            'result': 'Proceed to Wi-Fi only after successful upload and boot of the intended image. No upload is '
                      'claimed to have occurred here.'},
           {'id': 'join-ap',
            'title': '5 · Join the controller setup network',
            'actions': ['When no home network is saved or compiled and a valid AP password is configured, firmware '
                        'requests setup mode. Uncommissioned USB preparation also requires no verified robot link and '
                        'no motion ownership. Software cannot prove physical disconnection, so the first-step '
                        'isolation remains mandatory.',
                        'On the phone/computer, open Wi-Fi networks and select RooCore-Setup-xxxxxx. Enter '
                        'ROO_SETUP_AP_PASSWORD from the private file, not the home password, device key or website '
                        'password. The setup network accepts one client.',
                        'The phone may report no Internet. Stay connected for setup. Manually open exactly '
                        'http://192.168.4.1/setup — local HTTP inside a WPA2-PSK/CCMP protected AP. No automatic '
                        'window or DNS name is required.',
                        'Setup closes after five minutes. If a home network was already saved, firmware tries it '
                        'instead of opening an AP automatically; use step 11 to change networks.'],
            'code': '',
            'result': 'The RooCore network-selection page is visible locally. Home-network and server connectivity are '
                      'not yet established.'},
           {'id': 'choose-wifi',
            'title': '6 · Scan and choose the home network',
            'actions': ['Press the scan button and wait for the list. Select a 2.4GHz home network; its name fills the '
                        'network-name field. For a hidden network or one absent from the scan, enter the exact SSID '
                        'manually.',
                        'Enter the home-network password and press connect and save. Passwords are 8–63 printable '
                        'ASCII characters, or empty for an open network. SSIDs are limited to 32 bytes, so a non-ASCII '
                        'name can reach the limit before 32 characters. Enterprise networks and 64-digit raw PSKs are '
                        'not supported by this form.',
                        'Wait for a result; a connection attempt is limited to 25 seconds. On failure, check the '
                        'name/password and retry within the same setup window. Do not change the device key or safety '
                        'flags to fix Wi-Fi.'],
            'code': '',
            'result': 'An explicit success or failure message appears; pressing the button alone does not prove a '
                      'connection.'},
           {'id': 'save-network',
            'title': '7 · Confirm persistence and return to home Wi-Fi',
            'actions': ['Only after local Wi-Fi connection is one NVS record written and read back. Wait for the '
                        'saved-network message. A storage error is a failure even if RF association succeeded; '
                        'correct/retry rather than assuming the details will survive a reboot.',
                        'On success, the password field is cleared and the setup network closes after an eight-second '
                        'response window. Reconnect the phone to home Wi-Fi. The controller retains the network, so '
                        'the configuration file does not need to be filled again.',
                        'The saved message tests Wi-Fi, not Internet/server reachability. The record checksum is not '
                        'Flash encryption. Setup, expiry and reboot do not start cleaning.'],
            'code': '',
            'result': 'Software reports home-network connection and saved credentials; verify the server separately in '
                      'the next step.'},
           {'id': 'server-login',
            'title': '8 · Sign in to management and select the robot',
            'actions': ['Open https://roocore.saymour.co.il/ in the browser and sign in with your personal '
                        'username/password. Complete the eight-digit code sent to the registered email. No '
                        'built-in/public user password is provided by this guide; controller configuration is not the '
                        'website account file.',
                        'Use Forgot password and the personal email link if needed. The owner creates users and '
                        'assigns per-robot permissions in the website, not in Wi-Fi setup.',
                        'Select roomba-620. sim-620 is labelled simulation and is not the physical robot. A successful '
                        'website login alone does not prove the controller connected.'],
            'code': '',
            'result': 'The correct account is open and roomba-620 is selected; simulation data must not be presented '
                      'as physical robot data.'},
           {'id': 'verify-stream',
            'title': '9 · Verify authenticated controller streaming',
            'actions': ['Wait for roomba-620 to report online and update its last-seen time. The software cadence is '
                        'sensors/status each second and map/coverage each ten seconds when a snapshot is available. '
                        'The server reports online only from a fresh report, up to five seconds old; verify the time '
                        'keeps updating.',
                        'The controller initiates HTTPS to a fixed address using its roomba-620 key and '
                        'certificate/hostname verification. Internet, DNS and time synchronization are needed before '
                        'TLS. Do not open router ports, expose home MQTT or weaken certificate validation.',
                        'An isolated USB test has no qualified Roomba/LiDAR sensors or verified home localization. '
                        'Online state or a received controller map does not make those measurements valid. Record '
                        'server connectivity, sensor validity, localization and policy acknowledgement separately.',
                        'If no reports arrive, use the troubleshooting table: Wi-Fi, Internet/time, ROO_CLOUD_ENABLED '
                        'and device key. Diagnostic logs must not contain passwords or Authorization headers.'],
            'code': '',
            'result': 'The server received a fresh authenticated report from the correct device identity. Physical '
                      'sensor/safety validation remains separate.'},
           {'id': 'map-policy',
            'title': '10 · Save maps/zones and distinguish storage from enforcement',
            'actions': ['In the map editor, choose the current controller-observed map for roomba-620. Planning, '
                        'imported, old or sim-620 maps do not validate the physical home map. Physical localization '
                        'anchoring and calibration are required before robot operation.',
                        'Mark the intended zones, save the server edit, then use the send-zones-to-controller button. '
                        'This requires map-edit permission. A cloud-only saved edit is not yet active on the '
                        'controller.',
                        'Wait for the acknowledgement that the controller reports local storage of the exact revision. '
                        'Application requires a matching map and an already stopped robot with qualified '
                        'sensors/encoders and flash-save voltage; both mission slots are read back before '
                        'acknowledgement. On USB without the robot, pending policy is expected; never enable synthetic '
                        'verification flags to bypass it.',
                        'A software acknowledgement is still not physical enforcement proof. Missing/corrupt required '
                        'policy holds motion. Website room-clean/go-to actions currently save drafts and do not '
                        'dispatch physical motion. Do not press native Clean to test map barriers: independent native '
                        'cleaning does not enforce the RooCore map.'],
            'code': '',
            'result': 'Cloud storage, pending delivery and controller acknowledgement are recorded separately. '
                      'Cleaning is not released before hardware/enforcement tests.'},
           {'id': 'change-recover',
            'title': '11 · Change Wi-Fi, reboot and handle Internet loss',
            'actions': ['To change network, after firmware is running and the robot is already stopped, hold BOOT for '
                        'three seconds and release. On XIAO ESP32S3 this is GPIO0; header D0 is GPIO1 used for OI, not '
                        'the button. Do not hold BOOT during reset/power-on for Wi-Fi setup — that selects ROM '
                        'download.',
                        'Requests during cleaning, undocking, native operation or OTA are rejected. A commissioned '
                        'robot also needs the qualified stopped/Passive, sensor and flash-save voltage conditions; do '
                        'not weaken missing conditions to open the AP. Setup holds motion and completion does not '
                        'automatically resume a mission.',
                        'After successful storage, first test reboot in the same isolated USB preparation: verify '
                        'reconnection to saved Wi-Fi and a new server update without re-entering credentials. This is '
                        'a planned board test, not an already measured result.',
                        'Ordinary Wi-Fi/Internet loss neither opens setup nor erases received/stored maps and zones. '
                        'Local cleaning can continue only with valid localization, sensors and local policy. A newly '
                        'edited cloud restriction is unknown until received and stored. Reboot requires physical '
                        'reanchoring and explicit resume; floor cleaning/Internet-loss tests occur only after hardware '
                        'release.'],
            'code': '',
            'result': 'A network-change/recovery procedure is available without making local safety depend on the '
                      'cloud or automatically starting from setup.'}],
 'result_label': 'End-of-step check',
 'trouble_title': 'If setup stops — checks and next action',
 'trouble_head': ['Symptom', 'Check or action', 'Stop condition'],
 'trouble_rows': [['No RooCore-Setup network',
                   'Check that the flashed RooCoreXiao image has a valid AP password. Saved home credentials prevent '
                   'automatic setup; try BOOT only after boot and while stopped. The setup window is five minutes.',
                   'Do not flash or connect the robot if board/image/USB isolation is unclear.'],
                  ['Phone connects but page does not open',
                   'Stay on setup Wi-Fi and type http://192.168.4.1/setup. Do not change to HTTPS or rely on an '
                   'automatic window; check that the phone has not switched networks.',
                   'Do not open ports or disable router/server security.'],
                  ['Connection fails or network is absent',
                   'Check 2.4GHz, exact name/password; enter a hidden SSID manually. Correct/retry after an attempt of '
                   'up to 25 seconds.',
                   'Resolve a storage error; do not assume credentials survive reset.'],
                  ['Network saved but roomba-620 is offline',
                   'Check Internet and DNS/time, cloud enablement and the roomba-620 device key; ensure sim-620 is not '
                   'selected. Website access on the phone does not test the controller link.',
                   'Do not substitute a user credential or bypass TLS/policy requirements.'],
                  ['Connected but sensors/localization invalid',
                   'Expected in isolated USB preparation. Valid hardware needs assembly, calibration and measurements '
                   'under the guide release gates.',
                   'Do not enable commissioning just to change a dashboard indicator.'],
                  ['Zones saved in cloud but acknowledgement pending',
                   'Check current matching map, exact revision, communication and stopped write conditions. A '
                   'disconnected robot cannot receive a new edit.',
                   'Do not rely on new restrictions before acknowledgement, or treat software acknowledgement as a '
                   'floor test.']],
 'closing': 'After preparation, keep the controller disconnected from the harness until the guide electrical/physical '
            'checks are complete. Record what actually passed and what remains pending. Wi-Fi connection, website '
            'login, sensor reception and policy persistence are four separate checks; none alone releases robot '
            'operation.'}

PENDING = [
 ('Management server and maps — HTTPS code and local zone holds', 'Firmware now includes an outbound HTTPS device stream, sensors and map, with networking separate from control. Zones are checked and stored in both mission slots before acknowledgement; Internet loss does not erase them. Missing or corrupt required policy holds motion. Native fallback/docking is blocked with map barriers. Private configuration, flashing and physical tests remain; no connected robot is claimed. Hardware flags stay disabled and no wire is added. Private Wi-Fi setup now includes a protected AP, network list and save after successful connection; it opens for first setup or a stopped BOOT request, never because Internet was lost. No real-board test has occurred. See WIFI_SETUP.md, CLOUD_STREAM.md and the management-server documentation.'),
 ('Apple Home — separate simulator', 'RooCoreMatterSim is a separate Matter sketch compiled and tested in a host model only. It has not been flashed, paired or integrated with real control. Future tests require a XIAO fully disconnected from the robot harness and powered only by USB; installing the simulator replaces the main firmware on that board. No Apple Home wire is added. See the Apple Home source below.'),
 ('Battery monitoring and learning', 'Firmware integrates current/energy, retains 32 cycles and 256 traces in two snapshots, and estimates OI-gauge-referenced capacity and return reserve. Gaps/reboot invalidate continuity; learned return can only start earlier. Voltage thresholds, EN and fallback approval stay unchanged. Persistence/adaptive use remain blocked until measurement accuracy, all add-on loads, Flash voltage and docking margin are qualified. No new wiring. Documentation: ../../BATTERY_LEARNING.md.'),
 ('Native-cleaning capture and wireless XIAO updates', 'RooCoreXiao now provides record-start/record-stop, computer-side OI sensor logging and energy estimates. Extended data require opt-in; internal PWM and separate vacuum current are not reported. Password-protected OTA requires stopped service hold, fresh telemetry and a measured voltage floor; it defaults off. Initial USB installation and physical qualification remain required. See NATIVE_RECORDING.md.'),
 ('Power management and docking — new trial decision', 'The chosen trial values are a 13.8V alert, 13.5V return, 13.0V urgent return, 12.5V stop with optional fallback offer, and a calculated RooCore cutoff near 12.1V using 11kΩ from EN to GND. Native iRobot handoff requires explicit user approval and a local safety check; without both the robot stays stopped. The policy and approval are integrated in firmware; the old D10 path is inactive. Hardware flags stay false and measured values zero until qualification. Dock at the approach also requires approval; there is no bypass. POWER_DOCKING_PLAN.md now includes an unpowered conditional appendix with schematic and table: U3 EN → 10kΩ → 1kΩ → U3 GND, each 1% and at least 0.25W. No GPIO connection. This design appendix is separate from the 13-wire preparation harness; actual thresholds remain unmeasured.'),
 ('Body and brush dimensions — approximate measurements', 'User report: body about 33 cm, main brushes 16×7 cm, side radius 7 cm and up to 5 cm protrusion beyond the body. Code now uses 160×70 mm, an estimated 35 mm rearward offset and a 70 mm side radius. Planning no longer recentres at the start cell and stalls at a furniture corner; cleaning targets now account for brush offset. Clearance checks were not weakened. The collision envelope remains 340 mm; side pivot and the 17×8 frame discrepancy remain uncalibrated. Calibration stays false. See ROBOT_GEOMETRY.md.'),
 ('One NOYITO for Roomba; the other is spare', 'The Roomba UART uses 5V levels and the controller uses 3.3V, so one module remains planned for that interface. Its VCC/3V3 and OE routing must be established before wiring. LiDAR already uses 3.3V UART with software power sequencing, so the second module is unnecessary. Do not add R9/R10/R11 or C1–C4.'),
 ('UART is integrated; hardware qualification remains', 'Wires 12–13 connect D4→RX and TX→D5. RooCoreXiao uses UART2 and a shared shutdown path; configure rail settling only after measurement. Flashing, signal levels, reset and total power loss remain untested. This is not hardware fault isolation; see LIDAR_POWER_UART.md.'),
 ('Q1/Q2 — reference pinout found', 'These are two 2N3904 transistors, not two new models. The onsemi TO-92 reference is E–B–C left to right with the flat face toward you and leads down. No matching BOJACK specification was found for the supplied parts. Match markings/manufacturer or identify E/B/C appropriately; a two-diode test identifies the base only. Q1 belongs to the old software EN path and Q2 to BRC; neither is in the preparation wiring. The new 11kΩ divider does not require Q1.'),
 ('Fuse already purchased — placement known; rating pending', 'Place the fuse holder in series with the positive project branch, near the battery source and before XT30 LOAD + and U3. No duplicate purchase is requested. Select the fuse from the kit using operating/startup current and DC interrupt capability; 2A remains a candidate only. A 250V marking and a 13A listing claim do not establish those properties.'),
 ('Roomba power connection points', 'No physical positive or power-return pad has been identified on your board. The R3 schematic separates J14 and SYS_RTN with a shunt; do not bridge them. A continuity beep cannot distinguish its two sides.'),
 ('Low-profile OI harness — no plug body', 'The user chose direct wires retained with adhesive to close the original cover. A Mini-DIN 7 cable has been removed from missing purchases. Retain the Roomba socket contact map for identification; verify each lead and insulate adjacent contacts before retention. Pins 1/2 do not power the project.'),
 ('Assembly measurements are not unidentified pins', 'U3/S1/XIAO and LiDAR pin maps are documented. Cable-end LiDAR voltage, controller current, EN thresholds and battery rebound still require measurements. 12.81/14.41V belong to the old divider; the new 11kΩ design calculates nominal 12.11/13.62V, not yet measured. See POWER_DOCKING_PLAN.md and the electrical review.')
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
 ('ELEGOO 525 resistors','B072BL2VX1','The hardware-cutoff section describes a series 10kΩ and 1kΩ pair without a PCB, each 1% and at least 0.25W. Q1 is not needed for the new divider; Q2/BRC still awaits qualification.'),
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
 ('U3','EN, PG','Open in the 13-wire harness. The separate EN-divider section adds 10kΩ+1kΩ to GND; PG stays open. No calibrated discharge protection'),
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
    for name in ['REVISION','SOURCES','STEPS','UV_SECTION','SETUP_SECTION','PENDING','MODULES','ENDPOINTS','NC']:
        for original, translated in pairs(getattr(source,name),globals()[name]):
            if original != translated:
                if original in result:
                    assert result[original] == translated, original
                result[original] = translated
    return result
