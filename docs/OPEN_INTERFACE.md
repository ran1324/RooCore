# The Roomba interface and our library

Initial review and implementation: 2026-11-07. Target: Roomba 620 with XIAO ESP32S3. The iRobot specifications were reviewed and a C++11 library was written with host tests. No command was sent to the robot and its firmware version was not verified. This work does not replace or reverse-engineer iRobot firmware.

## Source basis

- [iRobot Create 2 / Roomba 600 specification, April 13, 2021 revision](https://cdn.hackaday.io/files/1835247851890816/2021-04-13_iRobot_Roomba_600_Open_Interface_Spec-3.pdf): an archived copy of the original iRobot document. Commands, sensors, group tables and the firmware-bug appendix were reviewed. Pages 3–4 cover connections, 7 modes, 8–22 commands, 23–39 sensors, and 40–41 differences and bugs.
- [Original iRobot Roomba 500 specification](https://cps-vo.org/sites/cps-vo.org/files/cpsvo_file_nodes/iRobot_Roomba_500_Open_Interface_Specpdf.pdf): the basis for comparison with the previous generation. The Create name does not establish that every feature exists on the 620; capabilities depend on the actual robot and firmware.

The specification provides no delivery acknowledgement for motor commands. A successful UART write means bytes were handed to the transport, not that the robot executed them. `requestedMode()` shows a requested or reported mode; `sensor(Sensor::OiMode)` is an explicit report with its measurement time.

## Interface capabilities

| Area | Meaning in our implementation |
|---|---|
| Operating modes | Passive for status reading and native cleaning; Safe for our control with iRobot protections; Full only through an explicit action that disables them |
| Motion | Straight speed, an arc with a specified radius, turning in place, per-wheel velocity or PWM |
| Native cleaning | Clean, Spot, Max, Seek Dock. These start built-in behavior; they do not navigate to a map location |
| Motors | Main brush, side brush and vacuum; direction and speed for motors supported by the specification |
| User interface | LEDs, buttons and songs; scheduling and digit displays depend on hardware and firmware |
| Safety and contact | Bumpers, wheel drops, cliffs, virtual wall and overcurrents |
| Battery | Voltage in millivolts, current in milliamps, temperature, charge and capacity in milliamp-hours, charging state and charging source |
| Motion and extended sensing | Distance/angle, encoders, light bumper, IR signals and Stasis; extensions are disabled in the initial profile until verified |
| Status reading | Single request, sensor list or continuous stream; the library handles byte order, negative numbers and checksum validation |

OI is not an API for the home map, internal cleaning route, coordinate target or restoring a native route after taking control. Entering Safe/Full replaces built-in cleaning control with ours. Passive allows sensor observation during native cleaning.

In Safe, the robot may stop and enter Passive because of a cliff, wheel drop or charger. The library does not automatically return it to Safe after such an event. Safe/Full also affect charging and sleep; call `releaseControl()` when control ends. `powerDownRobot()` does not disconnect power taken directly from the battery to the XIAO.

## Library structure

Code is in `lib/RoombaOI/src`:

- `RoombaOI.h/.cpp`: a core with no Arduino dependency or dynamic memory allocation. Public functions accept named values and units; bytes stay inside the core.
- `RoombaOIArduino.h`: connects to Arduino Stream, including ESP32 HardwareSerial. Pins and baud rate are configured when opening the UART.
- `examples/BatteryMonitor`: reads only battery data and does not run motors. The connection example uses GPIO2 as RX and GPIO1 as TX through a level translator.
- `tests/roomba_oi_test.cpp`: simulated transport, command/response fixtures, faults and decoding. `sh tests/run.sh` compiles and runs with AddressSanitizer and UndefinedBehaviorSanitizer.

Copy/link `lib/RoombaOI` into the Arduino libraries folder, or add the two core files to another C++ application. Board compilation is checked separately using `tests/build_xiao.sh`; results and tool versions are documented in [VALIDATION_XIAO.md](VALIDATION_XIAO.md). Compilation does not mean the code was flashed or measured on the robot.

Logical usage example; check operation return values in the application:

```cpp
robot.beginPassive();
robot.requestBattery(now);
// On every loop iteration:
robot.poll(now);
auto battery = robot.battery(now);
if (battery.valid && battery.percentageValid) {
    // battery.percentage, battery.millivolts, battery.milliamps
}

// Only when taking over motion control:
robot.enterSafe();
robot.driveWheels(100, 100, now); // left, right; mm per second
robot.stopWheels();
robot.releaseControl();
```

Motion commands renew with a default validity of 300 milliseconds, configurable by the application up to 2 seconds. On expiry, `poll()` sends wheel and cleaning-motor stop commands and returns to Passive. Explicit entry into Safe is then required to continue. **This XIAO firmware protection works only while the loop and UART are operating**; it is not a Roomba hardware watchdog and does not guarantee stopping when firmware hangs or the cable is disconnected. Autonomous cleaning commands are not covered by this motion expiry.

`stopWheels()` is not an emergency stop in every mode: in Passive, a Drive command may be rejected. To take over native cleaning, explicitly enter Safe and then stop. Do not replace this with a blind Clean press, which could start cleaning.

## Reviewed command map

| Opcode | API / implementation decision |
|---|---|
| 128 | `beginPassive()`; before other commands |
| 129 | `changeBaud(rate)`: drain TX, send the code at the old rate, wait 100ms after transmission completes, change the local UART and check a fresh response |
| 130 / 131 | `enterSafe()` sends 131; 130 is a legacy alias, with no duplicate API |
| 132 | `enterFullDisablingSafety()`; never the default |
| 133 | `powerDownRobot()` |
| 134 / 135 / 136 / 143 | `spot()`, `clean()`, `maxClean()`, `seekDock()` |
| 137 | `driveStraight()`, `driveArc()`, `turnInPlace()` |
| 138 / 144 | `setMotors()`, `setMotorPwm()` |
| 139 | `setLeds()` with Boolean fields and color/intensity |
| 140 / 141 | `defineSong()`, `playSong()`; four songs, up to 16 notes per song |
| 142 / 149 | `request(sensor, ...)`, `request(sensors, count, ...)`; `requestBattery()` requests the battery group |
| 145 / 146 | `driveWheels(left, right, ...)`, `driveWheelPwm(left, right, ...)`; the library converts to the protocol's right-left order |
| 148 / 150 | `startStream()`, `pauseStream()`, `resumeStream()` |
| 162 / 163 / 164 | `setSchedulingLeds()`, `displaySegments()`, `displayText()`; display opt-in required; 163 is marked with a known bug in the specification |
| 165 | `pressButton()`; a named value, not a user-supplied byte mask |
| 167 / 168 | `setSchedule()`, `setClock()`; opt-in required; do not assume the 620 has other models' display/scheduling |
| 7 / 173 | `resetRobot()`, `stopInterface()`; disabled by default until support is verified; reset requires draining the boot message |
| 147, 151–158 | Legacy Create cargo/script/IR commands; not presented as Roomba 620 capabilities |

BRC is a physical line for wake/baud changes. `wakeViaBrc()` sends one 100ms LOW pulse, releases, waits 2 seconds and checks a fresh OI response. `forceBaud19200ViaBrc()` is for an already powered robot: release the line for 2 seconds, send three 100ms LOW pulses separated by 100ms, set the local UART to 19200 and check a response. Neither operation supplies power to the robot or wakes an unpowered XIAO.

These operations use `Esp32Transport`, are restricted to maintenance with no active custom motor control, pending sensor request or stream (even paused), and block the loop during the sequence. Before maintenance: `releaseControl()` if in Safe/Full; end the stream with `resynchronizeInput()` and continue `poll()` until draining finishes. On a generic Stream interface without platform control, the operations return `Unsupported` without sending bytes.

`Result::Ok` for these operations requires a fresh Mode=Passive reading after Start. The response is not an encrypted/checksummed protocol ACK; it is a functional check over a direct UART link. Buffered previous responses are drained before the check. `connectionVerified()` indicates a successful link check in the past, not a guarantee that the link is still alive. `baudRate()` is the configured local rate, not evidence that the robot accepted the command.

If no response arrives or local baud configuration fails, there is no automatic rollback to the old rate: the robot may already have changed speed. Control commands are blocked until successful `verifyConnection()` or an explicit successful BRC sequence. Partial transmission or undrained TX locks the transport until protocol state is recovered; there is no blind retry. A pulse exceeding 500ms is ended and remaining pulses are not sent. There is no automatic reset to identify firmware.

XIAO connection: D9/GPIO8 through 4.7k to the NPN base, a 10k base-emitter resistor, emitter to GND and collector to BRC. GPIO HIGH pulls robot BRC LOW; LOW releases it. `beginBrc()` starts released. This drives the planned transistor, rather than connecting GPIO directly to the 5V line. Docked wake has a documented firmware bug before release-3.8.2 / release-stm32-3.7.7; the library can detect a missing response but does not fix iRobot firmware.

## Sensors and units

All individual packets 7–58 and all defined groups 0–6, 100, 101, 106, 107 are decoded. Groups 102–105 do not appear in the manufacturer's table, despite the general wording “100–107”. Requests for unknown IDs are rejected before transmission.

| Range | Data |
|---|---|
| 7–18 | Bumpers/wheels, wall, four cliffs, virtual wall, overcurrent, dirt, reserved byte, IR and buttons |
| 19–20 | Distance and angle since the previous reading — accumulated values consumed on reading, not absolute position |
| 21–26 | Battery data. Percentage is calculated from charge/capacity and marked invalid if capacity is zero or charge exceeds it |
| 27–34 | Wall/cliff signal strength, reserved fields and charging sources |
| 35–42 | OI mode, song, sensor stream and most recent motion commands received by the robot |
| 43–44 | Encoders with rollover. The library returns the 16 bits as an unsigned counter and provides `encoderDelta()` for signed differences |
| 45–53 | Light bumper and six sensor strengths, left/right IR |
| 54–58 | Motor currents and Stasis; Stasis is processed state, not caster position or raw pulses |

`battery()` and `safety()` return structures with readable names and validity. `sensor(Sensor::...)` provides a decoded value, update time, update number and `fresh()`. A battery/safety snapshot requires all fields to arrive in the same update; new voltage is not mixed with old capacity. Less common mask fields, such as LightBumper and Overcurrents, remain available as decoded values; additional Boolean wrappers can be added without changing the protocol.

Counters require sampling often enough to avoid a jump of half the range or more between readings. There is no automatic conversion to position: wheel diameter, wheelbase and slip require calibration. Some firmware versions have incorrect Distance, Angle and RequestedRadius values; the library neither guesses the version nor corrects using an unverified constant.

## Communication limits and recovery

- Roomba UART uses 5V levels versus XIAO's 3.3V, with a level translator and common GND. 8N1, no flow control. LiDAR uses a separate UART.
- `poll()` is nonblocking; call it continuously from one place only. Do not also read the same UART outside the library. Transport `write()` should be short and bounded, such as writing to a local UART buffer.
- Only one sensor request at a time; at least 15ms between requests. Do not mix one-shot responses with a stream, even a paused one. `resynchronizeInput()` pauses and drains until 100ms without data has been observed.
- The stream is validated by header, expected length, packet list and checksum. Sensor updates are atomic after the entire frame is validated. Invalid frames do not replace valid data; data age must still be checked.
- A one-shot response has no header, checksum or request ID. Timeout invalidates data and blocks another request until resynchronization. 100ms of silence is a recovery policy, **not proof** that a delayed response cannot arrive through an external bridge; use streaming for continuous control and a verified session initialization when uncertain.
- If a write stops mid-command, the library locks transmission with `TransportFault`. A new opcode could be interpreted as data for the previous command; it therefore does not automatically send “stop” as though the protocol were still synchronized. Restore the link and robot parser state before creating a new instance.
- Stream-rate calculation prevents requesting more than can be transmitted in 15ms at the configured baud rate. Duplicates and overlapping groups are rejected, avoiding consuming a distance/angle counter twice in one request.

## Specification findings and validation status

The specification contains internal inconsistencies: its checksum example contains decimal bytes 2,25, meaning 537, although the text says 549; we test the value from the bytes themselves. Song numbers are described as four slots and elsewhere as 0–4; the implementation limits them to 0–3. Encoders are stored as modular bits so signedness does not break rollover calculations. This does not confirm a bug or fix in our particular robot's firmware.

Tests cover right/left order, big-endian and two's complement, command boundaries without partial transmission, sensor groups, invalid/stale battery data, split responses, bad checksums, 10,000 noise bytes, resynchronization, timeout across clock rollover, motion expiry and write failure. They are not Roomba, wiring or ESP32 build tests.

Before using motors: identify version and baud rate, first read only battery and OI mode, then safety sensors, and only then issue short motion commands under controlled test conditions. Extensions 43–58, docked wake and display features remain unverified until tested on the 620. The library does not operate LiDAR, implement SLAM, OTA or a web server, or replace the shutdown circuit in POWER_SHUTDOWN.md.


Complete machine-readable mapping: [oi-map.json](oi-map.json) — 30 opcodes including alias 130, 52 individual packets, 11 groups, units, masks and capabilities. `python3 tests/check_oi_map.py` checks for missing IDs, duplicates or nonexistent APIs.


`setSampleObserver(callback, context)` delivers every individual reading with reception time and generation after committing an entire valid frame; it does not merely return the latest value after `poll`. The callback must be short and nonblocking and must not reenter library communication/commands. [DirtDetect collection](DIRT_DETECT.md).

`stasis(now)` decodes packet 58 as a state: 0 = no forward progress (also valid while stopped/turning/reversing); 1 = progress; 2 = caster too dirty to read, requiring inspection, cleaning and drying; 3 = undefined. Only fresh 0/1 readings are marked `usableForProgress`. Do not interpret 2 as stuck or slipping, or as proof that the entire robot is dirty. Support on the 620 still requires physical testing.
