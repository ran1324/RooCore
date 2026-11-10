# LiDAR shutdown without unplugging wires

2026-11-10 · Revision 5 · Reviewed using electronics and esp32-development.

The chosen arrangement uses two permanent UART wires and prepares their GPIO states before switching LiDAR power off. It uses the purchased XIAO ESP32S3, Pololu #2810 and GH cable, with no additional resistor, capacitor, transistor, diode or level shifter in this path.

The sequence is integrated into the full RooCoreXiao firmware on `main`. Builds and host tests passed; no flashing or physical measurements have been performed. It handles intentional LiDAR shutdown while XIAO remains powered, including the step before a D10 whole-system cutoff request. Software sequencing does not provide hardware isolation during reset, unexpected power loss or electrical faults.

[Stage 7](RooCore-assembly.html#s-uart) · [Photo wiring](photo-uart.svg) · [Combined view](combined-wiring.html) · [Schematic](electrical-schematic.svg) · [Hebrew](../LIDAR_POWER_UART.md)

## Permanent connections

View XIAO from the component side with USB at the top. For LiDAR, look **into the sensor socket**, as in manufacturer Figure 3: VCC, TX, GND, RX from left to right. Do not copy that order onto the cable plug's wire-entry side.

| Wire | From | To | Purpose |
|---|---|---|---|
| 9 | D3/GPIO4, fourth pad down the left edge | S1 ON | Power control; mechanical switch OFF |
| 10 | S1 VOUT | LiDAR VCC | Switched 5V, not a signal |
| 11 | GND BUS | LiDAR GND | Common return; XIAO joins it through wire 7 |
| 12 | D4/GPIO5, fifth pad down the left edge | LiDAR RX | Controller transmit, 3.3V |
| 13 | LiDAR TX | D5/GPIO6, sixth pad down the left edge | Controller receive, 3.3V |

Use the supplied 28AWG, 15cm GH tails without an extension in this qualification. Green/purple in the drawings are suggested labels, not proof of cable function. Map contacts by continuity with the cable disconnected. Neither NOYITO is used here. Do not attach 5V, battery voltage or a 3V3 pullup to either signal. D4/D5 are reserved for this UART; their printed SDA/SCL labels do not force I²C operation.

## Shutdown order

1. Stop accepting new writes. A single task owns UART2 and D3/D4/D5.
2. Check TX completion without blocking the control loop. Any remaining transmission is canceled next while LiDAR still has power.
3. Call `HardwareSerial.end()` to detach UART from the pins.
4. Drive **D4 LOW**. Configure **D5 as an input with internal pulldown and no pullup**; never drive against the sensor's TX output.
5. Only then drive **D3 LOW** to disable S1 and remove LiDAR 5V.

The cable stays connected. The pulldown is inside the controller, not a resistor to solder. Neither signal receives a positive drive or pullup from XIAO in the commanded-off state. This removes a potential host-side injection source; it is not galvanic isolation.

Stopping byte writes or calling `flush()` alone is insufficient because idle UART TX is HIGH. `gpio_reset_pin()` alone is also unsuitable: Espressif documents that it enables a pullup. The adapter explicitly configures the GPIO states after detaching UART.

## Startup order

1. Keep D4 LOW and D5 as an input with pulldown.
2. Raise D3 to enable S1.
3. Continue the robot control loop while the measured rail-settling interval elapses; do not use a blocking startup delay. The reviewed sensor document does not supply a guaranteed interval for this assembled rail. Zero deliberately prevents startup.
4. Start UART2 at **230400, 8N1, RX=GPIO6, TX=GPIO5**, without inversion or flow control. Disable the internal positive RX pull before `begin()`.
5. Send the scan command and validate sensor responses separately. Opening UART does not prove a running scanner or valid distance data.

## Implementation and limits

[Arduino adapter](../../../lib/RooCoreLidarPower/src/RooCoreLidarPower.h) · [Portable state machine](../../../lib/RooCoreLidarPower/src/LidarPowerSequence.h) · [Optional off-by-default example](../../../lib/RooCoreLidarPower/examples/OffByDefault/OffByDefault.ino) · [Integration record](FIRMWARE_INTEGRATION.md).

Arduino-ESP32 **3.3.12** was inspected and used for builds. Its `enableRxInternalPull(false)` is called before `begin()`. `end()` detaches pin routing and removes the UART driver; explicit GPIO setup follows. Recheck behavior and builds when changing core version.

`setup()` calls `lidarPort.begin()` before networking/tasks. Afterward, `controlTask` owns UART2 and the three pins; UART1 remains for Roomba. `lidarPower(true)` calls `requestOn(now, ROO_LIDAR_RISE_MS)`. `serviceLidar(now)` advances startup and sends `A5 60` once. `lidarPower(false)` is the shared shutdown path, also called by `FirmwarePowerPort::quiesce()` before D10. It clears stale parser data and scan validity. There is no D6/D7 LiDAR UART or D8/OE control.

States are `off`, `warming`, `ready` and `fault`. `ready` means UART ready, not valid fresh scans. UART/command failure enters `fault` with power off. Repeated enable requests cannot cycle power; an explicit normal off transition is required before another attempt.

Defaults are **`ROO_LIDAR_POWER_VERIFIED=false` and `ROO_LIDAR_RISE_MS=0`**. Configure them only after the required power/signal checks and a measured settling interval with margin. Missing fields in an older configuration also keep power disabled. The removed 500ms constant and the synthetic 25ms build fixture are not installation settings.

`/status` exposes `lidar_power_state`, `lidar_power_configured`, `lidar_uart_ready` and `lidar_rise_ms`. Other tasks must not use Serial2, I²C on D4/D5 or direct D3 writes. HTTP requests use the control task queue.

Commands are limited to 64 bytes with no software TX buffer: 64×10/230400 is 2.78ms of wire time. On shutdown, `uart_wait_tx_done(UART_NUM_2, 0)` only probes completion; `end()` cancels remaining output before safe GPIO setup and S1 shutdown. There is no added 100ms drain wait or blocking rail-settling delay.

Full firmware built in two configurations, plus two RoombaOI examples. Host tests cover 100 modeled cycles, canceled startup, `millis()` wrap, stale RX, busy TX and partial initialization failure. These are not physical power cycles or electrical timing measurements.

## Pending physical qualification

Start with the isolated current-limited source described in the guide, not the robot battery. Use black in COM and red in V/Ω for DC voltage. Attach probes and change wiring with power off. Reference the oscilloscope only to the selected common GND in the isolated test arrangement.

| Check | Expected result / acceptance | Status |
|---|---|---|
| Unpowered continuity | Wire 12 joins only D4 to RX; 13 only TX to D5. Neither signal joins 5V or the other signal | Not performed |
| Controller startup, sensor initially disconnected | D3 and D4 near 0V; D5 input without pullup; no unwanted S1 enable | Not performed |
| Before UART enable | LiDAR-end VCC has reached and remains within 4.8–5.2V including uncertainty. Record a settling interval with margin from the measured transition | Not performed; interval unknown |
| Shutdown waveform | D4 is LOW and D5 has no positive pull before VCC falls. VCC then decays toward 0V; a sustained positive rail or rebound needs investigation | Not performed |
| Sensor TX at controller D5 | HIGH above 0.75×VDD and LOW below 0.25×VDD, with no GPIO-limit violations. Measure during actual data, not only with a DC meter | Not performed |
| Reset, brownout, controller/system power loss and sleep | No signal powers an unpowered rail and no unintended enable; capacitor discharge and reset mux behavior are outside the software model | Not performed; these states are not qualified |

YDLIDAR lists HIGH as 2.4–3.5V without clearly separating VOH from VIH. ESP32-S3 requires at least 2.475V at VDD=3.3V. Consequently, a guaranteed all-corners margin is not established; the direct 3.3V connection requires signal-level qualification. This does not require manual unplugging during normal shutdown.

USB service remains separate: remove XIAO from the whole external harness before USB in the selected single-source, no-added-diode arrangement. Do not manually remove only LiDAR 5V while leaving UART active, or enter sleep with signal pins held HIGH.

## Sources

- [Seeed XIAO ESP32S3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/).
- [YDLIDAR T-mini Plus, Figure 3 and Tables 2–4](../sources/YDLIDAR-T-mini-Plus-v1.1.pdf).
- [Pololu #2810](https://www.pololu.com/product/2810), [switch schematic](../sources/Pololu-2810-schematic.pdf).
- [Espressif GPIO API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html).
- [Arduino-ESP32 3.3.12 UART implementation](https://github.com/espressif/arduino-esp32/blob/3.3.12/cores/esp32/esp32-hal-uart.c), [HardwareSerial](https://github.com/espressif/arduino-esp32/blob/3.3.12/cores/esp32/HardwareSerial.cpp).
- [ESP32-S3 v2.2, Tables 2-1, 2-2 and 5-4](../sources/Espressif-ESP32-S3-v2.2.pdf). GPIO4/5/6 are not strapping pins; actual startup behavior still needs measurement.
