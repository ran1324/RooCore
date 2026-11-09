# Automatic shutdown through D10

Implemented in the XIAO firmware on 2026-11-08. [Wiring diagram](diagrams/wiring-xiao.svg): D10/GPIO9 → 10k resistor → 2N3904 base → pull D36V28F5 EN to ground. Shutdown disconnects the add-on branch, including 5V and 3.3V; it does not disconnect the battery from the original Roomba. No new component is required for the software implementation.

**Code and tests are complete; calibration and validation on the robot have not been performed.** No physical voltage values have been approved. The default is therefore `unconfigured`: data reading is available, while motion and D10 HIGH are blocked. No further code is needed to activate the sequence after measurements and calibration configuration.

## Operating sequence

1. Battery voltage below the software threshold for two seconds and at least five distinct measurements starts shutdown. Readings come from OI packet 22, not charge percentage. A single cached frame is not counted again.
2. Driving, remote control, undocking and navigation recovery commands are blocked. The command queue is emptied and the control epoch changes. Safe is sent, followed by zero wheel and cleaning-motor commands, then Passive to keep native charging available. Neither Full nor the Roomba power-down command is used.
3. The shared `lidarPower(false)` path detaches UART2, cancels any outstanding TX, drives D4 LOW and D5 input pulldown with no pullup, then lowers D3/ON. GPIO7/D8/OE is no longer used. No manual unplugging is needed. The remaining decision requires neither LiDAR nor a server.
4. Fresh, coherent frames are required for voltage, charging state, charging source, OI mode and both encoders. Passive mode and unchanged encoders must be confirmed for one second. Voltage must then remain stable for two seconds, within a 100mV range, with at least five measurements.
5. Proceeding requires no charging source/active charging state and `voltage + reboundMargin < restartMin`. The margin must include measurement error and additional rebound after the XIAO itself shuts down.
6. Above the safe Flash-write threshold, a checkpoint is saved and a `/power-cutoff` marker is written and read back. Checkpoint failure is reported but does not cancel battery protection if the marker was saved. Marker-write failure blocks D10. A corrupt marker found at boot is treated as active.
7. UART data accumulated during writing is invalidated: the stream is stopped, cache and decoder are reset, 100 ms of silence is required, and the stream restarts. Stop confirmation and stable voltage are required again **after** writing; cutoff cannot use the pre-write reading.
8. D10 goes HIGH once and stays there. There are no repeated pulses, deep sleep or deliberate reset. If the controller is still running after two seconds, `/status` reports `cutoff_not_observed`; the system remains held and the pin remains HIGH.

Shutdown preparation is limited to 30 seconds. Motion that has not stopped, missing data or unstable voltage does not count as permission to cut power. Terminal failure transitions to `held`. If voltage is already below the safe-write threshold, neither Flash writing nor D10 assertion occurs; hardware EN protection remains the independent fallback. This is an intentional limit of shutdown requiring a stored marker, not a claim that shutdown is guaranteed under every fault.

Connecting a charger or voltage rising too close to the restart threshold cancels cutoff and enters a hold. A charging error also blocks software cutoff. Loss of Roomba data stops motion and turns off LiDAR; voltage is not inferred from percentage or an old reading. Wi-Fi loss alone does not disable local voltage protection.

## Restart and preventing repeated cycles

The converter restarts through the EN divider even while the XIAO is off. Software cannot wake itself without power. According to [Pololu](https://www.pololu.com/product/3782), nominal EN thresholds are 1.20V off and 1.35V on, with an internal 100k pull-up to VIN. In the planned divider, an external 100k in parallel with the internal resistor and 4.7k+470Ω to ground give approximately 12.81V/14.41V. **This calculation is not calibration or an approved battery safety threshold.**

Every boot starts with LiDAR off, D10 LOW and control held. If a shutdown marker remains, no further D10 attempt occurs until voltage is at least `RESTART_MIN_MV`, stable for five seconds after stop confirmation. Only then is the marker cleared. Without a marker, `RECOVER_MV` is used. Activation is voltage-based and does not require charger detection. Cleaning does not resume automatically: a new command is required, and after reboot, position must also be anchored using the existing recovery mechanism.

The marker, margin and lack of automatic cleaning resumption reduce repeated cycles after voltage rebound; they do not guarantee the behavior of an unmeasured battery. If voltage after load removal can cross the EN threshold, calibration/resistor ratio must be corrected. EN is not a latch, and off-state consumption is not zero.

## Configuration after measurement

In `firmware/xiao/RooCoreXiao/config.local.h`, following the updated template:

| Setting | Meaning |
|---|---|
| `ROO_POWER_CUTOFF_VERIFIED` | `true` only after verifying the circuit, battery pack and voltages |
| `ROO_POWER_CUTOFF_MV` | Low threshold under load that starts the sequence, above the hardware cutoff threshold |
| `ROO_POWER_RECOVER_MV` | Stable voltage permitting a new motion request after a hold without a marker |
| `ROO_POWER_RESTART_MIN_MV` | **Lowest measured** restart threshold under operating conditions |
| `ROO_POWER_REBOUND_MARGIN_MV` | Maximum rebound after final cutoff, including OI measurement error |
| `ROO_POWER_SAVE_MIN_MV` | Voltage permitting stable Flash writes, above the hardware EN threshold with a margin |

`ROO_COMMISSIONED=true` and `ROO_ENCODERS_VERIFIED=true` are also required. Calibration is rejected if values are zero or violate these relationships: `saveMin + 100 ≤ cutoff`, `cutoff + 100 ≤ recover < restartMin`, and `cutoff + reboundMargin < restartMin`. All values are in mV. An old local file without the settings block does not silently authorize cutoff or cleaning.

**Values in `tests/build_xiao.sh` are synthetic compilation data only; do not flash them as calibration.** A nominal 14.4V battery label does not establish chemistry, discharge threshold, charging voltage or rebound.

## Diagnostics and tests

`GET /status` includes `power_state`, `power_reason`, `power_configured`, `power_intent_pending`, the D10 assertion count, cutoff time and voltage, and checkpoint-save result. `running` means the guard permits motion, not that the robot is already moving. `init-storage` may not erase an active shutdown marker or run during preparation/cutoff.

Successful cutoff may remove power before an `asserted` report reaches the server; absence of a final report does not prove failure. The marker is saved before D10 assertion and checked at the next boot.

`tests/power_shutdown_test.cpp` checks under ASan/UBSan: calibration thresholds, filtering brief dips, repeated/stale measurements, zero voltage, the successful sequence, server-ownership cancellation, charger/fault/rebound mid-sequence, moving wheels, timeout, stop/save/marker-clear failure, checkpoint-only failure, readings before/after writing, reboot with a marker and millis rollover. Simulated traffic passes through the real OI library and verifies the stop-command sequence and invalidation of old frames before stream resumption.

**Pending hardware validation:** VIN versus OI voltage, actual EN thresholds, off-state consumption, pack settling and rebound, stop timing, LittleFS writes during falling voltage, reboot/charging and absence of backfeeding. Before connecting USB, physically disconnect the XIAO 5V supply in the diode-free configuration. No firmware was flashed and no physical Roomba was tested as part of this change.

LiDAR power sequencing is integrated into the full application; see [the integration record](assembly/FIRMWARE_INTEGRATION.md). Intentional shutdown is sequenced before D10; unexpected loss of controller power remains a physical qualification case.
