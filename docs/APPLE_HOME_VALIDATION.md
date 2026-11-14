# Apple Home simulator validation — 2026-11-13

Scope: the standalone `firmware/xiao/RooCoreMatterSim` application. Changes are local; no device was flashed, no Apple Home accessory was created, and no real robot command was sent. No Linux server or Cloudflare configuration was changed for this work.

## Completed

| Check | Result and limits |
|---|---|
| Shared C++ state-machine tests | Passed with Clang, AddressSanitizer and UndefinedBehaviorSanitizer |
| Normal cycle | 30-second simulated cleaning → 5-second simulated return → docked |
| Pause/resume | Cleaning and return preserve remaining time; repeated requests do not reset it |
| Cancel/duplicate/invalid commands | Cleaning cancellation, duplicate start/return, unsupported mode and invalid transitions checked. An Idle request during return is a no-op, matching Matter ModeBase's same-mode short circuit |
| Network/restart | Disconnect during running, paused and returning cancels activity. Reconnect does not resume; a fresh start is required. Reconstructed model starts inactive |
| Timer rollover | Normal completion and pause/resume checked across 32-bit milliseconds rollover |
| Source isolation | No robot-library includes or external GPIO/UART control calls in the simulator application |
| Target build | Passed for `esp32:esp32:XIAO_ESP32S3`, Espressif Arduino core **3.3.12**, Arduino CLI **1.5.1**; no compiler warnings in the completed build log |
| Program size | **1,680,854 / 3,342,336 bytes (50%)** reported by Arduino CLI |
| Static RAM | **106,668 / 327,680 bytes (32%)**; runtime heap/stack high-water marks are not measured |
| Linked image inspection | ELF contains the new simulator adapter/delegates; no `RoombaOI`, `RooNavigation` or `RooCoreLidarPower` symbols |
| Local hardware discovery | No `/dev/cu.usb*` device was present. No serial port was opened or toggled |

The host tests exercise the **same `SimVacuum.h` used by the target sketch**. They do not run the Matter stack or emulate Apple Home. Target compilation validates the SDK APIs and linked image, not commissioning, over-the-air commands, subscriptions or attribute rendering.

## Build artifact

Local, ignored artifacts are retained in `.roocore-build/matter-sim/`, including the application, bootloader, partition table, merged image, ELF, map, build log and a source SHA-256 manifest. This is build output, **not a record of a flash operation**.

- Application file: `RooCoreMatterSim.ino.bin`
- File size: **1,681,008 bytes** (image headers/padding differ from the compiler's program-size accounting)
- SHA-256: `bfcb6792e6e6996fca1e116413847121aa6d68ad3d8fc86e0620867066b3f108`
- Board options: `PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=default`
- Production `RooCoreXiao` and assembly wiring/power guides were not changed; this feature is not integrated into the real controller.

## Still unverified

- Physical board identity, installed firmware, standalone power arrangement and recovery/backup procedure.
- Actual boot, entropy/NVS persistence, BLE advertisement, Wi-Fi commissioning and behavior under low memory.
- Apple TV model/software/home-hub status; Apple Home acceptance of the SDK development attestation.
- Discovery of the Robotic Vacuum Cleaner endpoint, available controls, Matter responses, reports and completion event on a controller.
- Removal/recommissioning and persistence after a physical power cycle.
- Real network loss, hub loss, router restart, controller reachability and command delivery behavior. The host model tests do not establish those radio/protocol properties.
- Any SCI/OI behavior, Roomba actuation, electrical safety, physical motion, charging or firmware integration.

The sketch intentionally contains no real control path. ESP32 ROM/SDK startup logging can still use UART0; future board testing must use a fully disconnected XIAO. See the [device test procedure](APPLE_HOME.md#next-device-test--not-performed) and [Hebrew guide](APPLE_HOME-he.md).
